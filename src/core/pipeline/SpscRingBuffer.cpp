#include "SpscRingBuffer.h"

#include <algorithm>
#include <cstring>
#include <thread>

namespace gayospace::pipeline {
namespace {
// 读侧有界自旋预算（R-65.3）：写侧仅在覆盖决策瞬间持有 owner，让步重试后仍失败则跳过本批次
constexpr int kReaderMaxSpins = 64;
}

SpscRingBuffer::SpscRingBuffer(std::size_t capacityBytes)
    : capacity_(capacityBytes == 0 ? std::size_t{1} : capacityBytes)
    , buffer_(std::make_unique<std::byte[]>(capacity_))
{
}

std::size_t SpscRingBuffer::write(const std::byte* data, std::size_t len)
{
    if (len == 0)
        return 0;
    if (len > capacity_) { // 永远无法整块容纳
        rxDropped_.fetch_add(len, std::memory_order_relaxed);
        return 0;
    }

    const std::uint64_t w = writePos_.load(std::memory_order_relaxed); // 写侧独占
    const std::uint64_t r = readPos_.load(std::memory_order_acquire);
    const std::size_t space = capacity_ - static_cast<std::size_t>(w - r);

    // R-65.2 整块校验：writePos + len − capacity ≤ readPos ⟺ space ≥ len（块尾别名一并覆盖）
    if (space >= len) {
        writeAt(w, data, len);
        writePos_.store(w + len, std::memory_order_release); // 读侧 acquire 读后可见
        return len;
    }

    // 满载：owner CAS 0→2（R-59：覆盖决策与读侧 begin+copy 串行化）
    auto expected = static_cast<std::uint32_t>(Owner::Idle);
    if (!owner_.compare_exchange_strong(expected, static_cast<std::uint32_t>(Owner::WriterSkip),
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        // 读侧拷贝中：丢弃新到字节块，不阻塞、不触碰 [readPos, readInFlight)
        rxDropped_.fetch_add(len, std::memory_order_relaxed);
        return 0;
    }

    // 成功：此刻读侧必不在飞（readInFlight == readPos 由临界区保证）
    const std::uint64_t rHeld = readPos_.load(std::memory_order_relaxed);
    const std::size_t spaceHeld = capacity_ - static_cast<std::size_t>(w - rHeld);
    const std::size_t skip = (spaceHeld >= len) ? std::size_t{0} : len - spaceHeld;
    if (skip > 0) {
        advanceReadPosCasMax(rHeld + skip); // R-64：CAS-max 推进
        droppedBytes_.fetch_add(skip, std::memory_order_relaxed);
    }
    readInFlight_.store(rHeld + skip, std::memory_order_release); // 恢复不变量 readPos ≤ readInFlight
    owner_.store(static_cast<std::uint32_t>(Owner::Idle), std::memory_order_release);

    // 释放后再写：读侧此后 beginBatch 得到的 k ≤ writePos − readPos，与 [w, w+len) 序号不相交
    writeAt(w, data, len);
    writePos_.store(w + len, std::memory_order_release);
    return len;
}

SpscRingBuffer::Batch SpscRingBuffer::beginBatch(std::size_t kMax)
{
    for (int spin = 0; spin < kReaderMaxSpins; ++spin) {
        auto expected = static_cast<std::uint32_t>(Owner::Idle);
        if (owner_.compare_exchange_strong(expected, static_cast<std::uint32_t>(Owner::ReaderCopy),
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            const std::uint64_t r = readPos_.load(std::memory_order_acquire);
            const std::uint64_t w = writePos_.load(std::memory_order_acquire); // R-65.1 acquire
            const std::uint64_t k = std::min<std::uint64_t>(kMax, w - r);      // 不拷贝未发布区间
            if (k == 0) {
                owner_.store(static_cast<std::uint32_t>(Owner::Idle), std::memory_order_release);
                return {};
            }
            readInFlight_.store(r + k, std::memory_order_release); // ceiling 发布（临界区内）
            Batch batch; // move-only（R-71），不可聚合初始化
            batch.startPos = r;
            batch.size = static_cast<std::size_t>(k);
            batch.ok = true;
            return batch;
        }
        std::this_thread::yield(); // 写侧覆盖决策中：让步后重试
    }
    return {}; // 自旋预算耗尽：跳过本批次（设计内降级）
}

std::size_t SpscRingBuffer::copyBatch(const Batch& batch, std::byte* dst, std::size_t dstCap) const
{
    if (!batch.ok || batch.size > dstCap)
        return 0;
    const std::size_t off = static_cast<std::size_t>(batch.startPos % capacity_);
    const std::size_t first = std::min<std::size_t>(batch.size, capacity_ - off);
    std::memcpy(dst, buffer_.get() + off, first);
    if (batch.size > first)
        std::memcpy(dst + first, buffer_.get(), batch.size - first);
    return batch.size;
}

void SpscRingBuffer::publish(Batch& batch)
{
    if (!batch.ok)
        return;
    advanceReadPosCasMax(batch.startPos + batch.size); // R-64：临界区内无竞争，CAS-max 纵深防御
    batch.ok = false;
    readInFlight_.store(readPos_.load(std::memory_order_acquire), std::memory_order_release);
    owner_.store(static_cast<std::uint32_t>(Owner::Idle), std::memory_order_release);
}

void SpscRingBuffer::cancelBatch(Batch& batch)
{
    if (!batch.ok)
        return;
    batch.ok = false;
    readInFlight_.store(readPos_.load(std::memory_order_acquire), std::memory_order_release);
    owner_.store(static_cast<std::uint32_t>(Owner::Idle), std::memory_order_release);
}

// 非并发安全方法（见头文件注释）：仅在切换时序下（写读两侧均暂停）调用。
// R 归 W 并把未读字节计入 droppedBytes，使后续 parse stats 的 dropped 口径连续。
void SpscRingBuffer::clear()
{
    const auto w = writePos_.load(std::memory_order_relaxed);
    const auto r = readPos_.load(std::memory_order_relaxed);
    droppedBytes_.fetch_add(w - r, std::memory_order_relaxed);
    readInFlight_.store(w, std::memory_order_relaxed);
    readPos_.store(w, std::memory_order_relaxed); // 单线程时刻，relaxed 足够
}

void SpscRingBuffer::advanceReadPosCasMax(std::uint64_t target)
{
    std::uint64_t cur = readPos_.load(std::memory_order_acquire);
    while (target > cur) {
        if (readPos_.compare_exchange_weak(cur, target,
                std::memory_order_acq_rel, std::memory_order_acquire))
            return;
        // 失败时 cur 已更新为最新值；对方推进已超过 target 则退出（单调不减）
    }
}

#ifdef GAYOSPACE_TESTING
void SpscRingBuffer::testInjectReadPosAdvance(std::uint64_t target)
{
    advanceReadPosCasMax(target);
}
#endif

void SpscRingBuffer::writeAt(std::uint64_t idx, const std::byte* src, std::size_t len)
{
    const std::size_t off = static_cast<std::size_t>(idx % capacity_);
    const std::size_t first = std::min<std::size_t>(len, capacity_ - off);
    std::memcpy(buffer_.get() + off, src, first);
    if (len > first)
        std::memcpy(buffer_.get(), src + first, len - first);
}

} // namespace gayospace::pipeline
