// 串口领域错误模型（TS-SW 第 8.3 节重连策略的错误码判定基础；PRD 失效表"串口意外断开"行）。
// QSerialPort::SerialPortError → 领域枚举的映射收敛于此，业务代码只判领域枚举，
// 不直接依赖 Qt 错误码（错误码语义出处：Qt 官方文档 QSerialPort::SerialPortError）。
#pragma once

#include <QSerialPort>
#include <QString>
#include <QMetaType>

namespace gayospace::serial {

enum class SerialErrorKind {
    None,          // 无错误
    DeviceNotFound,// 打开时设备不存在（未插 / 已拔出）→ 可重试
    AccessDenied,  // 被其他进程占用或权限不足 → **不重试**（TS §8.3：占用不重试，避免无限刷）
    OpenFailed,    // 通用打开失败 → 可重试
    Read,          // 读错误 → 可重试（瞬态）
    Write,         // 写错误 → 可重试（瞬态）
    ResourceLost,  // 设备被移除 / 资源不可用（拔线典型码）→ 可重试
    Unsupported,   // 参数/操作不被支持 → 不重试（用户须改配置，重试无意义）
    Timeout,       // 超时 → 可重试
    NotOpen,       // 端口未打开（内部状态错误）→ 不重试
    Unknown        // 未知错误 → 可重试（保守兜底）
};

[[nodiscard]] SerialErrorKind mapQtError(QSerialPort::SerialPortError error);

// TS §8.3：仅"端口被占用/不支持/未打开"不重试，其余可重连。
[[nodiscard]] bool isRetryable(SerialErrorKind kind);

// 用户可读提示（GUI 状态栏 / CLI stderr）。
[[nodiscard]] QString userMessage(SerialErrorKind kind);

} // namespace gayospace::serial

Q_DECLARE_METATYPE(gayospace::serial::SerialErrorKind)
