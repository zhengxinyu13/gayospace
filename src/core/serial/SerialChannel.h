// 串口会话通道：状态机 + 自动重连编排（TS-SW §8.3；PRD 失效表"串口意外断开"行）。
// 仅依赖 ISerialTransport 抽象——单测用假传输驱动全部路径，无需真实端口。
//
// 状态语义：
//   Disconnected — 初始 / 用户主动关闭 / 打开失败（可重试错误且未开自动重连）
//   Connected    — 传输已打开，可收发
//   Reconnecting — 会话中断开后自动重连进行中（退避计次中）
//   Failed       — 终态：重连尝试耗尽，或不可重试错误（占用/不支持）→ 需用户干预
//
// 触发语义（对齐 TS §8.3）：自动重连仅由"已连接状态下的会话中断"触发；
// 初始打开失败只报错，不自动重连（CLI 场景退出码 3 由上层据 errorOccurred 判定）。
#pragma once

#include <QObject>
#include <QString>

#include "ISerialTransport.h"
#include "ReconnectPolicy.h"
#include "SerialError.h"
#include "SerialParams.h"

namespace gayospace::serial {

enum class SerialChannelState { Disconnected, Connected, Reconnecting, Failed };

class SerialChannel : public QObject {
    Q_OBJECT
public:
    // 不持有所有权：transport 生命周期由调用方管理（worker 层）。
    explicit SerialChannel(ISerialTransport* transport, QObject* parent = nullptr);

    void setAutoReconnect(bool on);
    void setPolicy(const ReconnectPolicy& policy);

    // 用户请求打开。打开结果经 opened()/errorOccurred 异步回报（传输层同步实现时为同步回调）。
    void open(const SerialParams& params);
    // 用户主动关闭：终止重连循环 → Disconnected。
    void close();

    // 重连定时器到点回调。worker 层把 reconnectScheduled(attempt, delayMs) 接到
    // QTimer::singleShot(delayMs, this, &SerialChannel::fireReconnectTimer)；
    // 测试直接调用本槽驱动重连时序，不经真实等待。
    // 状态非 Reconnecting 时为 no-op（close() 后残余定时器安全失效）。
    void fireReconnectTimer();

    [[nodiscard]] SerialChannelState state() const { return m_state; }
    [[nodiscard]] int reconnectAttempt() const { return m_attempt; }
    [[nodiscard]] SerialParams params() const { return m_params; }

signals:
    void stateChanged(gayospace::serial::SerialChannelState state);
    void errorOccurred(gayospace::serial::SerialErrorKind kind, const QString& message);
    // 自动重连已排程：第 attempt 次尝试、等待 delayMs。上层据此设置定时器。
    void reconnectScheduled(int attempt, int delayMs);
    // 自动重连成功回到 Connected。
    void reconnected();
    // 用户 open() 失败（未触发自动重连的初始打开失败，供 CLI/GUI 直接报错）。
    void openFailed(gayospace::serial::SerialErrorKind kind, const QString& message);
    // 收到原始字节（转发自传输层）。
    void bytesReceived(const QByteArray& data);

private:
    void setState(SerialChannelState next);
    void startReconnect();
    void scheduleNextAttempt();
    void handleTransportError(SerialErrorKind kind, const QString& message);
    void handleTransportClosed();

    ISerialTransport* m_transport;
    ReconnectPolicy m_policy = ReconnectPolicy::defaultPolicy();
    bool m_autoReconnect = true;
    bool m_wasReconnecting = false; // 本次 Connected 是否来自自动重连（区分首开与重连成功）
    SerialChannelState m_state = SerialChannelState::Disconnected;
    SerialParams m_params;
    int m_attempt = 0; // 已执行的重连尝试次数（0 = 未在重连）
};

} // namespace gayospace::serial

Q_DECLARE_METATYPE(gayospace::serial::SerialChannelState)
