// 最小串口传输接口（CODE-REVIEW 第 5 轮 `feature/serial-core` 验收要求 2：
// "把 QSerialPort 藏在一个最小传输接口后面，单测用假实现驱动重连与错误路径"）。
// 事件通过信号上报；SerialChannel 只依赖本接口，不依赖 QSerialPort，
// 使重连与错误路径可在无真实端口环境下单元测试。
#pragma once

#include <QByteArray>
#include <QObject>
#include <QSerialPort>
#include <QtGlobal>

#include "SerialError.h"
#include "SerialParams.h"

namespace gayospace::serial {

class ISerialTransport : public QObject {
    Q_OBJECT
public:
    explicit ISerialTransport(QObject* parent = nullptr) : QObject(parent) {}
    ~ISerialTransport() override = default;

    // 同步打开/关闭。open 失败时同步发出 errorOccurred。
    virtual bool open(const SerialParams& params) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    // 写入字节；返回写入数（负数 = 失败，语义对齐 QIODevice::write）。
    virtual qint64 writeBytes(const QByteArray& data) = 0;

signals:
    // 打开成功（已进入可收发状态）。
    void opened();
    // 传输层错误（含打开失败）。kind 已映射为领域错误。
    void errorOccurred(gayospace::serial::SerialErrorKind kind, const QString& message);
    // 收到原始字节（顺序 = 设备到达顺序）。
    void bytesReceived(const QByteArray& data);
    // 连接已断开（传输层视角：拔线/设备消失/被抢占）。
    void closed();
};

} // namespace gayospace::serial
