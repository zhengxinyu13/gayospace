// QSerialTransport 行为测试（R-55：目前仅 Fake 覆盖，真实后端零行为断言）。
// 无需硬件：打开不存在的端口名即可驱动"打开失败"路径（R-52 验收用例）。
#include <catch2/catch_test_macros.hpp>

#include <QString>

#include "serial/QSerialTransport.h"
#include "serial/SerialChannel.h"

using namespace gayospace::serial;

namespace {
struct Counters {
    int errors = 0;
    int openFailed = 0;
    int opened = 0;
    int closed = 0;

    void attach(ISerialTransport& t)
    {
        QObject::connect(&t, &ISerialTransport::errorOccurred,
            [&](SerialErrorKind, const QString&) { ++errors; });
        QObject::connect(&t, &ISerialTransport::opened, [&]() { ++opened; });
        QObject::connect(&t, &ISerialTransport::closed, [&]() { ++closed; });
    }
};

SerialParams invalidPortParams()
{
    SerialParams p;
    p.portName = QStringLiteral("COM_DOES_NOT_EXIST_9999");
    return p;
}
} // namespace

TEST_CASE("open failure on nonexistent port emits error exactly once", "[serial][qtransport]")
{
    // R-52 验收：QSerialPort open 失败时自发 errorOccurred，手动补发路径被去重——恰好 1 次
    QSerialTransport transport;
    Counters c;
    c.attach(transport);

    REQUIRE_FALSE(transport.open(invalidPortParams()));
    REQUIRE(c.errors == 1);
    REQUIRE(c.opened == 0);
    REQUIRE_FALSE(transport.isOpen());
}

TEST_CASE("repeated open failures report error once each", "[serial][qtransport]")
{
    QSerialTransport transport;
    Counters c;
    c.attach(transport);

    REQUIRE_FALSE(transport.open(invalidPortParams()));
    REQUIRE_FALSE(transport.open(invalidPortParams()));
    REQUIRE(c.errors == 2); // 每次失败恰好一次，不随调用累积翻倍
}

TEST_CASE("channel reports openFailed exactly once for invalid port", "[serial][qtransport]")
{
    // R-52 验收（探针 C2 场景）：错误经 SerialChannel 后 openFailed 恰好 1 次
    QSerialTransport transport;
    SerialChannel channel(&transport);
    Counters c;
    c.attach(transport);
    int openFailedCount = 0;
    QObject::connect(&channel, &SerialChannel::openFailed,
        [&](SerialErrorKind, const QString&) { ++openFailedCount; });

    channel.open(invalidPortParams());

    REQUIRE(channel.state() == SerialChannelState::Disconnected);
    REQUIRE(openFailedCount == 1);
    REQUIRE(c.errors == 1);
}

TEST_CASE("write to unopened port returns failure without crash", "[serial][qtransport]")
{
    QSerialTransport transport;
    const qint64 written = transport.writeBytes(QByteArray("abc"));
    // QIODevice 契约：未打开时写入失败返回负值（不崩不静默成功）
    REQUIRE(written < 0);
}

TEST_CASE("close without open emits no closed signal", "[serial][qtransport]")
{
    QSerialTransport transport;
    Counters c;
    c.attach(transport);

    transport.close();
    REQUIRE(c.closed == 0);
}

TEST_CASE("empty write is a no-op success", "[serial][qtransport]")
{
    QSerialTransport transport;
    // 未打开端口写空数组：实现短路返回 0，不经底层
    REQUIRE(transport.writeBytes(QByteArray()) == 0);
}
