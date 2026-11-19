// 测试用假传输实现（CODE-REVIEW 第 5 轮验收要求 2：单测用假实现驱动重连与错误路径）。
// 仅供单元测试链接，不进 gayospace_core。
#pragma once

#include <QByteArray>

#include "serial/ISerialTransport.h"
#include "serial/SerialError.h"
#include "serial/SerialParams.h"

namespace gayospace::serial::test {

class FakeTransport final : public ISerialTransport {
    Q_OBJECT
public:
    explicit FakeTransport(QObject* parent = nullptr) : ISerialTransport(parent) {}

    bool open(const SerialParams& params) override
    {
        m_params = params;
        if (m_nextOpenError != SerialErrorKind::None) {
            const auto kind = m_nextOpenError;
            m_nextOpenError = SerialErrorKind::None; // 一次性注入
            emit errorOccurred(kind, userMessage(kind));
            return false;
        }
        m_open = true;
        ++m_openCount;
        emit opened();
        return true;
    }

    void close() override
    {
        if (!m_open) {
            return;
        }
        m_open = false;
        ++m_closeCount;
        emit closed();
    }

    bool isOpen() const override { return m_open; }

    qint64 writeBytes(const QByteArray& data) override
    {
        if (m_writeFails) {
            return -1;
        }
        m_lastWritten = data;
        return data.size();
    }

    // ---- 测试注入面 ----
    SerialErrorKind m_nextOpenError = SerialErrorKind::None; // 下一次 open 失败的错误类型
    bool m_writeFails = false;
    int m_openCount = 0;
    int m_closeCount = 0;
    QByteArray m_lastWritten;
    SerialParams m_params;
    bool m_open = false;

    // ---- 测试驱动面 ----
    void simulateDisconnect(SerialErrorKind kind = SerialErrorKind::ResourceLost)
    {
        m_open = false;
        emit errorOccurred(kind, userMessage(kind));
        emit closed();
    }

    // 仅注入错误、端口保持打开（模拟 Timeout/Write 等瞬态错误——R-54 场景）
    void simulateError(SerialErrorKind kind)
    {
        emit errorOccurred(kind, userMessage(kind));
    }

    void simulateBytes(const QByteArray& data) { emit bytesReceived(data); }
};

} // namespace gayospace::serial::test
