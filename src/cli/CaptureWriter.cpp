// CaptureWriter 实现（契约见头文件；TS-SW §6）。
// 流式策略：meta 用 QJsonDocument 序列化（免手工转义）；samples 逐帧手写 JSON 文本
// （QJsonDocument 全量包装需驻留全帧，违背 R-13 流式语义）——通道浮点一律 %1 经
// QString::number 输出，NaN/Inf 分支输出 null。
#include "cli/CaptureWriter.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

#include <cmath>

#include "gayospace_version.h"
#include "platform/FileOps.h"

namespace gayospace::cli {

using applogic::PipelineStats;

namespace {

// NaN/Inf 判定（JSON 输出 null；CSV 输出空字段）。
[[nodiscard]] bool isBadFloat(float v) noexcept
{
    return !std::isfinite(v);
}

// t_ms = round(frameIndex × 1000 / rate)（TS §6；调用方保证 hasRate）。
[[nodiscard]] qint64 frameTimeMs(std::uint64_t frameIndex, double rateFs) noexcept
{
    return static_cast<qint64>(std::llround(static_cast<double>(frameIndex) * 1000.0 / rateFs));
}

} // namespace

CaptureWriter::CaptureWriter(CaptureMeta meta, QString outPath)
    : meta_(std::move(meta)),
      outPath_(std::move(outPath)),
      tempPath_(outPath_ + QStringLiteral(".tmp")),
      json_(outPath_.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
{
}

QString CaptureWriter::timeFieldName() const
{
    return meta_.hasRate ? QStringLiteral("t_ms") : QStringLiteral("t_index");
}

bool CaptureWriter::begin(QString& error)
{
    error.clear();
    Q_ASSERT(!finished_);
    // 不用 QIODevice::Text：Windows 会把 \n 转 \r\n，CSV 行尾随平台漂移；
    // 统一二进制写 \n（JSON 合法性不受影响，CSV 首行契约确定性）
    file_ = new QFile(tempPath_);
    if (!file_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = QStringLiteral("无法创建临时文件 %1：%2").arg(tempPath_, file_->errorString());
        delete file_;
        file_ = nullptr;
        return false;
    }

    if (json_) {
        QJsonObject m;
        m.insert(QStringLiteral("tool"), QStringLiteral("gayospace"));
        m.insert(QStringLiteral("version"), gayospace::kVersion);
        m.insert(QStringLiteral("port"), meta_.port);
        m.insert(QStringLiteral("baud"), meta_.baud);
        m.insert(QStringLiteral("protocol"), meta_.protocol);
        m.insert(QStringLiteral("channels"), meta_.channels);
        if (meta_.hasRate) { // R-106：缺省 → 省略 rate 字段
            m.insert(QStringLiteral("rate"), meta_.rateFs);
        }
        m.insert(QStringLiteral("started_at"),
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

        const auto head = QStringLiteral("{\"meta\":")
                          + QString::fromUtf8(QJsonDocument(m).toJson(QJsonDocument::Compact))
                          + QStringLiteral(",\"samples\":[");
        file_->write(head.toUtf8());
    } else {
        // CSV 首行（R-40）：t_ms,ch0,ch1,… / t_index,ch0,ch1,…
        QString header = timeFieldName();
        for (int i = 0; i < meta_.channels; ++i) {
            header += QStringLiteral(",ch%1").arg(i);
        }
        file_->write(header.toUtf8());
        file_->write("\n");
    }
    return true;
}

bool CaptureWriter::writeFrame(std::uint64_t frameIndex, const float* ch,
    std::size_t chCount, QString& error)
{
    error.clear();
    if (file_ == nullptr || finished_) {
        error = QStringLiteral("CaptureWriter 状态机错误：begin 前或 finish 后调用 writeFrame");
        return false;
    }

    QByteArray line;
    if (json_) {
        if (!firstSample_) {
            line += ',';
        }
        line += "{\"" + timeFieldName().toUtf8() + "\":";
        line += meta_.hasRate
                    ? QByteArray::number(frameTimeMs(frameIndex, meta_.rateFs))
                    : QByteArray::number(static_cast<qulonglong>(frameIndex));
        line += ",\"ch\":[";
        for (std::size_t i = 0; i < chCount; ++i) {
            if (i != 0) {
                line += ',';
            }
            line += isBadFloat(ch[i]) ? QByteArrayLiteral("null")
                                      : QByteArray::number(static_cast<double>(ch[i]), 'g', 9);
        }
        line += "]}";
    } else {
        if (meta_.hasRate) {
            line += QByteArray::number(frameTimeMs(frameIndex, meta_.rateFs));
        } else {
            line += QByteArray::number(static_cast<qulonglong>(frameIndex));
        }
        for (std::size_t i = 0; i < chCount; ++i) {
            line += ',';
            if (!isBadFloat(ch[i])) {
                line += QByteArray::number(static_cast<double>(ch[i]), 'g', 9);
            }
            // NaN → 空字段（逗号后留空）
        }
        line += '\n';
    }
    firstSample_ = false;
    return file_->write(line) >= 0;
}

bool CaptureWriter::finish(const PipelineStats& stats, QString& error)
{
    error.clear();
    if (file_ == nullptr || finished_) {
        error = QStringLiteral("CaptureWriter 状态机错误：begin 前或重复 finish");
        return false;
    }

    if (json_) {
        // stats（R-86：rx_bytes = ringBytes + rxDropped，组装点在 CLI/Writer）
        QJsonObject s;
        s.insert(QStringLiteral("rx_bytes"),
            static_cast<qint64>(stats.ringBytes) + static_cast<qint64>(stats.rxDropped));
        s.insert(QStringLiteral("dropped_bytes"), static_cast<qint64>(stats.droppedBytes));
        s.insert(QStringLiteral("rx_dropped"), static_cast<qint64>(stats.rxDropped));
        s.insert(QStringLiteral("parse_errors"), static_cast<qint64>(stats.parseErrors));
        const auto tail = QStringLiteral("],\"stats\":")
                          + QString::fromUtf8(QJsonDocument(s).toJson(QJsonDocument::Compact))
                          + QStringLiteral("}");
        if (file_->write(tail.toUtf8()) < 0) {
            error = QStringLiteral("写入失败：%1").arg(file_->errorString());
            delete file_;
            file_ = nullptr;
            return false;
        }
    }

    if (!file_->flush()) {
        error = QStringLiteral("刷盘失败：%1").arg(file_->errorString());
        delete file_;
        file_ = nullptr;
        return false;
    }
    const auto tempPath = tempPath_;
    delete file_;
    file_ = nullptr;

    // R-103：临时文件原子覆盖到目标（Windows 走 MoveFileEx，POSIX 走 rename）
    if (!platform::replaceFile(std::filesystem::path(reinterpret_cast<const char16_t*>(
                                                   tempPath.utf16())),
            std::filesystem::path(reinterpret_cast<const char16_t*>(outPath_.utf16())),
            error)) {
        QFile::remove(tempPath); // 替换失败清临时文件，不留垃圾
        return false;
    }
    finished_ = true;
    return true;
}

} // namespace gayospace::cli
