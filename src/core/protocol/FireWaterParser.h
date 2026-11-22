// FireWater 解析器（TS-SW §4.2；出处 https://www.vofa.plus/docs/learning/dataengines/firewater/ ，核对日 2026-11-16）。
// 格式："<any>:ch0,ch1,...,chN\n"；any 与冒号可省略；\n 不可省略，兼容 \r\n 与 \n\r。
// 前缀判定（R-25 收窄 + R-37 二次收窄）：行首至第一个 ':' 的片段能被 strtof 完整解析为数字
// → 数据（不剔除）；否则视为前缀剔除；image 前缀整行丢弃并计数；空前缀/无冒号按数据宽容解析。
// 行缓冲带单行长度上限（第 17 轮评审 §2.3），超限整行丢弃并计数，防垃圾流无限增长。
#pragma once

#include <deque>
#include <optional>
#include <vector>

#include "protocol/IProtocolParser.h"

namespace gayospace::protocol {

class FireWaterParser final : public IProtocolParser {
public:
    static constexpr std::size_t kMaxLineBytes = 512;   // 单行上限（评审 §2.3），超限整行丢弃

    FireWaterParser() = default;

    void feed(std::span<const std::byte> bytes) override;
    std::optional<ParseFrame> pop_frame() override;
    void reset() override;
    ParseStats stats() const override;

private:
    // 处理一条完整行（不含行结束符）；t 时间戳在此刻取
    void handle_line(const std::vector<std::byte>& line);

    std::vector<std::byte> line_;          // 行缓冲（不含 \n / \r）
    bool line_overflow_ = false;           // 超长行丢弃中：吞到下一个 \n 为止
    std::optional<std::uint8_t> base_ch_count_;   // 通道数基准 = 首条有效数据行字段数（TS §3.3）

    std::deque<ParseFrame> out_;
    ParseStats stats_;
    std::uint64_t next_frame_index_ = 0;
};

} // namespace gayospace::protocol
