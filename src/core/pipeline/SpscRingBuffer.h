// SPSC 环形字节缓冲（契约唯一来源：docs/ts-pipeline-concurrency.md §2；TS-SW §3.2）。
// 并发纪律（第 10~12 轮评审定稿）：
//  - owner CAS（0 空闲 / 1 读侧拷贝 / 2 写侧覆盖决策，acq_rel）串行化"写侧覆盖决策"与
//    "读侧 beginBatch+拷贝"（R-59：两个独立原子挡不住 Dekker 式先读后写窗口，缓冲满时
//    写侧下一笔写的物理槽位恰为读侧拷贝起点，必须在覆盖前串行化）；
//  - readPos 双方一律 CAS-max 推进（R-64：普通 store 会被写侧 CAS 挤回导致序号回退）；
//  - writePos 写侧独占写（release）/ 读侧 acquire 读（R-65.1：字节缓冲非原子，release/acquire
//    构成字节可见性同步）；readInFlight 为 ceiling 发布锚点与可观测状态；
//  - 写侧未满整块直写：writePos + len − capacity ≤ readPos（等价剩余空间 ≥ len，R-65.2——
//    仅校验起始槽对多字节块不够，块尾别名可能越过 readPos 落进拷贝区间）；
//  - 写侧无阻塞（拿不到 owner 即丢新计 rxDropped），读侧有界自旋（R-65.3：非严格 lock-free，
//    不作无锁性能承诺）。
// 丢弃计数两分支（R-34）：droppedBytes = 跳过的最老未读（丢旧）；rxDropped = 丢弃的新到（丢新）。
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace gayospace::pipeline {

class SpscRingBuffer {
public:
    // owner 状态机：单写者/单读者，CAS 获取
    enum class Owner : std::uint32_t { Idle = 0, ReaderCopy = 1, WriterSkip = 2 };

    struct Batch {
        std::uint64_t startPos = 0; // 批次起始序号 R
        std::size_t size = 0;       // 实际 k（≤ k_max，临界区内按已发布字节数确定）
        bool ok = false;            // false = 未获得批次（owner 争用失败或缓冲空，跳过本批次）

        // R-71：禁拷贝（只 move）——可拷贝时 publish 副本后原对象仍 ok=true，
        // 重复 publish 会静默二次推进 owner 临界区
        Batch() = default;
        Batch(Batch&&) = default;
        Batch& operator=(Batch&&) = default;
        Batch(const Batch&) = delete;
        Batch& operator=(const Batch&) = delete;
    };

    explicit SpscRingBuffer(std::size_t capacityBytes);

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    // ---- 写侧（单写者：串口/解析线程）----
    // 返回实际写入字节数：
    //  - 未满整块直写成功 == len；
    //  - 满载且 owner 被读侧持有（或 len > capacity 永远无法容纳）→ 0，rxDropped += len；
    //  - 满载且拿到 owner：跳过最老未读（droppedBytes += skip）后整块写入，返回 len。
    std::size_t write(const std::byte* data, std::size_t len);

    // ---- 读侧（单读者；API 为拷贝快照语义，R-34）----
    [[nodiscard]] Batch beginBatch(std::size_t kMax);
    // 拷贝批次快照到 dst（须在 publish/cancel 前调用；环形回绕自动分两段）
    [[nodiscard]] std::size_t copyBatch(const Batch& batch, std::byte* dst, std::size_t dstCap) const;
    void publish(Batch& batch);     // CAS-max readPos → R+k；readInFlight = readPos；owner → Idle
    void cancelBatch(Batch& batch); // 放弃批次：readInFlight = readPos；owner → Idle

    // 协议切换/重配时清空缓冲（TS-SW §4.4）：readPos 追平 writePos（读写序号对齐），
    // 未读字节计入 droppedBytes。**非并发安全方法**：调用前提 = 切换时序已保证写侧与
    // 读侧均暂停（上层先停串口写入与解析线程）——胶水层 PipelinePump::switchParser 使用。
    void clear();

    // ---- 可观测状态（不变量检查、测试与诊断）----
    [[nodiscard]] std::uint64_t readPos() const noexcept { return readPos_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t writePos() const noexcept { return writePos_.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t readInFlight() const noexcept { return readInFlight_.load(std::memory_order_acquire); }
    [[nodiscard]] Owner ownerState() const noexcept
    {
        return static_cast<Owner>(owner_.load(std::memory_order_acquire));
    }
    [[nodiscard]] std::uint64_t droppedBytes() const noexcept { return droppedBytes_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t rxDropped() const noexcept { return rxDropped_.load(std::memory_order_relaxed); }

    // 仅测试注入（R-64 纵深防御用例）：模拟"写侧 skip 已把 readPos 推过本批末序号"的假想交错。
    // 生产代码不得调用；publish() 的 CAS-max 保证即使发生该交错 readPos 也不回退（R-71：收进测试宏）。
#ifdef GAYOSPACE_TESTING
    void testInjectReadPosAdvance(std::uint64_t target);
#endif

private:
    void advanceReadPosCasMax(std::uint64_t target);
    void writeAt(std::uint64_t idx, const std::byte* src, std::size_t len);

    std::size_t capacity_;
    std::unique_ptr<std::byte[]> buffer_;
    std::atomic<std::uint32_t> owner_{static_cast<std::uint32_t>(Owner::Idle)};
    std::atomic<std::uint64_t> writePos_{0};
    std::atomic<std::uint64_t> readPos_{0};
    std::atomic<std::uint64_t> readInFlight_{0};
    std::atomic<std::uint64_t> droppedBytes_{0};
    std::atomic<std::uint64_t> rxDropped_{0};
};

} // namespace gayospace::pipeline
