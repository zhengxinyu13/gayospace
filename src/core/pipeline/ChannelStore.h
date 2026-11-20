// ChannelStore：frame-major 环形帧数组 + 单一 frameIndex（R-33 方案② / R-62 帧级单索引；TS-SW §3.3）。
// 并发纪律（第 10~12 轮评审定稿）：
//  - 每帧 ch_count 个 float 连续存放，全部通道写完发布同一个 frameIndex（release）——
//    跨通道同帧对齐由布局与单一索引保证（R-62）；
//  - 样本读写经 std::atomic_ref<float>（relaxed）——单写多读、无 UB、单份存储（内存上界 76.8 MB 不变）；
//  - 容量 = 存储时长 × fs 帧；显示窗口 ≤ 容量（R-60）；启动未满窗 clamp 到 0；
//  - 读侧 seqlock 快照：acquire 读 frameIndex → 窗口拷贝（relaxed）→ atomic_thread_fence(acquire) → 复测
//    （R-61 第 11 轮修正：acquire 约束其后的操作不得上移，单独末端 load-acquire 不能阻止其前的窗口读
//    被后移；标准 seqlock 读侧 = 窗口读 → acquire fence → 复测）；复测不变则快照有效，
//    变化则有界重试 2 次，仍失败跳过本渲染周期（设计内降级）。
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace gayospace::pipeline {

class ChannelStore {
public:
    struct Window {
        bool ok = false;            // false = 复测连续失败超限，跳过本渲染周期
        std::uint64_t start = 0;    // 窗口起始帧序号（启动未满窗 clamp 到 0）
        std::uint64_t count = 0;    // 实际帧数（≤ 请求窗长，≤ 已发布帧数）
        std::vector<float> samples; // frame-major：samples[i * chCount + c]
    };

    ChannelStore(std::size_t chCount, std::uint64_t capacityFrames);

    [[nodiscard]] std::size_t channelCount() const noexcept { return chCount_; }
    [[nodiscard]] std::uint64_t capacityFrames() const noexcept { return capacityFrames_; }
    [[nodiscard]] std::uint64_t frameIndex() const noexcept { return frameIndex_.load(std::memory_order_acquire); }

    // ---- 写侧（解析线程，单写者）----
    // 写入一帧全部通道样本（ch_count 个 float），随后发布同一 frameIndex
    void writeFrame(const float* values);

    // ---- 读侧快照（seqlock；带界重试，降级语义见 TS-SW §3.3）----
    [[nodiscard]] Window readWindow(std::uint64_t windowFrames);

    // ---- 分步 API（测试/诊断注入用；readWindow 内部即按此序列实现）----
    [[nodiscard]] std::uint64_t snapshotIndexLoad() const; // acquire 读 frameIndex
    void copyFrames(std::uint64_t start, std::uint64_t count, std::vector<float>& out); // relaxed 拷贝；out 须预置 count × chCount 个元素
    [[nodiscard]] bool recheckIndex(std::uint64_t idx0) const; // fence(acquire) → 复测 == idx0

private:
    [[nodiscard]] float* frameSlot(std::uint64_t frameIdx) noexcept
    {
        return buffer_.get() + (frameIdx % capacityFrames_) * chCount_;
    }

    [[nodiscard]] const float* frameSlot(std::uint64_t frameIdx) const noexcept
    {
        return buffer_.get() + (frameIdx % capacityFrames_) * chCount_;
    }

    std::size_t chCount_;
    std::uint64_t capacityFrames_;
    std::unique_ptr<float[]> buffer_;
    std::atomic<std::uint64_t> frameIndex_{0};
};

} // namespace gayospace::pipeline
