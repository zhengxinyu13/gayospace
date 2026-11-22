// FireWater 解析器实现。判定语义与取舍见头文件注释与 tests/unit/test_firewater_parser.cpp 的
// 各用例注释（评审对账用）。
#include "protocol/FireWaterParser.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace gayospace::protocol {

namespace {

// strtof 语义即"宽容"（解析到不能解析为止）；这里取"能解析出至少一个数字"为成功。
bool parseFloatField(const std::string& s, float& out)
{
    if (s.empty()) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (end == s.c_str()) {
        return false;                        // 完全无法解析（如 "abc"）
    }
    // strtof 溢出返回 ±HUGE_VAL 且置 ERANGE：按 NaN + 错误计数处理（数据坏，不该冒充 ±Inf）
    if (errno == ERANGE && (v == HUGE_VALF || v == -HUGE_VALF)) {
        return false;
    }
    out = v;
    return true;
}

// 前缀判定（R-25 + R-37）：片段能被 strtof 完整解析为数字 → 数据；否则 → 前缀。
// "完整"= endptr 停在片段末尾且至少转换 1 字符（与宽容字段解析不同，判定必须完整）。
bool prefixIsNumber(const std::string& s)
{
    if (s.empty()) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    std::strtof(s.c_str(), &end);
    if (errno == ERANGE) {
        // 溢出也算"像一个数"（如 1e999）——判定只关心像不像数，取值由字段解析兜底
        return end != s.c_str();
    }
    return end != s.c_str() && *end == '\0';
}

std::string bytesToString(const std::vector<std::byte>& b, std::size_t begin, std::size_t end)
{
    std::string s;
    s.reserve(end - begin);
    for (std::size_t i = begin; i < end; ++i) {
        s.push_back(static_cast<char>(std::to_integer<unsigned char>(b[i])));
    }
    return s;
}

} // namespace

void FireWaterParser::feed(std::span<const std::byte> bytes)
{
    for (const std::byte b : bytes) {
        const char c = static_cast<char>(std::to_integer<unsigned char>(b));
        if (c == '\n') {
            // 行结束（\r\n 与 \n\r 的 \r 已在两侧被忽略）
            if (line_overflow_) {
                // 超长行到此为止：整行丢弃并计数（评审 §2.3 防垃圾流无限增长）
                ++stats_.frames_bad;
                line_.clear();
                line_overflow_ = false;
                continue;
            }
            handle_line(line_);
            line_.clear();
            continue;
        }
        if (c == '\r') {
            continue;                        // \r\n / \n\r 兼容：\r 一律忽略
        }
        if (line_overflow_) {
            continue;                        // 超长行剩余部分静默吞掉
        }
        if (line_.size() >= kMaxLineBytes) {
            line_overflow_ = true;           // 达上限即进入丢弃状态，直到 \n
            continue;
        }
        line_.push_back(b);
    }
}

std::optional<ParseFrame> FireWaterParser::pop_frame()
{
    if (out_.empty()) {
        return std::nullopt;
    }
    ParseFrame f = std::move(out_.front());
    out_.pop_front();
    return f;
}

void FireWaterParser::reset()
{
    line_.clear();
    line_overflow_ = false;
    base_ch_count_.reset();
    out_.clear();
    stats_ = ParseStats{};
    next_frame_index_ = 0;
}

ParseStats FireWaterParser::stats() const
{
    return stats_;
}

void FireWaterParser::handle_line(const std::vector<std::byte>& line)
{
    // 空行：静默忽略（不算错误；采集流中常见心跳/空转）
    if (line.empty()) {
        return;
    }

    // 前缀判定：找第一个 ':'（ASCII；FireWater 为文本协议，非 ASCII 字节只会落在
    // "无法 strtof 解析"的一侧，判定行为正确）
    std::size_t colon = std::string::npos;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (std::to_integer<unsigned char>(line[i]) == ':') {
            colon = i;
            break;
        }
    }

    std::size_t data_begin = 0;              // 数据部分起点（含）
    if (colon != std::string::npos) {
        const std::string prefix = bytesToString(line, 0, colon);
        if (prefix == "image") {
            // 官方约定：any 不得为 image，整行丢弃并计数（TS §4.2）
            ++stats_.frames_bad;
            return;
        }
        if (!prefix.empty() && !prefixIsNumber(prefix)) {
            // ch1: / pid1: / v2: / #1: / channels: …… 合法通道名 → 前缀剔除（R-25/R-37）
            data_begin = colon + 1;
        }
        // 数字前缀（1.0:2.0,3.0）与空前缀（:1,2,3）：整行/冒号后进宽容解析
        // ——空前缀数据显然从冒号后开始
        if (prefix.empty()) {
            data_begin = colon + 1;
        }
    }
    // 无冒号：整行按数据宽容解析（TS §4.2）

    // R-77：数据部分为空（如 "ch1:" / ":"）——无字段可解析，忽略整行不计错，
    // 避免空字段产出 NaN 帧
    if (data_begin >= line.size()) {
        return;
    }

    // R-82（第 19 轮评审）：先判字段数上界再解析——已丢弃行（>kMaxChannels 字段）的坏字段
    // 不参与 parse_errors 计数；通道数基准永远 ≤ 32。
    std::size_t field_count = 1;
    for (std::size_t i = data_begin; i < line.size(); ++i) {
        if (std::to_integer<unsigned char>(line[i]) == ',') {
            ++field_count;
        }
    }
    if (field_count > kMaxChannels) {
        ++stats_.frames_bad;
        return;
    }

    // 逗号切分 → 逐字段 strtof 宽容解析；失败字段置 NaN 并 parse_errors++，不丢整行（TS §4.2）
    ParseFrame f;
    f.t = std::chrono::steady_clock::now();
    f.frame_index = next_frame_index_;
    std::size_t n = 0;                       // R-76：改 size_t——uint8_t 会在 >256 字段时回绕
    std::size_t field_begin = data_begin;
    bool has_field = false;
    for (std::size_t i = data_begin; i <= line.size(); ++i) {
        const bool at_end = i == line.size();
        const bool is_comma = !at_end && std::to_integer<unsigned char>(line[i]) == ',';
        if (is_comma || at_end) {
            // 逐字段：宽容解析（strtof 取前缀数值）；空字段与坏字段置 NaN 并计数
            const std::string field = bytesToString(line, field_begin, i);
            float v = 0.0F;
            if (!parseFloatField(field, v)) {
                v = std::numeric_limits<float>::quiet_NaN();
                ++stats_.parse_errors;
            }
            if (n < kMaxChannels) {
                f.ch[n] = v;
            }
            ++n;
            has_field = true;
            field_begin = i + 1;
            if (at_end) {
                break;
            }
        }
    }
    if (!has_field) {
        return;
    }

    // 通道数以首行为基准（TS §3.3）；后续行字段数变化计错误，不产出
    // （字段数上界已在解析前由 R-82 预检排除，此处 n ≤ kMaxChannels 恒成立）
    if (!base_ch_count_.has_value()) {
        base_ch_count_ = static_cast<std::uint8_t>(n);
    }
    if (n != base_ch_count_) {
        ++stats_.frames_bad;
        return;
    }

    f.ch_count = *base_ch_count_;
    next_frame_index_++;
    out_.push_back(std::move(f));
    ++stats_.frames_ok;
}

} // namespace gayospace::protocol
