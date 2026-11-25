// list-ports JSON 输出实现（契约见头文件）。
#include "cli/PortLister.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace gayospace::cli {

QString portsToJson(const QList<QSerialPortInfo>& ports)
{
    QJsonArray arr;
    for (const auto& info : ports) {
        QJsonObject o;
        o.insert(QStringLiteral("device"), info.portName());
        o.insert(QStringLiteral("system_location"), info.systemLocation());
        o.insert(QStringLiteral("description"), info.description());
        // 硬件 ID：VID/PID hex（无效=缺省省略）；serial_number 无效同样省略
        if (info.vendorIdentifier() != -1) {
            o.insert(QStringLiteral("vendor_id"),
                QString::number(static_cast<ushort>(info.vendorIdentifier()), 16));
        }
        if (info.productIdentifier() != -1) {
            o.insert(QStringLiteral("product_id"),
                QString::number(static_cast<ushort>(info.productIdentifier()), 16));
        }
        if (!info.serialNumber().isEmpty()) {
            o.insert(QStringLiteral("serial_number"), info.serialNumber());
        }
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

} // namespace gayospace::cli
