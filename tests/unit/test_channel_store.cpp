// ChannelStore 单测——契约 docs/ts-pipeline-concurrency.md §1/§3（R-33 方案② / R-60/R-61/R-62 定稿）。
// 快照分步用例按 §4 反 flaky 纪律显式驱动；压力用例只断言终态。
// 矩阵 #13（R-61 内存序）为代码评审检查点：recheckIndex 内的 acquire fence 由评审把关，无法运行时断言。
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

#include "pipeline/ChannelStore.h"

using gayospace::pipeline::ChannelStore;

namespace {

// 第 i 帧第 c 通道的确定性样本值：(i * 10 + c) —— 行内可同时校验帧号与通道号（跨通道同帧对齐）
std::vector<float> frame(std::uint64_t i, std::size_t ch)
{
    std::vector<float> v(ch);
    for (std::size_t c = 0; c < ch; ++c)
        v[c] = static_cast<float>(i * 10 + static_cast<std::uint64_t>(c));
    return v;
}

} // namespace

// 矩阵 #6：ChannelStore 快照一致性（写 N 帧后读窗口 == 期望序列）
TEST_CASE("channel store window snapshot matches written frames", "[pipeline][store]")
{
    ChannelStore store(3, 16);
    for (std::uint64_t i = 0; i < 10; ++i) {
        const auto f = frame(i, 3);
        store.writeFrame(f.data());
    }
    REQUIRE(store.frameIndex() == 10);

    auto w = store.readWindow(10);
    REQUIRE(w.ok);
    REQUIRE(w.start == 0);
    REQUIRE(w.count == 10);
    REQUIRE(w.samples.size() == 30);
    for (std::uint64_t i = 0; i < 10; ++i)
        for (std::size_t c = 0; c < 3; ++c)
            REQUIRE(w.samples[static_cast<std::size_t>(i) * 3 + c]
                == static_cast<float>(i * 10 + c));

    // 请求窗长 5：返回最近 5 帧 [5, 10)
    auto w5 = store.readWindow(5);
    REQUIRE(w5.ok);
    REQUIRE(w5.start == 5);
    REQUIRE(w5.count == 5);
    REQUIRE(w5.samples[0] == 50.0f);
}

// 矩阵 #6 补充：环形回绕——写超过容量后读窗口仍取最新、取模寻址正确
TEST_CASE("channel store wraps past capacity and keeps latest frames", "[pipeline][store]")
{
    ChannelStore store(2, 8);
    for (std::uint64_t i = 0; i < 20; ++i)
        store.writeFrame(frame(i, 2).data());

    auto w = store.readWindow(8);
    REQUIRE(w.ok);
    REQUIRE(w.start == 12);
    REQUIRE(w.count == 8);
    for (std::uint64_t i = 0; i < 8; ++i)
        for (std::size_t c = 0; c < 2; ++c)
            REQUIRE(w.samples[static_cast<std::size_t>(i) * 2 + c]
                == static_cast<float>((12 + i) * 10 + c));
}

// 矩阵 #7：复测重试——发布推进致复测失败 → 重试成功（注入交错模拟）
TEST_CASE("channel store recheck failure retries and recovers (R-61 steps)", "[pipeline][store][r61]")
{
    ChannelStore store(2, 16);
    for (std::uint64_t i = 0; i < 10; ++i)
        store.writeFrame(frame(i, 2).data());

    // 分步模拟：读侧取 idx0 → 拷贝窗口 → 写侧发布推进 → 复测失败
    const std::uint64_t idx0 = store.snapshotIndexLoad();
    REQUIRE(idx0 == 10);
    std::vector<float> out(8); // count × chCount
    store.copyFrames(6, 4, out);
    REQUIRE(out[0] == 60.0f);

    store.writeFrame(frame(10, 2).data()); // 写侧发布推进：10 → 11
    REQUIRE_FALSE(store.recheckIndex(idx0)); // 复测失败：窗口读期间有新帧发布

    // 重试路径：readWindow 重新取索引 → 成功（无并发写）
    auto w = store.readWindow(4);
    REQUIRE(w.ok);
    REQUIRE(w.start == 7);
    REQUIRE(w.count == 4);
    REQUIRE(w.samples[0] == 70.0f);
    REQUIRE(w.samples[6] == 100.0f); // 最新帧 10 的 ch0
}

// 矩阵 #7 补充：连续失败超限 → 跳过本渲染周期（真线程注入，只断言终态恢复）
TEST_CASE("channel store degrades after repeated recheck failures then recovers", "[pipeline][store][r61]")
{
    ChannelStore store(1, 64);
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        std::vector<float> f{0.0f};
        while (!stop.load(std::memory_order_acquire)) {
            f[0] += 1.0f;
            store.writeFrame(f.data()); // 持续发布，制造复测竞争
        }
    });

    int degraded = 0;
    int succeeded = 0;
    for (int i = 0; i < 200; ++i) {
        auto w = store.readWindow(64); // 全窗读：发布频繁 → 复测失败概率升高
        if (w.ok)
            ++succeeded;
        else
            ++degraded; // 设计内降级：跳过本渲染周期，不阻塞采集
    }
    stop.store(true, std::memory_order_release);
    writer.join();

    REQUIRE(succeeded + degraded == 200);
    // 终态恢复：写侧停止后 readWindow 必然成功
    auto w = store.readWindow(64);
    REQUIRE(w.ok);
}

// 矩阵 #8：帧序号时间基准——frameIndex 单调递增（0 基帧序号），t = t0 + index/fs 的 index 口径可回放
TEST_CASE("channel store frameIndex is monotonic and matches frame order", "[pipeline][store]")
{
    ChannelStore store(1, 32);
    std::uint64_t last = 0;
    for (std::uint64_t i = 0; i < 100; ++i) { // 帧序号 0 基：第 i 次写发布序号 i+1
        const auto f = frame(i, 1);
        store.writeFrame(f.data());
        const auto idx = store.frameIndex();
        REQUIRE(idx == i + 1);       // 每帧发布恰好 +1
        REQUIRE(idx > last);         // 单调递增
        last = idx;
    }
    // 窗口起始即帧序号：start + i 即可换算 t = t0 + (start + i) / fs
    auto w = store.readWindow(3);
    REQUIRE(w.start == 97);
    REQUIRE(w.count == 3);
    for (std::uint64_t i = 0; i < 3; ++i)
        REQUIRE(w.samples[static_cast<std::size_t>(i)]
            == static_cast<float>((97 + i) * 10));
}

// 矩阵 #10：启动未满窗（R-60）——idx0 < window 时窗口起点 clamp 到 0，无负索引/越界
TEST_CASE("channel store startup clamps window start to zero (R-60)", "[pipeline][store][r60]")
{
    ChannelStore store(2, 16);
    for (std::uint64_t i = 0; i < 5; ++i)
        store.writeFrame(frame(i, 2).data());

    auto w = store.readWindow(10); // 请求 10 帧，仅 5 帧已发布
    REQUIRE(w.ok);
    REQUIRE(w.start == 0);
    REQUIRE(w.count == 5);
    REQUIRE(w.samples.size() == 10);
    REQUIRE(w.samples[0] == 0.0f);
    REQUIRE(w.samples[9] == static_cast<float>(4 * 10 + 1));
}

// 矩阵 #11：跨通道同帧对齐（R-62）——任一采样行的 ch0..chN 均来自同一帧
TEST_CASE("channel store frame-major layout keeps channels of a row in the same frame (R-62)",
    "[pipeline][store][r62]")
{
    ChannelStore store(4, 16);
    for (std::uint64_t i = 0; i < 12; ++i)
        store.writeFrame(frame(i, 4).data());

    auto w = store.readWindow(6);
    REQUIRE(w.ok);
    REQUIRE(w.count == 6);
    for (std::uint64_t r = 0; r < w.count; ++r) {
        const auto base = static_cast<std::size_t>(r) * 4;
        // 行内 4 个样本必须同帧：值编码同时含帧号与通道号
        const auto frameNo = w.start + r;
        for (std::size_t c = 0; c < 4; ++c)
            REQUIRE(w.samples[base + c] == static_cast<float>(frameNo * 10 + c));
    }
}

// 矩阵 #6 补充：空 store / 请求 0 帧 / 超容量窗长的边界
TEST_CASE("channel store edge cases: empty store, zero window, oversized window", "[pipeline][store]")
{
    ChannelStore store(2, 8);

    auto w0 = store.readWindow(4); // 空 store：count=0，仍 ok
    REQUIRE(w0.ok);
    REQUIRE(w0.count == 0);
    REQUIRE(w0.samples.empty());

    for (std::uint64_t i = 0; i < 3; ++i)
        store.writeFrame(frame(i, 2).data());

    auto wz = store.readWindow(0); // 请求 0 帧
    REQUIRE(wz.ok);
    REQUIRE(wz.count == 0);

    auto wb = store.readWindow(100); // 超容量窗长：clamp 到已发布帧数
    REQUIRE(wb.ok);
    REQUIRE(wb.count == 3);
}

// R-70：降级路径确定性覆盖——测试注入接缝强制三次复测失败，
// 断言 readWindow 返回 ok=false（跳过本渲染周期）且数据/索引状态未被破坏。
TEST_CASE("channel store degrades deterministically after forced recheck failures (R-70)",
    "[pipeline][store][r70]")
{
    ChannelStore store(2, 16);
    for (std::uint64_t i = 0; i < 10; ++i)
        store.writeFrame(frame(i, 2).data());

    store.testSetForceRecheckFailure(true); // 复测恒失败
    auto w = store.readWindow(4);
    REQUIRE_FALSE(w.ok); // 设计内降级：不阻塞、不破坏状态
    REQUIRE(w.count == 0);
    REQUIRE(w.samples.empty());

    store.testSetForceRecheckFailure(false); // 关闭注入后立即恢复
    auto w2 = store.readWindow(4);
    REQUIRE(w2.ok);
    REQUIRE(w2.start == 6);
    REQUIRE(w2.count == 4);
    REQUIRE(store.frameIndex() == 10); // 索引未被降级路径触碰
}

// R-68：copyFrames 边界——out 尺寸不足时安全返回，不越界
TEST_CASE("channel store copyFrames rejects undersized output (R-68)", "[pipeline][store][r68]")
{
    ChannelStore store(2, 16);
    for (std::uint64_t i = 0; i < 4; ++i)
        store.writeFrame(frame(i, 2).data());

    std::vector<float> tooSmall(3); // 需要 4×2=8，只给 3
    store.copyFrames(0, 4, tooSmall);
    REQUIRE(tooSmall == std::vector<float>(3, 0.0f)); // 未写入任何数据

    std::vector<float> ok(8);
    store.copyFrames(0, 4, ok);
    REQUIRE(ok[0] == 0.0f);
    REQUIRE(ok[7] == static_cast<float>(3 * 10 + 1));
}
