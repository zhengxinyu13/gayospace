// 自动重连退避策略与 SerialChannel 状态机单测（TS-SW §8.3）。
// 用假传输 + 秒级以下梯度策略驱动全路径，无真实端口、无真实等待。
#include <catch2/catch_test_macros.hpp>

#include <QVector>

#include "FakeTransport.h"
#include "serial/ReconnectPolicy.h"
#include "serial/SerialChannel.h"

using namespace gayospace::serial;
using namespace gayospace::serial::test;

namespace {
// 微秒级测试策略：1ms→2ms→4ms 封顶 5ms，共 10 次（与默认策略同构，等比缩小 1000 倍）；
// 梯度数值断言见 test_serial_reconnect_policy.cpp，此处只关心编排时序。
ReconnectPolicy testPolicy()
{
    ReconnectPolicy p;
    p.stepDelays = {std::chrono::milliseconds{1}, std::chrono::milliseconds{2},
        std::chrono::milliseconds{4}};
    p.capDelay = std::chrono::milliseconds{5};
    p.maxAttempts = 10;
    return p;
}

// 记录状态迁移与信号的探针
struct Probe {
    QVector<int> states;       // SerialChannelState 序列
    QVector<int> scheduleDelays; // 每次 reconnectScheduled 的 delayMs
    QVector<int> scheduleAttempts;
    bool reconnectedFired = false;
    bool openFailedFired = false;
    bool failedErrorFired = false;

    void attach(SerialChannel& ch)
    {
        QObject::connect(&ch, &SerialChannel::stateChanged,
            [&](SerialChannelState s) { states.append(static_cast<int>(s)); });
        QObject::connect(&ch, &SerialChannel::reconnectScheduled,
            [&](int attempt, int delayMs) {
                scheduleAttempts.append(attempt);
                scheduleDelays.append(delayMs);
            });
        QObject::connect(&ch, &SerialChannel::reconnected, [&]() { reconnectedFired = true; });
        QObject::connect(&ch, &SerialChannel::openFailed, [&](SerialErrorKind, const QString&) {
            openFailedFired = true;
        });
        QObject::connect(&ch, &SerialChannel::errorOccurred,
            [&](SerialErrorKind, const QString& msg) {
                if (msg.contains("自动重连失败")) {
                    failedErrorFired = true;
                }
            });
    }
};
} // namespace

// ---- SerialChannel：打开/关闭状态机 ----

TEST_CASE("open success reaches Connected", "[serial][channel]"){
    FakeTransport transport;
    SerialChannel channel(&transport);
    Probe probe;
    probe.attach(channel);

    channel.setPolicy(testPolicy());
    channel.open(SerialParams{});

    REQUIRE(channel.state() == SerialChannelState::Connected);
    REQUIRE(probe.states == QVector<int>{static_cast<int>(SerialChannelState::Connected)});
    REQUIRE(transport.m_params.portName.isEmpty()); // 参数已下传
}

TEST_CASE("user close returns to Disconnected", "[serial][channel]")
{
    FakeTransport transport;
    SerialChannel channel(&transport);
    Probe probe;
    probe.attach(channel);
    channel.setPolicy(testPolicy());

    channel.open(SerialParams{});
    REQUIRE(channel.state() == SerialChannelState::Connected);

    channel.close();
    REQUIRE(channel.state() == SerialChannelState::Disconnected);
    REQUIRE_FALSE(transport.isOpen());

    // R-51 验收：用户主动关闭不得触发重连状态与排程
    REQUIRE(probe.scheduleDelays.isEmpty());
    REQUIRE(probe.scheduleAttempts.isEmpty());
    REQUIRE_FALSE(probe.states.contains(static_cast<int>(SerialChannelState::Reconnecting)));
    REQUIRE(probe.states == QVector<int>{static_cast<int>(SerialChannelState::Connected),
        static_cast<int>(SerialChannelState::Disconnected)});
    REQUIRE(transport.m_closeCount == 1); // R-54 同源：close 必须真的关传输
}

TEST_CASE("initial open failure reports openFailed without auto-reconnect", "[serial][channel]")
{
    // TS §8.3：自动重连仅由"已连接状态下的会话中断"触发；初始打开失败只报错。
    FakeTransport transport;
    SerialChannel channel(&transport);
    Probe probe;
    probe.attach(channel);
    channel.setPolicy(testPolicy());

    transport.m_nextOpenError = SerialErrorKind::DeviceNotFound;
    channel.open(SerialParams{});

    REQUIRE(channel.state() == SerialChannelState::Disconnected);
    REQUIRE(probe.openFailedFired);
    REQUIRE(probe.scheduleDelays.isEmpty()); // 未排程任何重连
    REQUIRE(transport.m_openCount == 0);
}

TEST_CASE("initial open failure with occupied port enters Failed and reports openFailed",
    "[serial][channel]")
{
    FakeTransport transport;
    SerialChannel channel(&transport);
    Probe probe;
    probe.attach(channel);
    channel.setPolicy(testPolicy());

    transport.m_nextOpenError = SerialErrorKind::AccessDenied; // 占用：不可重试
    channel.open(SerialParams{});

    REQUIRE(channel.state() == SerialChannelState::Failed);
    REQUIRE(probe.openFailedFired); // R-53：不可重试的打开失败同样发 openFailed
}

// ---- SerialChannel：会话中断后的自动重连 ----

TEST_CASE("disconnect triggers full backoff sequence then Failed", "[serial][channel]")
{
    // 全程重连失败：10 次尝试，梯度 1,2,4,5,5,5,5,5,5,5 ms（×1000 = TS §8.3 默认值）
    FakeTransport transport;
    SerialChannel channel(&transport);
    Probe probe;
    probe.attach(channel);
    channel.setPolicy(testPolicy());

    channel.open(SerialParams{});
    REQUIRE(channel.state() == SerialChannelState::Connected);

    transport.simulateDisconnect();
    REQUIRE(channel.state() == SerialChannelState::Reconnecting);

    // 每次定时器到点即失败一次，直到耗尽
    for (int i = 0; i < 10; ++i) {
        transport.m_nextOpenError = SerialErrorKind::OpenFailed;
        channel.fireReconnectTimer();
    }

    REQUIRE(channel.state() == SerialChannelState::Failed);
    REQUIRE(probe.failedErrorFired); // "自动重连失败 10 次"
    REQUIRE(probe.scheduleAttempts.size() == 10);
    REQUIRE(probe.scheduleAttempts == (QVector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
    REQUIRE(probe.scheduleDelays == (QVector<int>{1, 2, 4, 5, 5, 5, 5, 5, 5, 5}));
    REQUIRE(channel.reconnectAttempt() == 10);
}

TEST_CASE("successful reconnect clears attempt counter", "[serial][channel]")
{
    FakeTransport transport;
    SerialChannel channel(&transport);
    Probe probe;
    probe.attach(channel);
    channel.setPolicy(testPolicy());

    channel.open(SerialParams{});
    transport.simulateDisconnect();

    // 第 1 次重连失败，第 2 次成功
    transport.m_nextOpenError = SerialErrorKind::OpenFailed;
    channel.fireReconnectTimer();
    REQUIRE(channel.state() == SerialChannelState::Reconnecting);

    channel.fireReconnectTimer();
    REQUIRE(channel.state() == SerialChannelState::Connected);
    REQUIRE(probe.reconnectedFired);
    REQUIRE(channel.reconnectAttempt() == 0); // 任一次成功即清计数（TS §8.3）
}

TEST_CASE("stale reconnect timer after close is a no-op", "[serial][channel]")
{
    FakeTransport transport;
    SerialChannel channel(&transport);
    channel.setPolicy(testPolicy());

    channel.open(SerialParams{});
    transport.simulateDisconnect();
    REQUIRE(channel.state() == SerialChannelState::Reconnecting);

    channel.close();
    REQUIRE(channel.state() == SerialChannelState::Disconnected);

    channel.fireReconnectTimer(); // 残余定时器触发：不得重开
    REQUIRE(channel.state() == SerialChannelState::Disconnected);
    REQUIRE(transport.m_openCount == 1); // 只有最初那一次 open
}

TEST_CASE("reconfig open while connected emits no reconnected or reconnect schedule",
    "[serial][channel]")
{
    // R-51 验收（探针 C）：已连接时再次 open（重配）——QSerialTransport 会先关旧口，
    // 不得误判为会话中断而进入 Reconnecting / 误发 reconnected。
    FakeTransport transport;
    SerialChannel channel(&transport);
    Probe probe;
    probe.attach(channel);
    channel.setPolicy(testPolicy());

    channel.open(SerialParams{});
    REQUIRE(channel.state() == SerialChannelState::Connected);

    SerialParams reconfig;
    reconfig.portName = QStringLiteral("COM7");
    channel.open(reconfig);

    REQUIRE(channel.state() == SerialChannelState::Connected);
    REQUIRE_FALSE(probe.reconnectedFired);
    REQUIRE(probe.scheduleDelays.isEmpty());
    REQUIRE_FALSE(probe.states.contains(static_cast<int>(SerialChannelState::Reconnecting)));
    REQUIRE(transport.m_openCount == 2);
    REQUIRE(transport.m_params.portName == QStringLiteral("COM7"));
}

TEST_CASE("transient error with auto-reconnect off closes transport", "[serial][channel]")
{
    // R-54 验收：autoReconnect 关闭 + 瞬态错误（Timeout，端口未关）——
    // 状态迁移到 Disconnected 的同时必须关闭传输，二者保持一致。
    FakeTransport transport;
    SerialChannel channel(&transport);
    channel.setAutoReconnect(false);
    channel.setPolicy(testPolicy());

    channel.open(SerialParams{});
    REQUIRE(transport.isOpen());

    transport.simulateError(SerialErrorKind::Timeout);
    REQUIRE(channel.state() == SerialChannelState::Disconnected);
    REQUIRE_FALSE(transport.isOpen());
    REQUIRE(transport.m_closeCount == 1);
}

TEST_CASE("auto-reconnect off falls back to Disconnected on disconnect", "[serial][channel]")
{
    FakeTransport transport;
    SerialChannel channel(&transport);
    channel.setAutoReconnect(false);
    channel.setPolicy(testPolicy());

    channel.open(SerialParams{});
    transport.simulateDisconnect();

    REQUIRE(channel.state() == SerialChannelState::Disconnected);
    REQUIRE(transport.m_openCount == 1); // 无重试
}

TEST_CASE("occupied during session goes straight to Failed without retries", "[serial][channel]")
{
    // TS §8.3：端口被占用不重试，直接进入上限后状态
    FakeTransport transport;
    SerialChannel channel(&transport);
    Probe probe;
    probe.attach(channel);
    channel.setPolicy(testPolicy());

    channel.open(SerialParams{});
    transport.simulateDisconnect(SerialErrorKind::AccessDenied);

    REQUIRE(channel.state() == SerialChannelState::Failed);
    REQUIRE(probe.scheduleDelays.isEmpty()); // 一次都不排程
}

TEST_CASE("bytes are forwarded from transport", "[serial][channel]")
{
    FakeTransport transport;
    SerialChannel channel(&transport);
    QByteArray received;
    QObject::connect(&channel, &SerialChannel::bytesReceived,
        [&](const QByteArray& d) { received.append(d); });

    channel.open(SerialParams{});
    transport.simulateBytes("hello");

    REQUIRE(received == QByteArray("hello"));
}
