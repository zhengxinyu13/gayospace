// JustFloat 解析器（TS-SW §4.1；出处 https://www.vofa.plus/docs/learning/dataengines/justfloat/ ，核对日 2026-11-16）。
// 帧：N×4 字节小端 float 负载 + 帧尾 00 00 80 7F（小端 +Inf），无帧头；N 由构造参数给出（默认 4）。
// 重同步状态机（TS §4.1，R-26 修订版）：帧尾命中 → 帧距对齐校验 → 歧义按最老匹配优先丢弃 →
// 连续歧义超阈值触发降级重扫（见 .cpp 内注释的工程实现取舍）。
// 有界缓冲（第 17 轮评审 §2.2）：找不到帧尾时字节流会无限增长——缓冲上限 4×帧长，超限丢最旧并计数。
#pragma once

#include <deque>
#include <optional>
#include <vector>

#include "protocol/IProtocolParser.h"

namespace gayospace::protocol {

class JustFloatParser final : public IProtocolParser {
public:
    static constexpr std::size_t kTailSize = 4;                       // 00 00 80 7F
    static constexpr std::uint64_t kDefaultAmbiguousLimit = 8;        // 连续歧义阈值（TS §4.1 默认 8）
    static constexpr std::size_t kMaxBufferFrames = 4;                // 缓冲上限（帧数）——评审 §2.2

    // ch_count：字节流不含通道数，由用户配置（CLI --channels / GUI 配置项，默认 4，TS §4.0）。
    // R-76：ch_count == 0 或 > kMaxChannels 时 throw std::invalid_argument（不静默钳制）。
    explicit JustFloatParser(std::uint8_t ch_count = 4,
                             std::uint64_t ambiguous_limit = kDefaultAmbiguousLimit);

    void feed(std::span<const std::byte> bytes) override;
    std::optional<ParseFrame> pop_frame() override;
    void reset() override;
    ParseStats stats() const override;

private:
    std::uint8_t ch_count_;
    std::size_t frame_len_;                  // ch_count×4 + 4
    std::size_t capacity_;                   // kMaxBufferFrames × frame_len
    std::uint64_t ambiguous_limit_;

    std::vector<std::byte> buf_;             // 跨 feed 的字节缓冲（帧长很小，erase 前端开销可忽略）
    std::size_t scan_off_ = 0;               // 相对 buf_ 前端的扫描偏移（避免整块重扫 O(n²)）
    std::uint64_t consumed_abs_ = 0;         // 累计消费字节数（绝对坐标基准，帧距校验用）
    std::optional<std::uint64_t> last_tail_abs_;   // 上一确认帧尾结束位置的绝对坐标（锚点）
    std::uint64_t ambiguous_streak_ = 0;     // 连续歧义计数

    std::deque<ParseFrame> out_;
    ParseStats stats_;
    std::uint64_t next_frame_index_ = 0;

    // 从 scan_off_ 起扫描帧尾并驱动状态机；确认/歧义后回卷缓冲再继续
    void scan();
    // 弹出缓冲前端 n 字节；dropped=true 时计入 bytes_dropped_sync
    void consume_front(std::size_t n, bool dropped);
};

} // namespace gayospace::protocol
