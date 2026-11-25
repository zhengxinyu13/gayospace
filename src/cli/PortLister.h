// list-ports JSON 输出组装（R-114；TS-SW §6:257 契约：设备名、描述、硬件 ID，
// 供 Agent/脚本 json.loads() 探测——TSV 纯文本违反契约，已废）。
// 纯函数：QSerialPortInfo 列表 → JSON 数组文本（stdout 直写）。
// 字段：device（端口名）、system_location、description、vendor_id / product_id
//（hex 字符串，无效时省略）、serial_number（无效时省略）。
#pragma once

#include <QString>

#include <QSerialPortInfo>

namespace gayospace::cli {

[[nodiscard]] QString portsToJson(const QList<QSerialPortInfo>& ports);

} // namespace gayospace::cli
