// CaptureWriter：capture 落盘器（批次②；TS-SW §6 JSON/CSV 流式写 + R-103 原子落盘）。
// 流式语义（R-13）：帧不全量驻留内存——写临时文件（<out>.tmp），finish 时经
// platform::replaceFile 原子覆盖目标（R-103：QFile::rename 在 Windows 目标已存在时必失败）。
// 字段契约：
//  - JSON：单文件合法 JSON = {"meta":{...},"samples":[
//    {..},..],"stats":{...}}；t_ms = round(index×1000/rate)；--rate 缺省 → t_index
//    （R-106）且 meta 无 rate 字段；NaN/Inf → null（JSON 标准不允许 NaN 字面量，取舍见测试注记）。
//  - CSV：首行 t_ms,ch0,ch1,…（rate 缺省首列 t_index，R-40）；NaN → 空字段。
//  - stats：rx_bytes = ringBytes + rxDropped（R-86，组装点在 CLI）。
#pragma once

#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>

#include "applogic/PipelineStats.h"

class QFile;

namespace gayospace::cli {

struct CaptureMeta {
    QString port;
    qint32 baud = 115200;
    QString protocol;
    int channels = 0;
    bool hasRate = false;
    double rateFs = 0.0;
};

class CaptureWriter {
public:
    CaptureWriter(CaptureMeta meta, QString outPath);
    // RAII：未 finish 即析构（如 IoError 路径 / 中途异常）时关闭并泄漏清理临时句柄，
    // 防止 fixture remove_all 遇占用文件抛 filesystem_error → terminate
    ~CaptureWriter();

    CaptureWriter(const CaptureWriter&) = delete;
    CaptureWriter& operator=(const CaptureWriter&) = delete;

    // 写 meta（JSON）/首行（CSV）到临时文件。失败（临时文件打不开）→ false + error。
    [[nodiscard]] bool begin(QString& error);

    // 追加一帧（frame-major，chCount 个样本；JSON null / CSV 空字段处理 NaN）。
    [[nodiscard]] bool writeFrame(std::uint64_t frameIndex, const float* ch,
        std::size_t chCount, QString& error);

    // 写 stats 收尾 + 临时文件原子替换到目标（R-103）。幂等保护：二次调用报错。
    [[nodiscard]] bool finish(const applogic::PipelineStats& stats, QString& error);

    [[nodiscard]] bool finished() const noexcept { return finished_; }

private:
    [[nodiscard]] QString timeFieldName() const; // "t_ms" | "t_index"（R-106）

    CaptureMeta meta_;
    QString outPath_;
    QString tempPath_;
    QFile* file_ = nullptr; // 临时文件句柄（QFile 前置声明规避 include）
    bool json_ = false;
    bool firstSample_ = true;
    bool finished_ = false;
};

} // namespace gayospace::cli
