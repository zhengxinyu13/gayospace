// PipelineThread 实现（薄封装，不单测；见头文件注释）。
#include "applogic/PipelineThread.h"

namespace gayospace::applogic {

PipelineThread::PipelineThread(PipelinePump& pump, QMutex& mutex, QWaitCondition& cond,
    QObject* parent)
    : QThread(parent), pump_(pump), mutex_(mutex), cond_(cond)
{
}

void PipelineThread::stop()
{
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
