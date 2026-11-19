#include "SerialChannel.h"

namespace gayospace::serial {

SerialChannel::SerialChannel(ISerialTransport* transport, QObject* parent)
    : QObject(parent)
    , m_transport(transport)
{
    Q_ASSERT(m_transport != nullptr);

    connect(m_transport, &ISerialTransport::opened, this, [this]() {
        m_attempt = 0;
        setState(SerialChannelState::Connected);
        if (m_wasReconnecting) {
            m_wasReconnecting = false;
            emit reconnected();
        }
    });

    connect(m_transport, &ISerialTransport::errorOccurred, this, &SerialChannel::handleTransportError);
    connect(m_transport, &ISerialTransport::closed, this, &SerialChannel::handleTransportClosed);
    connect(m_transport, &ISerialTransport::bytesReceived, this, &SerialChannel::bytesReceived);
}

void SerialChannel::setAutoReconnect(bool on)
{
    m_autoReconnect = on;
}

void SerialChannel::setPolicy(const ReconnectPolicy& policy)
{
    m_policy = policy;
}

void SerialChannel::open(const SerialParams& params)
{
    m_params = params;
    m_attempt = 0;
    m_wasReconnecting = false;
    // 打开失败时不自动重连（TS §8.3：重连触发条件是"已连接状态下的会话中断"）。
    // 失败路径：transport 同步发出 errorOccurred → handleTransportError → 报 openFailed。
    m_transport->open(params);
}

void SerialChannel::close()
{
    m_transport->close();
    m_attempt = 0;
    m_wasReconnecting = false;
    setState(SerialChannelState::Disconnected);
}

void SerialChannel::fireReconnectTimer()
{
    if (m_state != SerialChannelState::Reconnecting) {
        return; // close() 或进入 Failed 后残余定时器安全失效
    }
    m_transport->open(m_params);
}

void SerialChannel::setState(SerialChannelState next)
{
    if (m_state == next) {
        return;
    }
    m_state = next;
    emit stateChanged(next);
}

void SerialChannel::startReconnect()
{
    m_attempt = 0;
    m_wasReconnecting = true;
    setState(SerialChannelState::Reconnecting);
    scheduleNextAttempt();
}

void SerialChannel::scheduleNextAttempt()
{
    if (m_policy.exhausted(m_attempt)) {
        m_wasReconnecting = false;
        setState(SerialChannelState::Failed);
        emit errorOccurred(SerialErrorKind::ResourceLost,
            QStringLiteral("已断开（自动重连失败 %1 次）").arg(m_policy.maxAttempts));
        return;
    }
    ++m_attempt;
    emit reconnectScheduled(m_attempt,
        static_cast<int>(m_policy.delayBefore(m_attempt).count()));
}

void SerialChannel::handleTransportError(SerialErrorKind kind, const QString& message)
{
    emit errorOccurred(kind, message);

    if (kind == SerialErrorKind::None) {
        return;
    }

    if (!isRetryable(kind)) {
        // 占用/不支持/未打开：直接终态（TS §8.3：占用不重试，避免无限刷）
        m_wasReconnecting = false;
        setState(SerialChannelState::Failed);
        return;
    }

    // 打开失败（状态未变，仍为 Disconnected）：仅报 openFailed，不自动重连
    if (m_state == SerialChannelState::Disconnected) {
        emit openFailed(kind, message);
        return;
    }

    if (m_state == SerialChannelState::Connected && m_autoReconnect) {
        // 会话中断 → 进入自动重连（PRD 失效表：进入重连流程）
        startReconnect();
        return;
    }

    if (m_state == SerialChannelState::Reconnecting) {
        // 重连尝试本身失败 → 排程下一次或进入终态
        scheduleNextAttempt();
        return;
    }

    if (m_state == SerialChannelState::Connected && !m_autoReconnect) {
        setState(SerialChannelState::Disconnected);
    }
    // Failed 状态下的迟到错误：已终态，只上报不迁移
}

void SerialChannel::handleTransportClosed()
{
    // 传输层 closed 与 errorOccurred 常成对到达（如 ResourceError）。
    // 断开迁移以 errorOccurred 为主路径；这里只在 error 未触发迁移时兜底，
    // 保证"设备消失但无错误码"的场景同样进入重连。
    if (m_state == SerialChannelState::Connected && m_autoReconnect) {
        startReconnect();
    } else if (m_state == SerialChannelState::Connected) {
        setState(SerialChannelState::Disconnected);
    }
}

} // namespace gayospace::serial
