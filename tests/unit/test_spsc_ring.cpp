// SPSC 环形字节缓冲单测——契约 docs/ts-pipeline-concurrency.md §2/§3（R-34/R-59/R-64/R-65 定稿）。
// 逻辑/交错用例按 §4 反 flaky 纪律：读写动作暴露为显式步骤，单线程按任意交错序列驱动，
// 不依赖真实线程时序；压力用例只断言终态守恒（矩阵 #9）。
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <thread>
#include <vector>

#include "pipeline/SpscRingBuffer.h"

using gayospace::pipeline::SpscRingBuffer;

namespace {

// 生成确定性模式字节：第 i 字节 = (seed + i) & 0xFF
std::vector<std::byte> pattern(std::size_t n, std::uint8_t seed = 0)
{
    std::vector<std::byte> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = static_cast<std::byte>(static_cast<std::uint8_t>(seed + i));
    return v;
}

bool bytesEqual(const std::vector<std::byte>& a, const std::vector<std::byte>& b)
{
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0;
}

} // namespace

// 矩阵 #1：SPSC 顺序写读一致
TEST_CASE("spsc sequential write-read preserves byte order", "[pipeline][spsc]")
{
    SpscRingBuffer ring(128);
    const auto in = pattern(100);
    REQUIRE(ring.write(in.data(), in.size()) == 100);

    auto batch = ring.beginBatch(128);
    REQUIRE(batch.ok);
    REQUIRE(batch.startPos == 0);
    REQUIRE(batch.size == 100);

    std::vector<std::byte> out(100);
    REQUIRE(ring.copyBatch(batch, out.data(), out.size()) == 100);
    REQUIRE(bytesEqual(in, out));

    ring.publish(batch);
    REQUIRE_FALSE(batch.ok);
    REQUIRE(ring.readPos() == 100);
    REQUIRE(ring.writePos() == 100);
    REQUIRE(ring.readInFlight() == 100); // clear：恢复空闲
    REQUIRE(ring.droppedBytes() == 0);
    REQUIRE(ring.rxDropped() == 0);
}

// 矩阵 #2：SPSC 回绕（写满一圈后覆盖最旧、序号取模寻址正确）
TEST_CASE("spsc wraparound on write and read", "[pipeline][spsc]")
{
    SpscRingBuffer ring(16);
    const auto a = pattern(16, 0x10); // A 填满一圈
    REQUIRE(ring.write(a.data(), a.size()) == 16);

    const auto b = pattern(4, 0x80); // 满载 → 拿 owner 跳最旧 4 字节后写入（物理回绕）
    REQUIRE(ring.write(b.data(), b.size()) == 4);
    REQUIRE(ring.droppedBytes() == 4);
    REQUIRE(ring.readPos() == 4);
    REQUIRE(ring.writePos() == 20);

    auto batch = ring.beginBatch(16);
    REQUIRE(batch.ok);
    REQUIRE(batch.startPos == 4);
    REQUIRE(batch.size == 16); // 序号 [4, 20)：物理 [4..16) + 回绕 [0..4)

    std::vector<std::byte> out(16);
    REQUIRE(ring.copyBatch(batch, out.data(), out.size()) == 16);
    std::vector<std::byte> expected;
    expected.insert(expected.end(), a.begin() + 4, a.end()); // A 未读部分
    expected.insert(expected.end(), b.begin(), b.end());     // B 跨回绕
    REQUIRE(bytesEqual(expected, out));
    ring.publish(batch);
}

// 矩阵 #3：满载丢最旧（读侧空闲，owner CAS 成功分支）
TEST_CASE("spsc full idle reader skips oldest into droppedBytes", "[pipeline][spsc]")
{
    SpscRingBuffer ring(16);
    const auto a = pattern(16);
    REQUIRE(ring.write(a.data(), a.size()) == 16);

    const auto b = pattern(4, 0x90);
    REQUIRE(ring.write(b.data(), b.size()) == 4); // 满载 + 读侧空闲 → 丢最旧
    REQUIRE(ring.droppedBytes() == 4);
    REQUIRE(ring.rxDropped() == 0);
    REQUIRE(ring.ownerState() == SpscRingBuffer::Owner::Idle);
    REQUIRE(ring.readInFlight() == ring.readPos()); // 空闲不变量

    auto batch = ring.beginBatch(16);
    std::vector<std::byte> out(16);
    REQUIRE(ring.copyBatch(batch, out.data(), out.size()) == 16);
    REQUIRE(out[0] == a[4]); // 最老 4 字节已被跳过
    ring.publish(batch);
}

// 矩阵 #4：R-59 TOCTOU（第 11/12 轮验收）
// 交错：写侧判定"满载需覆盖"（t0）→ 读侧 beginBatch（owner 0→1，ceiling 发布）→
// 写侧覆盖决策 CAS owner 0→2 失败 → 丢新计 rxDropped，在飞区间 [R, R+k) 完整。
TEST_CASE("spsc writer overwrite decision loses race to reader beginBatch (R-59 TOCTOU)",
    "[pipeline][spsc][r59]")
{
    SpscRingBuffer ring(16);
    const auto a = pattern(16);
    REQUIRE(ring.write(a.data(), a.size()) == 16); // 满载，全未读

    // t1：读侧 beginBatch——owner 0→1，ceiling 发布
    auto batch = ring.beginBatch(8);
    REQUIRE(batch.ok);
    REQUIRE(ring.readInFlight() == 8);
    REQUIRE(ring.ownerState() == SpscRingBuffer::Owner::ReaderCopy);

    // t2/t3：写侧覆盖决策 CAS 0→2 失败 → 走丢新分支
    const auto b = pattern(4, 0xA0);
    REQUIRE(ring.write(b.data(), b.size()) == 0);
    REQUIRE(ring.rxDropped() == 4);
    REQUIRE(ring.droppedBytes() == 0);
    REQUIRE(ring.readPos() == 0);
    REQUIRE(ring.readInFlight() == 8);
    REQUIRE(ring.ownerState() == SpscRingBuffer::Owner::ReaderCopy);

    // 在飞区间 [R, R+k) 数据完整未被触碰
    std::vector<std::byte> out(8);
    REQUIRE(ring.copyBatch(batch, out.data(), out.size()) == 8);
    REQUIRE(bytesEqual(std::vector<std::byte>(a.begin(), a.begin() + 8), out));

    // publish 后恢复空闲不变量（Batch move-only：直接发布原对象，R-71）
    ring.publish(batch);
    REQUIRE(ring.readPos() == 8);
    REQUIRE(ring.readInFlight() == 8);
    REQUIRE(ring.ownerState() == SpscRingBuffer::Owner::Idle);
}

// 矩阵 #5：in-flight 清位——publish + clear 后恢复"丢最旧"分支
TEST_CASE("spsc oldest-drop branch restored after publish", "[pipeline][spsc]")
{
    SpscRingBuffer ring(16);
    const auto a = pattern(16);
    REQUIRE(ring.write(a.data(), a.size()) == 16); // 满
    auto batch = ring.beginBatch(4);
    REQUIRE(batch.ok);
    ring.publish(batch); // readPos=4，腾出 4

    const auto b = pattern(4, 0xB0);
    REQUIRE(ring.write(b.data(), b.size()) == 4); // 空间恰好 4 → 整块直写

    // 再造满载：此时未读 16，写 8 → 空闲分支丢最旧 8
    const auto c = pattern(8, 0xC0);
    REQUIRE(ring.write(c.data(), c.size()) == 8);
    REQUIRE(ring.droppedBytes() == 8);
    REQUIRE(ring.rxDropped() == 0);

    // 全量校验：skip 8 后未读 = 16 字节，序号 [12, 28) = 旧 A 的 [12,16) + b + c
    auto batch2 = ring.beginBatch(20);
    REQUIRE(batch2.ok);
    REQUIRE(batch2.size == 16);
    std::vector<std::byte> out(16);
    REQUIRE(ring.copyBatch(batch2, out.data(), out.size()) == 16);
    std::vector<std::byte> expected;
    expected.insert(expected.end(), std::next(a.begin(), 12), a.end());
    expected.insert(expected.end(), b.begin(), b.end());
    expected.insert(expected.end(), c.begin(), c.end());
    REQUIRE(bytesEqual(expected, out));
    ring.publish(batch2);
}

// 矩阵 #12：readPos 单调性（R-64）
// ① owner==1 拷贝中注入写侧推进尝试 → 被 owner 拒绝（丢新分支），readPos 不动；
// ② 纵深防御：外部把 readPos 推到 R+d（d>k）后 publish（CAS-max）→ readPos 不回退。
TEST_CASE("spsc readPos monotonicity under injected writer skip (R-64)", "[pipeline][spsc][r64]")
{
    SECTION("writer skip attempt rejected while reader holds owner")
    {
        SpscRingBuffer ring(32);
        REQUIRE(ring.write(pattern(32).data(), 32) == 32); // 满
        auto batch = ring.beginBatch(5);
        REQUIRE(batch.ok);
        REQUIRE(ring.readPos() == 0);

        const auto b = pattern(8, 0xE0); // 需要腾空间 → owner 决策
        REQUIRE(ring.write(b.data(), b.size()) == 0);
        REQUIRE(ring.rxDropped() == 8);
        REQUIRE(ring.readPos() == 0);   // 未被写侧推进
        REQUIRE(ring.droppedBytes() == 0);

        ring.publish(batch);
        REQUIRE(ring.readPos() == 5);
    }

    SECTION("publish uses CAS-max and never regresses readPos")
    {
        SpscRingBuffer ring(32);
        REQUIRE(ring.write(pattern(20).data(), 20) == 20);
        auto batch = ring.beginBatch(5); // R=0, k=5
        REQUIRE(batch.ok);

        // 纵深防御验证：模拟"写侧 skip 已推进 readPos 到 R+8（d>k）"的假想交错
        ring.testInjectReadPosAdvance(8);
        REQUIRE(ring.readPos() == 8);

        ring.publish(batch); // 普通 store 会把它写回 5（R-64 缺陷），CAS-max 保持 8
        REQUIRE(ring.readPos() == 8);
        REQUIRE(ring.readInFlight() == 8);
        REQUIRE(ring.ownerState() == SpscRingBuffer::Owner::Idle);
    }
}

// 矩阵 #14：块尾边界值（R-65.2）——写入长度恰好使块尾别名 == readPos（贴住整块校验边界）
TEST_CASE("spsc block-end boundary exactly touches readPos (R-65.2)", "[pipeline][spsc][r65]")
{
    SpscRingBuffer ring(16);
    const auto a = pattern(12);
    REQUIRE(ring.write(a.data(), a.size()) == 12); // 未读 12，空间 4

    const auto b = pattern(4, 0xF0);
    REQUIRE(ring.write(b.data(), b.size()) == 4); // 块尾别名 = 12+4−16 = 0 == readPos → 整块直写
    REQUIRE(ring.droppedBytes() == 0);
    REQUIRE(ring.rxDropped() == 0);
    REQUIRE(ring.writePos() == 16);
    REQUIRE(ring.readPos() == 0);

    auto batch = ring.beginBatch(16);
    std::vector<std::byte> out(16);
    REQUIRE(ring.copyBatch(batch, out.data(), out.size()) == 16);
    std::vector<std::byte> expected;
    expected.insert(expected.end(), a.begin(), a.end());
    expected.insert(expected.end(), b.begin(), b.end());
    REQUIRE(bytesEqual(expected, out));
    ring.publish(batch);

    // 越过边界：先填满（整块直写 16），再写 1 字节 → 满载、空闲分支丢最旧 1
    REQUIRE(ring.write(pattern(16, 0x20).data(), 16) == 16);
    const std::byte one{0x7F};
    REQUIRE(ring.write(&one, 1) == 1);
    REQUIRE(ring.droppedBytes() == 1);
}

// 边界与防御分支：cancelBatch、len>capacity、len=0、空缓冲读、dst 容量不足
TEST_CASE("spsc edge cases: cancel, oversized write, empty read, dst cap", "[pipeline][spsc]")
{
    SpscRingBuffer ring(16);

    SECTION("cancelBatch releases owner without advancing readPos")
    {
        REQUIRE(ring.write(pattern(8).data(), 8) == 8);
        auto batch = ring.beginBatch(4);
        REQUIRE(batch.ok);
        ring.cancelBatch(batch);
        REQUIRE_FALSE(batch.ok);
        REQUIRE(ring.ownerState() == SpscRingBuffer::Owner::Idle);
        REQUIRE(ring.readPos() == 0);
        REQUIRE(ring.readInFlight() == 0); // 恢复空闲不变量
    }

    SECTION("len over capacity is dropped as rx_dropped")
    {
        const auto big = pattern(32);
        REQUIRE(ring.write(big.data(), big.size()) == 0); // 32 > 16
        REQUIRE(ring.rxDropped() == 32);
        REQUIRE(ring.writePos() == 0);
    }

    SECTION("zero-length write is a no-op")
    {
        const std::byte b{0x01};
        REQUIRE(ring.write(&b, 0) == 0);
        REQUIRE(ring.rxDropped() == 0);
        REQUIRE(ring.writePos() == 0);
    }

    SECTION("beginBatch on empty buffer yields no batch and releases owner")
    {
        auto batch = ring.beginBatch(4);
        REQUIRE_FALSE(batch.ok);
        REQUIRE(ring.ownerState() == SpscRingBuffer::Owner::Idle);
        REQUIRE(ring.readInFlight() == ring.readPos());
    }

    SECTION("copyBatch rejects invalid batch or undersized dst")
    {
        REQUIRE(ring.write(pattern(8).data(), 8) == 8);
        auto batch = ring.beginBatch(8);
        REQUIRE(batch.ok);

        SpscRingBuffer::Batch bad; // ok=false（move-only 下默认构造仍可用）
        std::vector<std::byte> out(16);
        REQUIRE(ring.copyBatch(bad, out.data(), out.size()) == 0);
        REQUIRE(ring.copyBatch(batch, out.data(), 4) == 0); // dst 容量不足

        ring.publish(batch); // Batch move-only（R-71）：原对象直接发布
        REQUIRE_FALSE(batch.ok); // publish 将批次置无效，防止重复发布/重复拷贝
    }
}

// 矩阵 #9：有界压力——1 写 1 读真线程，固定总量，终态守恒：
// written == read + droppedBytes（rxDropped 为从未入缓冲的丢弃，独立核对）。
// 只断言终态与序号单调，不断言中间时序——任何调度下结论一致（§4 反 flaky）。
TEST_CASE("spsc bounded pressure preserves conservation invariants", "[pipeline][spsc][pressure]")
{
    constexpr std::size_t kTotal = 1'000'000;
    constexpr std::size_t kCap = 1024;
    SpscRingBuffer ring(kCap);

    std::atomic<std::size_t> written{0};
    std::atomic<bool> writerDone{false};

    std::thread writer([&] {
        std::vector<std::byte> chunk(37); // 非整除块长，制造各种边界
        for (std::size_t i = 0; i < kTotal; i += chunk.size()) {
            const auto n = std::min(chunk.size(), kTotal - i);
            for (std::size_t j = 0; j < n; ++j)
                chunk[j] = static_cast<std::byte>(static_cast<std::uint8_t>((i + j) & 0xFF));
            written.fetch_add(ring.write(chunk.data(), n), std::memory_order_relaxed);
        }
        writerDone.store(true, std::memory_order_release);
    });

    std::size_t read = 0;
    std::uint64_t lastReadPos = 0;
    std::vector<std::byte> out(kCap);
    for (;;) {
        auto batch = ring.beginBatch(kCap);
        if (batch.ok) {
            REQUIRE(batch.startPos >= lastReadPos); // readPos 单调
            lastReadPos = batch.startPos;
            read += ring.copyBatch(batch, out.data(), out.size());
            ring.publish(batch);
        }
        if (writerDone.load(std::memory_order_acquire)
            && ring.readPos() == ring.writePos()) // 排干
            break;
    }
    writer.join();

    const auto dropped = ring.droppedBytes();
    const auto rxDrop = ring.rxDropped();
    REQUIRE(written.load() == read + dropped);      // 终态守恒
    REQUIRE(written.load() + rxDrop == kTotal);     // 总量恒等
    REQUIRE(ring.readPos() == ring.writePos());
    REQUIRE(ring.readInFlight() == ring.readPos()); // 空闲不变量
}
