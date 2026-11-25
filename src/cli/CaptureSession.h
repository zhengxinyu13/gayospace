// CaptureSession：capture 会话编排（批次②第二步；TS-SW §6 停止条件与退出码映射）。
// 单线程步进类（非 QObject，与 PipelinePump 同风格）：main.cpp 用 QTimer 轮询 tick()，
// CLI 不引入 PipelineThread（第 32 轮评审拍板：CLI 单线程事件循环足够）。
//
// 每 tick：pump.drain() → 从 store 增量读新帧（[lastWritten, frameIndex)）→ writer 落盘
// → 停止判定。终结时 writer.finish(stats) 落盘收尾并给出退出码：
//   - Ctrl+C（requestStop）→ Interrupted(130)，已收数据先落盘（R-105 语义）
//   - samples/duration 到达 → Ok(0)
//   - writer 任何一步失败 → IoError(5)
//   - parse_errors/frames_bad 不降退出码（R-105：垃圾流是数据质量问题，不是运行失败）
#pragma once

#include <QString>

#include <cstdint>
#include <vector>

#include "applogic/PipelinePump.h"
#include "cli/CaptureWriter.h"
#include "cli/CliOptions.h"

namespace gayospace::cli {

// 停止条件（--duration/--samples 经 CliOptions 校验面：duration ≤ 3600s、二选一）。
struct CaptureLimits {
    bool hasDuration = false;
    qint64 durationMs = 0;
    bool hasSamples = false;
    qint64 samples = 0;
};

class CaptureSession {
public:
    CaptureSession(applogic::PipelinePump& pump, CaptureWriter& writer, CaptureLimits limits);

    // Ctrl+C 入口（main.cpp 由控制台句柄/信号处置器置位；本类只负责"下个 tick 收尾"）。
    void requestStop() noexcept { stopRequested_ = true; }
    [[nodiscard]] bool stopRequested() const noexcept { return stopRequested_; }

    // 已写入 writer 的帧数（samples 停止判定与测试断言用）。
    [[nodiscard]] std::uint64_t writtenFrames() const noexcept { return writtenFrames_; }

    // 单线程 tick。nowMs = 会话起始起的毫秒数（main.cpp 由 QElapsedTimer 提供；测试注入假时钟）。
    // 返回 true = 会话继续；返回 false = 会话已终结（exitCode 为最终退出码，error 为用户可读信息）。
    bool tick(qint64 nowMs, CliExitCode& exitCode, QString& error);

    [[nodiscard]] bool finished() const noexcept { return finished_; }

private:
    [[nodiscard]] bool finishSession(CliExitCode& exitCode, QString& error, bool ioFailed);

    applogic::PipelinePump& pump_;
    CaptureWriter& writer_;
    CaptureLimits limits_;

    std::uint64_t lastWritten_ = 0;  // store 帧序号：下一个待写帧（增量读基点）
    std::uint64_t writtenFrames_ = 0;
    bool stopRequested_ = false;
    bool finished_ = false;
};

} // namespace gayospace::cli
