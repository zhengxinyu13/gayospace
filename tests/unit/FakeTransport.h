// 测试用假传输实现（CODE-REVIEW 第 5 轮验收要求 2：单测用假实现驱动重连与错误路径）。
// 仅供单元测试链接，不进 gayospace_core。
#pragma once

#include <QByteArray>
#include <QList>

#include "serial/ISerialTransport.h"
#include "serial/SerialError.h"
#include "serial/SerialParams.h"

namespace gayospace::serial::test {

class FakeTransport final : public ISerialTransport {
    Q_OBJECT
public:
    // R-84（第 22 轮评审）：析构计数——验证 SerialWorker 释放 transport 所有权
    inline static int s_destroyed = 0;
    ~FakeTransport() override { ++s_destroyed; }
    explicit FakeTransport(QObject* parent = nullptr) : ISerialTransport(parent) {}

    bool open(const SerialParams& params) override
    {
        // R-57：closeOnOpen 模拟 QSerialTransport::open 的"先关旧口再开"（同步发 closed()），
        // 使重配路径的回归测试真正覆盖 handleTransportClosed 的误判场景——
        // 不开此开关时 Fake 旧口保持打开，测试在修复前的代码上也会通过（假回归保护）。
        if (m_closeOnOpen && m_open) {
            close();
        }
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
        // R-185：!m_open 时早退（closed() 不重发）——断线后再 close() 是 no-op，
        // 用例不得期待 closeCount + 1（与真实 QSerialPort::close() 语义一致）
        if (!m_open) {
            return;
        }
        m_open = false;
        ++m_closeCount;
        // R-168：对齐真实 QSerialPort::close——丢弃未确认数据且不再为它们发
        // bytesWritten（保留 m_bytesWrittenTotal 历史累计，供"断线前确已确认 N 字节"断言）
        m_pendingWrittenBytes = 0;
        emit closed();
    }

    bool isOpen() const override { return m_open; }

    qint64 writeBytes(const QByteArray& data) override
    {
        // R-177：对齐 QIODevice::write——未打开返回 -1 且不产生 pending
        // （"断线后仍在写"必须在 fake 层暴露，真实传输此路径必失败）
        if (!m_open) {
            return -1;
        }
        if (m_writeFails) {
            return -1;
        }
        // 零进展注入（R-142 看门狗用例）：恒返 0、不产生待确认字节
        if (m_writeReturnsZero) {
            return 0;
        }
        // 部分写注入（R-166）：-1 = 全收；>=0 = 本次最多接受 n 字节
        // TODO(batch3): 由 SendQueue 部分写用例消费 —— 见 TS §6.1 第 4 条 / R-166（R-183）
        const qint64 accept = (m_writeAcceptLimit >= 0)
            ? qMin<qint64>(data.size(), m_writeAcceptLimit) : data.size();
        m_writeCalls.append(data);
        m_bytesAcceptedTotal += accept;
        m_lastWritten = data.left(accept);
        // 写确认改为手动推进（批次③）：payload 记入 pending，pump(n) 才发 bytesWritten——
        // 无事件循环环境下测试可精确控制"写了但系统没确认"的窗口
        m_pendingWrittenBytes += accept;
        return accept;
    }

    // ---- 测试注入面 ----
    SerialErrorKind m_nextOpenError = SerialErrorKind::None; // 下一次 open 失败的错误类型
    bool m_writeFails = false;
    bool m_closeOnOpen = false; // open 时先关旧口（R-57：模拟 QSerialTransport 行为）
    int m_openCount = 0;
    int m_closeCount = 0;
    QByteArray m_lastWritten;
    SerialParams m_params;
    bool m_open = false;

    // ---- 测试驱动面 ----
    void simulateDisconnect(SerialErrorKind kind = SerialErrorKind::ResourceLost)
    {
        m_open = false;
        // R-168：断线丢弃未确认数据（同 close()；m_bytesWrittenTotal 历史累计保留）
        m_pendingWrittenBytes = 0;
        emit errorOccurred(kind, userMessage(kind));
        emit closed();
    }

    // 仅注入错误、端口保持打开（模拟 Timeout/Write 等瞬态错误——R-54 场景）。
    // R-185：勿用它测"断线清队列"——瞬态错误不丢缓冲、不发 closed()；
    // 断线清队列凭 closed() 信号（§8.1），测它请用 simulateDisconnect()/close()
    void simulateError(SerialErrorKind kind)
    {
        emit errorOccurred(kind, userMessage(kind));
    }

    void simulateBytes(const QByteArray& data) { emit bytesReceived(data); }

    // ---- 写推进面（批次③ SendQueue 排空用例）----
    bool m_writeReturnsZero = false;    // writeBytes 恒返 0（零进展注入，R-142）
    qint64 m_writeAcceptLimit = -1;     // 部分写注入（R-166）：-1 = 全收；>=0 = 每次最多接受 n 字节
    qint64 m_pendingWrittenBytes = 0;   // 已写但未确认的字节
    qint64 m_bytesWrittenTotal = 0;     // 累计已确认字节
    QList<QByteArray> m_writeCalls;     // 逐次入参（R-178：续写/重发判据——第二次入参必须
                                        //   是 payload.mid(accept)，不能只留最后一片）
    qint64 m_bytesAcceptedTotal = 0;    // 累计被接受字节 = Σ writeBytes 返回值（R-178：
                                        //   "不丢不重"断言 = == payload.size()。
                                        //   勿用 m_bytesWrittenTotal + m_pendingWrittenBytes
                                        //   当总量断言——那是已接受总量，属循环论证）

    // 手动推进：确认 n 字节已被系统收走 → 发 bytesWritten(n)（n 超出 pending 时截断）。
    // R-172：本函数是同步 emit——SendQueue 槽在本调用栈内重入执行，与真实 Qt 的
    // 事件循环派发次序不同；写"回调内再次入队"类用例时注意这一差异，防假通过。
    void pump(qint64 n)
    {
        n = qMin(n, m_pendingWrittenBytes);
        if (n <= 0) {
            return;
        }
        m_pendingWrittenBytes -= n;
        m_bytesWrittenTotal += n;
        emit bytesWritten(n);
    }

    // 跳过截断直接注入陈旧确认（R-176 用例：归零后迟到的 bytesWritten 必须被
    // 写路径丢弃/clamp 到 0——pump() 的截断恰好吃掉这个场景，故另开入口）
    void pumpRaw(qint64 n)
    {
        m_bytesWrittenTotal += n;
        emit bytesWritten(n);
    }
};

} // namespace gayospace::serial::test
