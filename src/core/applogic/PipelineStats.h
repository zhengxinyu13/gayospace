// 管道 stats 汇总（只读快照；TS-SW §6 CLI stats 的数据来源）。
// R-85（第 22 轮评审）：非"原子读"——parser/store 计数为缓存式原子快照，由 pump 线程
// 在 pumpOnce/switchParser 结尾刷新；stats() 跨线程只读原子，不解引用 parser_/store_。
// 弱一致口径（各计数单调递增，快照间允许微小偏差）。
// R-86：rxBytes 口径 = ring.writePos（被接纳入 ring 的字节）；串口收到的总接收量
// = rxBytes + rxDropped（TS §6 已写明组合规则，CLI/GUI 不得各自解释）。
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
    std::uint32_t storeRebuilds = 0; // ch_count 变化重建 store 的次数（R-92：含构造时初始构建，故 ≥1）
};

} // namespace gayospace::applogic
