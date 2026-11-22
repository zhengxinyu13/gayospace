#include "ChannelStore.h"

#include <algorithm>

namespace gayospace::pipeline {

ChannelStore::ChannelStore(std::size_t chCount, std::uint64_t capacityFrames)
    : chCount_(chCount == 0 ? std::size_t{1} : chCount)
    , capacityFrames_(capacityFrames == 0 ? std::uint64_t{1} : capacityFrames)
    , buffer_(std::make_unique<float[]>(chCount_ * static_cast<std::size_t>(capacityFrames_)))
{
}

void ChannelStore::writeFrame(const float* values)
{
    const std::uint64_t idx = frameIndex_.load(std::memory_order_relaxed); // 单写者
    float* slot = frameSlot(idx);
    for (std::size_t c = 0; c < chCount_; ++c)
        std::atomic_ref<float>(slot[c]).store(values[c], std::memory_order_relaxed);
    frameIndex_.store(idx + 1, std::memory_order_release); // 全部通道写完发布同一 frameIndex
}

ChannelStore::Window ChannelStore::readWindow(std::uint64_t windowFrames)
{
    // 初始 + 有界重试 2 次（TS-SW §3.3 语义），仍失败跳过本渲染周期
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::uint64_t idx0 = snapshotIndexLoad();
        const std::uint64_t count = std::min({windowFrames, capacityFrames_, idx0});
        const std::uint64_t start = idx0 - count; // idx0 < 窗长 → 起点 clamp 到 0（R-60）
        Window w;
        w.start = start;
        w.count = count;
        w.samples.resize(static_cast<std::size_t>(count) * chCount_);
        copyFrames(start, count, w.samples);
        if (recheckIndex(idx0)) {
            w.ok = true;
            return w;
        }
    }
    return {};
}

std::uint64_t ChannelStore::snapshotIndexLoad() const
{
    return frameIndex_.load(std::memory_order_acquire);
}

void ChannelStore::copyFrames(std::uint64_t start, std::uint64_t count, std::vector<float>& out)
{
    // R-68：边界检查——out 不足 count × chCount 时安全返回（公共 API 不留堆损坏 footgun）
    if (out.size() < static_cast<std::size_t>(count) * chCount_)
        return;
    for (std::uint64_t f = 0; f < count; ++f) {
        float* slot = frameSlot(start + f); // 环形取模寻址
        const std::size_t base = static_cast<std::size_t>(f) * chCount_;
        for (std::size_t c = 0; c < chCount_; ++c)
            out[base + c] = std::atomic_ref<float>(slot[c]).load(std::memory_order_relaxed);
    }
}

bool ChannelStore::recheckIndex(std::uint64_t idx0) const
{
    // R-61（第 11 轮修正）：标准 seqlock 读侧——窗口读与复测之间插 acquire fence，
    // 禁止复测读被提前到窗口读之前（acquire 约束其后的操作不得上移；单独末端
    // load-acquire 不能阻止其前的 relaxed 窗口读被后移）。
    // 矩阵 #13 评审检查点：真线程压测无法验证指令顺序（DoD 2.3），本 fence 勿删。
    std::atomic_thread_fence(std::memory_order_acquire);
#ifdef GAYOSPACE_TESTING
    if (forceRecheckFailure_) // R-70：测试注入——确定性驱动降级路径
        return false;
#endif
    return frameIndex_.load(std::memory_order_acquire) == idx0;
}

} // namespace gayospace::pipeline
