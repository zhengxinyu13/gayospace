// QSerialPort 后端的 ISerialTransport 实现。
// 线程亲和（TS-SW §3.1）：本类实例与其内部 QSerialPort 必须同线程构造、同线程使用；
// SerialWorker（后续 PR）保证在 worker 线程内创建，主线程只经队列连接下发命令。
// 本类不做重连决策——错误只上报，策略在 SerialChannel。
#pragma once

#include <QSerialPort>

#include "ISerialTransport.h"

namespace gayospace::serial {

class QSerialTransport final : public ISerialTransport {
    Q_OBJECT
public:
    explicit QSerialTransport(QObject* parent = nullptr);
    ~QSerialTransport() override = default;

    bool open(const SerialParams& params) override;
    void close() override;
    bool isOpen() const override;
    qint64 writeBytes(const QByteArray& data) override;

private:
    QSerialPort* m_port; // 与本对象同线程亲和（父对象绑定）
};

} // namespace gayospace::serial
