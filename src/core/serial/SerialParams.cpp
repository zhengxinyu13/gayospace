#include "SerialParams.h"

namespace gayospace::serial {

QStringList SerialParams::validationErrors() const
{
    QStringList errors;
    if (portName.trimmed().isEmpty()) {
        errors << QStringLiteral("端口名不能为空");
    }
    if (baudRate <= 0) {
        errors << QStringLiteral("波特率必须为正整数");
    }
    return errors;
}

} // namespace gayospace::serial
