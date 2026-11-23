// 管道 stats 汇总（只读快照；TS-SW §6 CLI stats 的数据来源，第 21 轮评审 §2.1）。
// 各字段一次聚合自 ring / parser / store 的原子读——非同一瞬间的强一致快照
// （各计数单调递增，弱一致口径对 CLI stats 足够；TS §6 语义映射见 PipelinePump::stats）。
#pragma once

#include <cstddef>
#include <cstdint>

namespace gayospace::applogic {

struct PipelineStats {
    std::uint64_t rxBytes = 0;       // ring.writePos：累计写入环形缓冲的字节（CLI rx_bytes）
    std::uint64_t droppedBytes = 0;  // ring.droppedBytes：满载丢最旧（CLI dropped_bytes）
    std::uint64_t rxDropped = 0;     // ring.rxDropped：读侧在飞期间丢弃的新到字节（CLI rx_dropped）
    std::uint64_t framesOk = 0;      // parser stats.frames_ok
    std::uint64_t framesBad = 0;     // parser stats.frames_bad
    std::uint64_t parseErrors = 0;   // parser stats.parse_errors
    std::size_t chCount = 0;         // 当前 ChannelStore 通道数（CLI channels）
    std::uint64_t frames = 0;        // store.frameIndex：已发布帧数（含重建后从 0 计的部分）
    std::uint32_t storeRebuilds = 0; // ch_count 变化重建 store 的次数（状态栏提示用）
};

} // namespace gayospace::applogic
