// SerialWorker 实现（语义见头文件与 tests/unit/test_serial_worker.cpp 用例注释）。
#include "applogic/SerialWorker.h"

#include <cstring>

#include <QMutexLocker>

namespace gayospace::applogic {

SerialWorker::SerialWorker(std::unique_ptr<serial::ISerialTransport> transport,
    pipeline::SpscRingBuffer& ring, QObject* parent)
    : QObject(parent), ring_(ring), transport_(std::move(transport))
{
    // 同线程连接（worker 内构造 → 自动 Direct）：bytesReceived 同步驱动 onBytes。
    // R-84：channel 只借 transport_.get()（SerialChannel 不持有所有权）。
    channel_ = new serial::SerialChannel(transport_.get(), this);
    connect(channel_, &serial::SerialChannel::bytesReceived, this, &SerialWorker::onBytes);
}

SerialWorker::~SerialWorker()
{
    // R-84：先删 channel（脱离信号连接与 parent 链），再由 transport_ 成员析构释放
    // transport——顺序显式化，避免 channel 析构触碰已释放的 transport。
    delete channel_;
    channel_ = nullptr;
}

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
    // write 返回实际写入数；满载被丢的新字节由 ring 内部计 rxDropped（不重复计数）。
    // R-98（第 26 轮评审）：删冗余内层判空与未用返回值。
    static_cast<void>(ring_.write(reinterpret_cast<const std::byte*>(data.constData()),
        static_cast<std::size_t>(data.size())));
    // 写侧 notify（R-90，第 22 轮评审）：持锁后 wakeAll——解析线程在
    // "lock → 检查 stop → wait"之间不会错过本包的唤醒，最坏延迟从 50ms 兜底降为即时。
    // 未绑定唤醒面时为 no-op（单测直连场景）。
    if (wakeMutex_ && wakeCond_) {
        QMutexLocker locker(wakeMutex_);
        wakeCond_->wakeAll();
    }
}

} // namespace gayospace::applogic
