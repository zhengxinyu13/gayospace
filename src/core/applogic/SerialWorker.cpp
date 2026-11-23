// SerialWorker 实现（语义见头文件与 tests/unit/test_serial_worker.cpp 用例注释）。
#include "applogic/SerialWorker.h"

#include <cstring>

namespace gayospace::applogic {

SerialWorker::SerialWorker(std::unique_ptr<serial::ISerialTransport> transport,
    pipeline::SpscRingBuffer& ring, QObject* parent)
    : QObject(parent), ring_(ring)
{
    // 同线程连接（worker 内构造 → 自动 Direct）：bytesReceived 同步驱动 onBytes
    channel_ = new serial::SerialChannel(transport.release(), this);
    connect(channel_, &serial::SerialChannel::bytesReceived, this, &SerialWorker::onBytes);
}

SerialWorker::~SerialWorker() = default;

void SerialWorker::bindWakeCondition(QMutex* mutex, QWaitCondition* cond)
{
    wakeMutex_ = mutex;
    wakeCond_ = cond;
}

void SerialWorker::openPort(const serial::SerialParams& params)
{
    channel_->open(params);
}

void SerialWorker::closePort()
{
    channel_->close();
}

void SerialWorker::onBytes(const QByteArray& data)
{
    if (data.isEmpty()) {
        return;
    }
    // write 返回实际写入数；满载被丢的新字节由 ring 内部计 rxDropped（不重复计数）
    std::size_t written = 0;
    if (!data.isEmpty()) {
        written = ring_.write(reinterpret_cast<const std::byte*>(data.constData()),
            static_cast<std::size_t>(data.size()));
    }
    Q_UNUSED(written);
    // 写侧 notify：解析线程条件变量唤醒（未绑定时为 no-op——单测直连场景）
    if (wakeMutex_ && wakeCond_) {
        wakeCond_->wakeAll();
    }
}

} // namespace gayospace::applogic
