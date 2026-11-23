// PipelineThread 实现（薄封装，不单测；见头文件注释）。
#include <QMutexLocker>

#include "applogic/PipelineThread.h"

namespace gayospace::applogic {

PipelineThread::PipelineThread(PipelinePump& pump, QMutex& mutex, QWaitCondition& cond,
    QObject* parent)
    : QThread(parent), pump_(pump), mutex_(mutex), cond_(cond)
{
}

void PipelineThread::stop()
{
    // R-96（第 26 轮评审）：持锁置位 + wakeAll，与 R-90 写侧口径统一——
    // run 线程"复测 stopped → 进入 wait"之间到达的唤醒不会丢失（无锁时最坏多等 50ms 兜底）。
    QMutexLocker locker(&mutex_);
    stopped_.storeRelaxed(1);
    cond_.wakeAll();
}

void PipelineThread::run()
{
    while (!stopped_.loadRelaxed()) {
        pump_.drain();
        mutex_.lock();
        if (!stopped_.loadRelaxed()) {
            cond_.wait(&mutex_, 50); // 写侧 notify 唤醒；50ms 兜底防丢唤醒
        }
        mutex_.unlock();
    }
    pump_.drain(); // 停止时排空（评审 §2.2.5）
}

} // namespace gayospace::applogic
