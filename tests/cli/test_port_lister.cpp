// PortLister 单测（R-114；TS-SW §6:257 list-ports JSON 契约）。
// QSerialPortInfo 无公开构造函数，无法伪造端口实例——本文件验证空列表输出
// "[]" 是合法 JSON（TS 契约"合法 JSON 输出"的最小保证）；真实端口字段由
// com0com spike 后的真机手测覆盖（DoD 2.5）。
#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QSerialPortInfo>

#include "cli/PortLister.h"

using gayospace::cli::portsToJson;

TEST_CASE("list-ports empty enumeration is valid JSON array", "[cli][portlister]")
{
    const auto json = portsToJson(QList<QSerialPortInfo>{});
    REQUIRE(json == QStringLiteral("[]"));
    const auto doc = QJsonDocument::fromJson(json.toUtf8());
    REQUIRE(doc.isArray());
    REQUIRE(doc.array().size() == 0);
}
