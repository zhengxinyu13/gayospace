#include "SerialError.h"

namespace gayospace::serial {

SerialErrorKind mapQtError(QSerialPort::SerialPortError error)
{
    using E = QSerialPort::SerialPortError;
    switch (error) {
    case E::NoError:                return SerialErrorKind::None;
    case E::DeviceNotFoundError:    return SerialErrorKind::DeviceNotFound;
    case E::PermissionError:        return SerialErrorKind::AccessDenied;
    case E::OpenError:              return SerialErrorKind::OpenFailed;
    case E::WriteError:             return SerialErrorKind::Write;
    case E::ReadError:              return SerialErrorKind::Read;
    case E::ResourceError:          return SerialErrorKind::ResourceLost;
    case E::UnsupportedOperationError: return SerialErrorKind::Unsupported;
    case E::TimeoutError:           return SerialErrorKind::Timeout;
    case E::NotOpenError:           return SerialErrorKind::NotOpen;
    }
    return SerialErrorKind::Unknown;
}

bool isRetryable(SerialErrorKind kind)
{
    switch (kind) {
    case SerialErrorKind::None:
    case SerialErrorKind::AccessDenied:   // 占用/权限——重试无意义（TS §8.3 硬要求）
    case SerialErrorKind::Unsupported:    // 配置问题——重试无意义
    case SerialErrorKind::NotOpen:        // 内部状态错误
        return false;
    default:
        return true;
    }
}

QString userMessage(SerialErrorKind kind)
{
    switch (kind) {
    case SerialErrorKind::None:           return QStringLiteral("无错误");
    case SerialErrorKind::DeviceNotFound: return QStringLiteral("设备不存在，可能已拔出");
    case SerialErrorKind::AccessDenied:   return QStringLiteral("端口被占用（可能被 GUI 或其他程序占用）或权限不足");
    case SerialErrorKind::OpenFailed:     return QStringLiteral("串口打开失败");
    case SerialErrorKind::Read:           return QStringLiteral("读取错误");
    case SerialErrorKind::Write:          return QStringLiteral("发送错误");
    case SerialErrorKind::ResourceLost:   return QStringLiteral("连接中断，设备已消失");
    case SerialErrorKind::Unsupported:    return QStringLiteral("串口参数或操作不受支持，请检查配置");
    case SerialErrorKind::Timeout:        return QStringLiteral("操作超时");
    case SerialErrorKind::NotOpen:        return QStringLiteral("串口未打开");
    case SerialErrorKind::Unknown:        return QStringLiteral("未知串口错误");
    }
    return QStringLiteral("未知串口错误");
}

} // namespace gayospace::serial
