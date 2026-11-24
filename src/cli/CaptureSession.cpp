// CaptureSession 实现（契约见头文件）。
#include "cli/CaptureSession.h"

#include <algorithm>

namespace gayospace::cli {

CaptureSession::CaptureSession(applogic::PipelinePump& pump, CaptureWriter& writer, CaptureLimits limits)
    : pump_(pump), writer_(writer), limits_(limits)
{
}

bool CaptureSession::tick(qint64 nowMs, CliExitCode& exitCode, QString& error)
{
    exitCode = CliExitCode::Ok;
    error.clear();
    if (finished_) {
        // 幂等：重复 tick 不篡改上次 tick 给出的 exitCode/error（会话结果只归属终结那次调用）
        return false;
    }

    // 1) 排空 ring → parser → store（R-85：stats 缓存快照在 drain 内刷新）
    pump_.drain();

    // 2) 增量读新帧 [lastWritten_, frameIndex)
    auto& store = pump_.store();
    const auto end = store.frameIndex();
    auto start = lastWritten_;
    const auto capacity = store.capacityFrames();
    if (start + capacity < end) {
        // 环形覆盖：最老帧已被新数据冲掉 → 从缓冲内最老可用帧起（旧帧不补写，已物理丢失）。
        // CLI 落盘背压（写盘跟不上串口速率）的完整方案挂账 TODO(v2.0)，见 PR 说明。
        start = end - capacity;
    }
    std::uint64_t count = end > start ? end - start : 0;
    if (limits_.hasSamples) {
        const auto remaining = limits_.samples > static_cast<qint64>(writtenFrames_)
                                   ? static_cast<std::uint64_t>(limits_.samples - static_cast<qint64>(writtenFrames_))
                                   : 0U;
        count = std::min(count, remaining);
    }

    if (count > 0) {
        const auto ch = store.channelCount();
        std::vector<float> buf(static_cast<std::size_t>(count) * ch);
        store.copyFrames(start, count, buf);
        for (std::uint64_t i = 0; i < count; ++i) {
            if (!writer_.writeFrame(start + i, buf.data() + static_cast<std::size_t>(i) * ch, ch, error)) {
                return finishSession(exitCode, error, true); // R-105：落盘失败 → 5
            }
        }
        lastWritten_ = start + count;
        writtenFrames_ += count;
    }

    // 3) 停止判定（优先级：Ctrl+C > samples 上限 > duration 到期；语义互斥，先到先收）
    const bool bySamples = limits_.hasSamples
        && writtenFrames_ >= static_cast<std::uint64_t>(limits_.samples);
    const bool byDuration = limits_.hasDuration && nowMs >= limits_.durationMs;
    if (!(stopRequested_ || bySamples || byDuration)) {
        return true;
    }
    return finishSession(exitCode, error, false);
}

bool CaptureSession::finishSession(CliExitCode& exitCode, QString& error, bool ioFailed)
{
    finished_ = true;
    QString finishErr;
    const bool finishOk = writer_.finish(pump_.stats(), finishErr);
    if (ioFailed) {
        // 帧写入已失败：退出码定死 IoError；finish 的错误信息只作补充，不覆盖首个原因
        if (error.isEmpty()) {
            error = finishErr;
        }
        exitCode = CliExitCode::IoError;
    } else if (!finishOk) {
        error = finishErr;
        exitCode = CliExitCode::IoError;
    } else {
        exitCode = stopRequested_ ? CliExitCode::Interrupted : CliExitCode::Ok;
    }
    return false;
}

} // namespace gayospace::cli
