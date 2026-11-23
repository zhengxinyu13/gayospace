// 解析线程薄封装（评审 §2.2.4：线程循环极简，不做线程单测——由 CLI / com0com spike 验证）。
// 循环 = drain → 条件变量等待（写侧 SerialWorker notify 唤醒；50ms 兜底超时防丢唤醒）
// → stop 后末次排空。停止顺序（评审 §2.2.5）：stop() 唤醒 → run 内退出循环 → drain 排空
// → 线程结束；串口侧由 owner 层先 closePort（停读取）再 stop（TS §4.4 时序）。
#pragma once

#include <QMutex>
#include <QObject>
#include <QThread>
#include <QWaitCondition>

#include "applogic/PipelinePump.h"

namespace gayospace::applogic {

class PipelineThread : public QThread {
    Q_OBJECT
public:
    PipelineThread(PipelinePump& pump, QMutex& mutex, QWaitCondition& cond,
        QObject* parent = nullptr);

    // 线程安全：置位停止标志并唤醒等待中的解析线程（幂等）
    void stop();

protected:
    void run() override;

private:
    PipelinePump& pump_;
    QMutex& mutex_;
    QWaitCondition& cond_;
    QAtomicInt stopped_ {0};
};

} // namespace gayospace::applogic
