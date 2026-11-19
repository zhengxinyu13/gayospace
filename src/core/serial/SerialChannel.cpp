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
    // R-51：重配场景（当前已连接/重连中再次 open）先把状态归位 Disconnected——
    // QSerialTransport::open 内部会先关旧口，若不归位，closed() 会被误判为
    // "会话中断"而进入重连。归位后 closed()/errorOccurred 均走初始打开路径。
    // Failed 终态同样允许归位重开（对应 GUI"重连"按钮的语义）。
    if (m_state != SerialChannelState::Disconnected) {
        setState(SerialChannelState::Disconnected);
    }
    // 打开失败时不自动重连（TS §8.3：重连触发条件是"已连接状态下的会话中断"）。
    m_openInProgress = true;
    m_transport->open(params);
    m_openInProgress = false;
}

void SerialChannel::close()
{
    // R-51：先归位 Disconnected 再关传输——阻断 transport 同步 closed() 对
    // "已连接会话中断"的误判（否则会闪一次 Reconnecting 并排程一次假重连）。
    m_attempt = 0;
    m_wasReconnecting = false;
    setState(SerialChannelState::Disconnected);
    m_transport->close();
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

    // R-53：open() 发起的失败一律发 openFailed（无论可否重试），契约统一；
    // 随后按可重试性定状态——不可重试进 Failed，可重试保持 Disconnected 等上层决策。
    if (m_openInProgress) {
        emit openFailed(kind, message);
        if (!isRetryable(kind)) {
            setState(SerialChannelState::Failed);
        }
        return;
    }

    if (!isRetryable(kind)) {
        // 占用/不支持/未打开：直接终态（TS §8.3：占用不重试，避免无限刷）
        m_wasReconnecting = false;
        setState(SerialChannelState::Failed);
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
        // R-54：autoReconnect 关闭时断开须同时关端口——状态与传输保持一致，
        // 避免后续 open() 落在"已打开"的端口上。先归位再关，阻断 closed() 误判。
        setState(SerialChannelState::Disconnected);
        m_transport->close();
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
