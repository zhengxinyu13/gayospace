// 协议层共享类型（TS-SW §4.0 / §3.3）。
// MAX_CHANNELS 编译期常量集中于此（第 17 轮评审 §2.1：两份 parser 与未来 Script parser 共用，不各自 define）。
#pragma once

#include <array>
#include <chrono>
#include <cstdint>

namespace gayospace::protocol {

// TS-SW §3.3：32 通道上限（v1.0 编译期常量 MAX_CHANNELS；PRD F-05 要求 ≥8）
inline constexpr std::size_t kMaxChannels = 32;

struct ParseFrame {
    std::chrono::steady_clock::time_point t;   // 到达校准时间戳（仅用于 t0 校准，TS §3.4；显示时间不用它）
    std::uint64_t frame_index = 0;             // 单调递增帧序号（显示时间 = t0 + index/fs，TS §3.4）
    std::array<float, kMaxChannels> ch{};      // 仅前 ch_count 有效
    std::uint8_t ch_count = 0;
};

// TS-SW §4.0 定义三字段；parse_errors 为第 17 轮评审 §2.3 新增（FireWater 失败字段逐个计数，
// 独立于 frames_bad；JustFloat 恒 0）。TS §4.0 的 struct 已随本 PR 同步修订。
struct ParseStats {
    std::uint64_t frames_ok = 0;
    std::uint64_t frames_bad = 0;
    std::uint64_t bytes_dropped_sync = 0;
    std::uint64_t parse_errors = 0;
};

} // namespace gayospace::protocol
