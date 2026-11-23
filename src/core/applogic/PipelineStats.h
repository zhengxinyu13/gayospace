// 管道 stats 汇总（只读快照；TS-SW §6 CLI stats 的数据来源）。
// R-85（第 22 轮评审）：非"原子读"——parser/store 计数为缓存式原子快照，由 pump 线程
// 在 pumpOnce/switchParser 结尾刷新；stats() 跨线程只读原子，不解引用 parser_/store_。
// 弱一致口径（各计数单调递增，快照间允许微小偏差）。
// R-86：rx_bytes 口径 = ring.writePos + rxDropped（串口收到的总字节）——TS §6 已写明
// 组合规则；本结构只给原始分量，组装点在 CLI（字段名 ringBytes 防止调用者少加一项）。
#pragma once

#include <cstddef>
#include <cstdint>

namespace gayospace::applogic {

struct PipelineStats {
    std::uint64_t ringBytes = 0;     // ring.writePos：接纳入 ring 的字节（R-94 改名防呆：
                                     // CLI rx_bytes = ringBytes + rxDropped，见 TS §6）
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
