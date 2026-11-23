// 串口工作对象（评审 §2.1：SerialWorker 拥有 transport + SerialChannel，worker 线程内
// 构造使用——TS §3.1 线程亲和：channel 必须在其 QObject 所在线程收发信号）。
// 职责极薄：bytesReceived → ring.write（返回 0 时 ring 已计 rxDropped，不重复计数）
// → notify 条件变量唤醒解析线程。解析泵与线程循环在 PipelinePump/PipelineThread。
#pragma once

#include <memory>

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QWaitCondition>

#include "pipeline/SpscRingBuffer.h"
#include "serial/ISerialTransport.h"
#include "serial/SerialChannel.h"
#include "serial/SerialParams.h"

namespace gayospace::applogic {

class SerialWorker : public QObject {
    Q_OBJECT
public:
    // transport 所有权转移给 SerialChannel（channel 析构时随 parent 链管理）。
    // channel 以 this 为 parent，worker 线程内构造/销毁。
    explicit SerialWorker(std::unique_ptr<serial::ISerialTransport> transport,
        pipeline::SpscRingBuffer& ring, QObject* parent = nullptr);
    ~SerialWorker() override;

    // 绑定解析线程的等待面（对象由 owner 层创建；仅绑定一次）
    void bindWakeCondition(QMutex* mutex, QWaitCondition* cond);

public slots:
    void openPort(const serial::SerialParams& params);
    void closePort();

private slots:
    void onBytes(const QByteArray& data);

private:
    pipeline::SpscRingBuffer& ring_;
    serial::SerialChannel* channel_ = nullptr;
    QMutex* wakeMutex_ = nullptr;
    QWaitCondition* wakeCond_ = nullptr;
};

} // namespace gayospace::applogic
