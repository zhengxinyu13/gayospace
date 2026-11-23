// PipelinePump 胶水层单测（第 21 轮评审 §2.3 五类场景 + ring.clear() + stats 汇总）。
// 全部单线程 pumpOnce/drain 显式步进驱动，不碰真串口、真线程（评审 §2.2.4）。
// JustFloat 帧格式：小端 float × N + 帧尾 00 00 80 7F（TS §4.1）；
// FireWater 帧格式：CSV 文本行（TS §4.2）。
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "applogic/PipelinePump.h"
#include "pipeline/SpscRingBuffer.h"
#include "protocol/FireWaterParser.h"
#include "protocol/JustFloatParser.h"

using namespace gayospace;
using Catch::Matchers::WithinRel;

namespace {

std::vector<std::byte> toBytes(const std::string& s)
{
    std::vector<std::byte> out;
    for (const char c : s) {
        out.push_back(static_cast<std::byte>(c));
    }
    return out;
}

// JustFloat：float 小端 4 字节 × N + 帧尾
std::vector<std::byte> f32(float v)
{
    const auto u = std::bit_cast<std::uint32_t>(v);
    return {
        static_cast<std::byte>(u & 0xFFu),
        static_cast<std::byte>((u >> 8) & 0xFFu),
        static_cast<std::byte>((u >> 16) & 0xFFu),
        static_cast<std::byte>((u >> 24) & 0xFFu)};
}

std::vector<std::byte> justFrame(const std::vector<float>& values)
{
    std::vector<std::byte> out;
    for (const float v : values) {
        const auto b = f32(v);
        out.insert(out.end(), b.begin(), b.end());
    }
    out.push_back(std::byte{0x00});
    out.push_back(std::byte{0x00});
    out.push_back(std::byte{0x80});
    out.push_back(std::byte{0x7F});
    return out;
}

// 读 store 窗口首帧样本（frame-major；readWindow 返回最近 N 帧，取窗口起始帧）
std::vector<float> firstFrame(applogic::PipelinePump& pump, std::size_t ch)
{
    const auto total = pump.stats().frames;
    REQUIRE(total >= 1);
    const auto w = pump.store().readWindow(total);
    REQUIRE(w.ok);
    REQUIRE(w.count == total);
    return {w.samples.begin(), w.samples.begin() + static_cast<std::ptrdiff_t>(ch)};
}

} // namespace

TEST_CASE("pump moves batch bytes across frames into store", "[applogic][pump]")
{
    // 场景①批量跨帧：一次 ring.write 写 3 帧字节 → 单次 pumpOnce → store 3 帧
    pipeline::SpscRingBuffer ring(1024);
    applogic::PipelinePump pump(ring, std::make_unique<protocol::JustFloatParser>(2), 2, 64);

    std::vector<std::byte> stream;
    for (const auto f : {justFrame({1, 2}), justFrame({3, 4}), justFrame({5, 6})}) {
        stream.insert(stream.end(), f.begin(), f.end());
    }
    REQUIRE(ring.write(stream.data(), stream.size()) == stream.size());

    const auto frames = pump.pumpOnce();
    REQUIRE(frames == 3);
    REQUIRE(pump.stats().frames == 3);
    REQUIRE(pump.stats().chCount == 2);

    const auto v = firstFrame(pump, 2);
    REQUIRE_THAT(v[0], WithinRel(1.0F));
    REQUIRE_THAT(v[1], WithinRel(2.0F));
}

TEST_CASE("pump keeps partial frame across feeds", "[applogic][pump]")
{
    // 场景②残帧跨 feed：帧字节拆两半，各走一次 pumpOnce；残帧不出帧、不误计 bad
    pipeline::SpscRingBuffer ring(1024);
    applogic::PipelinePump pump(ring, std::make_unique<protocol::JustFloatParser>(1), 1, 64);

    const auto whole = justFrame({7.5F});
    REQUIRE(ring.write(whole.data(), whole.size() / 2) == whole.size() / 2);
    REQUIRE(pump.pumpOnce() == 0); // 残帧不产帧
    REQUIRE(pump.stats().framesBad == 0);

    REQUIRE(ring.write(whole.data() + whole.size() / 2, whole.size() - whole.size() / 2)
        == whole.size() - whole.size() / 2);
    REQUIRE(pump.pumpOnce() == 1); // 补齐后完整出帧
    const auto v = firstFrame(pump, 1);
    REQUIRE_THAT(v[0], WithinRel(7.5F));
    REQUIRE(pump.stats().framesOk == 1);
}

TEST_CASE("pump rebuilds store on channel count change", "[applogic][pump]")
{
    // 场景③通道数变更：initialChCount 与 parser 首帧不符 → 首帧触发重建列布局
    // （同协议内字段数变化由 parser 计 bad 不产出——TS §3.3，属 parser 单测职责）
    pipeline::SpscRingBuffer ring(1024);
    applogic::PipelinePump pump(ring, std::make_unique<protocol::FireWaterParser>(), 1, 64);

    REQUIRE(pump.stats().chCount == 1);
    REQUIRE(pump.stats().storeRebuilds == 1); // 仅构造时

    const auto l1 = toBytes("1,2,3\n");
    REQUIRE(ring.write(l1.data(), l1.size()) == l1.size());
    REQUIRE(pump.pumpOnce() == 1);
    REQUIRE(pump.stats().chCount == 3);       // 首帧 3 通道 ≠ store 1 → 重建
    REQUIRE(pump.stats().storeRebuilds == 2);
    REQUIRE(pump.stats().frames == 1);        // 重建后 frameIndex 从 0 起算

    const auto l2 = toBytes("4,5,6\n");
    REQUIRE(ring.write(l2.data(), l2.size()) == l2.size());
    REQUIRE(pump.pumpOnce() == 1);
    REQUIRE(pump.stats().chCount == 3);       // 字段数一致不再重建
    REQUIRE(pump.stats().storeRebuilds == 2);
    REQUIRE(pump.stats().frames == 2);
    // 重建发生在首帧写入前：frame 0 = "1,2,3"，frame 1 = "4,5,6"（窗口起始帧为前者）
    const auto v = firstFrame(pump, 3);
    REQUIRE_THAT(v[0], WithinRel(1.0F));
    REQUIRE_THAT(v[2], WithinRel(3.0F));
}

TEST_CASE("pump switches parser and clears ring", "[applogic][pump]")
{
    // 场景④协议切换：switchParser → ring 清空（dropped 累加）→ 新协议正常解析
    pipeline::SpscRingBuffer ring(1024);
    applogic::PipelinePump pump(ring, std::make_unique<protocol::JustFloatParser>(2), 2, 64);

    // 先喂 JustFloat 帧并解析
    const auto f = justFrame({9, 8});
    REQUIRE(ring.write(f.data(), f.size()) == f.size());
    REQUIRE(pump.pumpOnce() == 1);
    REQUIRE(pump.stats().ringBytes == f.size());

    // 切到 FireWater（时序：上层已暂停写入与解析线程）
    pump.switchParser(std::make_unique<protocol::FireWaterParser>(), 3);
    REQUIRE(pump.stats().droppedBytes == 0);  // 上一批已消费，无残留
    REQUIRE(pump.stats().chCount == 3);
    REQUIRE(pump.stats().storeRebuilds == 2); // 构造 1 次 + 切换重建 1 次

    // 新协议正常解析；rxBytes 口径 = ring.writePos 累计（clear 只对齐读侧序号，不重置写侧）
    const auto line = toBytes("0.25,0.5,0.75\n");
    REQUIRE(ring.write(line.data(), line.size()) == line.size());
    REQUIRE(pump.pumpOnce() == 1);
    REQUIRE(pump.stats().ringBytes == f.size() + line.size());
    const auto v = firstFrame(pump, 3);
    REQUIRE_THAT(v[0], WithinRel(0.25F));
    REQUIRE_THAT(v[2], WithinRel(0.75F));
}

TEST_CASE("pump drain empties ring and pending frames", "[applogic][pump]")
{
    // 场景⑤停止排空：多批字节 → drain 后 writePos == readPos、无待产帧
    pipeline::SpscRingBuffer ring(1024);
    applogic::PipelinePump pump(ring, std::make_unique<protocol::FireWaterParser>(), 2, 64);

    for (int i = 0; i < 5; ++i) {
        const auto line = toBytes("1,2\n");
        REQUIRE(ring.write(line.data(), line.size()) == line.size());
    }
    REQUIRE(pump.drain() == 5);
    REQUIRE(ring.writePos() == ring.readPos());
    REQUIRE(pump.drain() == 0); // 再 drain 无帧产出
    REQUIRE(pump.stats().frames == 5);
}

TEST_CASE("pump transmits ring overflow stats", "[applogic][pump]")
{
    // ring 满载丢弃透传：write 溢出部分 → droppedBytes；pump stats 透传不重复计数
    pipeline::SpscRingBuffer ring(64);
    applogic::PipelinePump pump(ring, std::make_unique<protocol::JustFloatParser>(1), 1, 64);

    const auto big = justFrame({1});
    std::vector<std::byte> flood;
    for (int i = 0; i < 100; ++i) { // 400 字节 >> 64 容量
        flood.insert(flood.end(), big.begin(), big.end());
    }
    const auto written = ring.write(flood.data(), flood.size());
    REQUIRE(written < flood.size()); // 有丢弃

    pump.drain(); // 丢弃后的可解析内容不确定（截断帧），只验 stats 口径
    REQUIRE(pump.stats().ringBytes == written);
    // 满载丢"新"计 rxDropped（ring 写侧口径）；droppedBytes 仅丢"旧"（clear/切换）时增加
    REQUIRE(pump.stats().rxDropped == flood.size() - written);
}

TEST_CASE("pump handles empty ring batch safely", "[applogic][pump]")
{
    // ok=false 路径（ring 空）：cancelBatch 为 no-op，pumpOnce 返回 0 且不破坏后续读取
    pipeline::SpscRingBuffer ring(256);
    applogic::PipelinePump pump(ring, std::make_unique<protocol::JustFloatParser>(1), 1, 64);

    REQUIRE(pump.pumpOnce() == 0);
    REQUIRE(ring.writePos() == ring.readPos()); // owner 状态未被破坏（后续批次正常）
    const auto f = justFrame({3});
    REQUIRE(ring.write(f.data(), f.size()) == f.size());
    REQUIRE(pump.pumpOnce() == 1);
    REQUIRE_THAT(firstFrame(pump, 1)[0], WithinRel(3.0F));
}

TEST_CASE("ring clear aligns read to write and counts dropped", "[applogic][pipeline]")
{
    // ring.clear()（TS §3.2/R-88）：readPos 追平 writePos，未读字节计入 droppedBytes
    pipeline::SpscRingBuffer ring(256);
    const auto data = justFrame({1});
    REQUIRE(ring.write(data.data(), data.size()) == data.size());
    REQUIRE(ring.droppedBytes() == 0);

    ring.clear();
    REQUIRE(ring.readPos() == ring.writePos());
    REQUIRE(ring.droppedBytes() == data.size());
    // 清空后可直接写入并正常读取（序号对齐未破坏契约）
    const auto f2 = justFrame({2});
    REQUIRE(ring.write(f2.data(), f2.size()) == f2.size());
    auto batch = ring.beginBatch(256);
    REQUIRE(batch.ok);
    std::vector<std::byte> out(f2.size());
    REQUIRE(ring.copyBatch(batch, out.data(), out.size()) == f2.size());
    ring.publish(batch);
}

TEST_CASE("pump rejects out-of-range initial channel count", "[applogic][pump]")
{
    // R-89：initialChCount 上界校验（拒绝 >kMaxChannels，与 JustFloatParser R-76 同风格）
    pipeline::SpscRingBuffer ring(256);
    REQUIRE_THROWS_AS(
        applogic::PipelinePump(ring, std::make_unique<protocol::FireWaterParser>(), 33, 64),
        std::invalid_argument);
    REQUIRE_THROWS_AS(
        applogic::PipelinePump(ring, std::make_unique<protocol::FireWaterParser>(), 0, 64),
        std::invalid_argument);
}

TEST_CASE("pump stats snapshot stays safe under concurrent reader", "[applogic][pump]")
{
    // R-85 验收：1 线程 pump + 1 线程高频 stats()，store 反复被 rebuildStore 替换——
    // stats() 只读原子缓存，不崩、不 UAF。有限时长压测（确定性 join），无 flaky 风险。
    // 通道数变化用 switchParser 驱动（FireWater 行内字段数变化只计 bad 不产出——TS §3.3）。
    pipeline::SpscRingBuffer ring(4096);
    applogic::PipelinePump pump(ring, std::make_unique<protocol::FireWaterParser>(), 3, 64);

    std::atomic<bool> stopReader {false};
    std::thread reader([&] {
        while (!stopReader.load(std::memory_order_relaxed)) {
            const auto s = pump.stats(); // 不得解引用 parser_/store_
            static_cast<void>(s);
        }
    });

    const auto wide = toBytes("1,2,3\n");
    for (int i = 0; i < 100; ++i) {
        REQUIRE(ring.write(wide.data(), wide.size()) == wide.size());
        pump.pumpOnce();
        if (i % 20 == 19) { // 每 20 帧交替重建 store（1↔3），替换 store_ 压 UAF 路径
            pump.switchParser(std::make_unique<protocol::FireWaterParser>(),
                (i % 40 == 19) ? 1 : 3);
        }
    }
    pump.drain();
    for (int i = 0; i < 3; ++i) {
        REQUIRE(ring.write(wide.data(), wide.size()) == wide.size());
    }
    pump.drain();

    stopReader.store(true, std::memory_order_relaxed);
    reader.join();

    const auto s = pump.stats();
    REQUIRE(s.frames == 3);   // 最后一次切换后写入的 3 帧
    REQUIRE(s.chCount == 3);  // 最终列布局
}
