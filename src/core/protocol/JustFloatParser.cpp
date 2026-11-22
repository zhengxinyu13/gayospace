// JustFloat 解析器实现。状态机语义与取舍见头文件注释与 tests/unit/test_justfloat_parser.cpp 的
// 各用例注释（评审对账用）。
#include "protocol/JustFloatParser.h"

#include <algorithm>
#include <bit>

namespace gayospace::protocol {

namespace {

// "本轮扫描尚无负载不全候选"的哨兵（仅 scan() 内部使用）
constexpr std::size_t kNoIncomplete = static_cast<std::size_t>(-1);

// 帧尾：00 00 80 7F（小端 +Inf），无帧头（TS §4.1）
constexpr std::array<std::byte, 4> kTail{
    std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x7F}};

// 4 字节小端 → float（x86/x64 与 CI core-portability 目标均为小端；大端平台 v1.0 不支持）
float leFloat(const std::byte* p)
{
    const std::uint32_t u = static_cast<std::uint32_t>(std::to_integer<unsigned>(*p))
        | (static_cast<std::uint32_t>(std::to_integer<unsigned>(*(p + 1))) << 8)
        | (static_cast<std::uint32_t>(std::to_integer<unsigned>(*(p + 2))) << 16)
        | (static_cast<std::uint32_t>(std::to_integer<unsigned>(*(p + 3))) << 24);
    return std::bit_cast<float>(u);
}

} // namespace

JustFloatParser::JustFloatParser(std::uint8_t ch_count, std::uint64_t ambiguous_limit)
    : ch_count_(ch_count == 0 ? 1 : ch_count),          // 防御：0 通道无意义
      frame_len_(static_cast<std::size_t>(ch_count_) * 4 + kTailSize),
      capacity_(kMaxBufferFrames * frame_len_),
      ambiguous_limit_(ambiguous_limit == 0 ? kDefaultAmbiguousLimit : ambiguous_limit)
{
}

void JustFloatParser::feed(std::span<const std::byte> bytes)
{
    buf_.insert(buf_.end(), bytes.begin(), bytes.end());

    // 有界缓冲（评审 §2.2）：超限丢最旧。找不到帧尾时字节流会无限增长，垃圾流由上限兜底；
    // 正常残帧（帧尾未到）不触发——缓冲上限 ≥ 4 帧长，远大于单帧等待窗口。
    if (buf_.size() > capacity_) {
        const std::size_t drop = buf_.size() - capacity_;
        consume_front(drop, true);
        scan_off_ = scan_off_ > drop ? scan_off_ - drop : 0;
    }

    scan();
}

std::optional<ParseFrame> JustFloatParser::pop_frame()
{
    if (out_.empty()) {
        return std::nullopt;
    }
    ParseFrame f = std::move(out_.front());
    out_.pop_front();
    return f;
}

void JustFloatParser::reset()
{
    buf_.clear();
    scan_off_ = 0;
    consumed_abs_ = 0;
    last_tail_abs_.reset();
    ambiguous_streak_ = 0;
    out_.clear();
    stats_ = ParseStats{};
    next_frame_index_ = 0;
}

ParseStats JustFloatParser::stats() const
{
    return stats_;
}

void JustFloatParser::consume_front(std::size_t n, bool dropped)
{
    if (dropped) {
        stats_.bytes_dropped_sync += n;
    }
    consumed_abs_ += n;
    buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(n));
}

void JustFloatParser::scan()
{
    // 逐候选推进；确认后回卷缓冲（consume_front）从头重扫（粘帧场景下一轮循环产出后续帧）。
    std::size_t search_from = scan_off_;
    std::size_t first_incomplete = kNoIncomplete;
    for (;;) {
        // 在 [search_from, buf_] 内找下一个完整帧尾
        const auto it = std::search(buf_.begin() + static_cast<std::ptrdiff_t>(search_from),
            buf_.end(), kTail.begin(), kTail.end());
        if (it == buf_.end()) {
            if (first_incomplete != kNoIncomplete) {
                // 存在"负载不全"候选（帧头尚未到达/已被消费）：停在最早的候选处等数据
                scan_off_ = first_incomplete;
                return;
            }
            // 无候选：不消费任何字节（残帧的负载可能已在缓冲、帧尾未到）。
            // 扫描偏移退到"可能跨块的帧尾前缀"处，下次 feed 从这里继续；缓冲增长由上限兜底。
            scan_off_ = buf_.size() >= kTailSize - 1 ? buf_.size() - (kTailSize - 1) : 0;
            return;
        }

        const std::size_t tail_start = static_cast<std::size_t>(it - buf_.begin());
        const std::uint64_t tail_end_abs = consumed_abs_ + tail_start + kTailSize;
        const std::uint64_t frame_start_abs = tail_end_abs - frame_len_;

        if (frame_start_abs < consumed_abs_) {
            // 负载不全：该帧头不在缓冲内（流开头无前置数据 / 已随前面的确认被消费），
            // 永远无法重建 → 跳过该候选，继续找后面的候选（它们不受影响）。
            if (first_incomplete == kNoIncomplete) {
                first_incomplete = tail_start;
            }
            search_from = tail_start + 1;
            continue;
        }

        // 帧距对齐校验（TS §4.1 第 1 条）：与上一确认帧尾的间距须为帧长的整数倍。
        // 无锚点（首个完整候选）直接确认——无参照可校验（R-26：JustFloat 扫描必然"成一帧"）。
        const bool aligned = !last_tail_abs_.has_value()
            || ((tail_end_abs - *last_tail_abs_) % frame_len_ == 0);

        if (aligned) {
            // 确认帧：负载 = [tail_end - frame_len, tail_end - kTailSize)
            ParseFrame f;
            f.t = std::chrono::steady_clock::now();
            f.frame_index = next_frame_index_++;
            const std::size_t payload_start = tail_start + kTailSize - frame_len_;
            for (std::size_t c = 0; c < ch_count_; ++c) {
                f.ch[c] = leFloat(buf_.data() + payload_start + c * 4);
            }
            f.ch_count = ch_count_;
            out_.push_back(std::move(f));
            ++stats_.frames_ok;
            last_tail_abs_ = tail_end_abs;
            ambiguous_streak_ = 0;

            // 消费到帧尾结束
            consume_front(tail_start + kTailSize, false);
            scan_off_ = 0;
            search_from = 0;
            first_incomplete = kNoIncomplete;
            continue;
        }

        // 歧义（帧距非整倍数，TS §4.1 第 2 条）："最老匹配优先丢弃"——本候选是最早命中的
        // 完整匹配，视为假帧尾（负载 +Inf / 流错位），跳过其首字节继续扫描；不消费数据
        // 字节，后续真帧尾的负载保持完整。配错通道数时每个真帧尾都会在此计数，frames_bad
        // 持续增长（TS §4.0：配置错误的表现形式）。
        ++stats_.frames_bad;
        ++ambiguous_streak_;
        search_from = tail_start + 1;

        // 降级重扫（TS §4.1 第 3 条）：连续歧义超阈值 → 锚点重置重扫。
        // 工程取舍：TS 的"半字节步进重扫"实现为"重新锚定"——清空锚点，下一个命中的帧尾
        // 作为新锚重新做对齐校验；效果等价于按 float 宽度相位重试对齐（4 个可能相位由后续
        // 帧距校验自然裁决），实现简单且可测。若评审裁决需严格逐相位重扫，见 PR 描述。
        if (ambiguous_streak_ >= ambiguous_limit_) {
            last_tail_abs_.reset();
            ambiguous_streak_ = 0;
        }
    }
}

} // namespace gayospace::protocol
