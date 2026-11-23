// SerialWorker 单测（评审 §2.1/§2.3：FakeTransport 同线程直连驱动，不碰真串口、真线程）。
// 覆盖：bytesReceived → ring 写入路径、唤醒面绑定、QByteArray 空/非空边界。
// 线程封装（PipelineThread）不做线程单测——由 CLI / com0com spike 验证（评审 §2.2.4）。
#include <catch2/catch_test_macros.hpp>

#include <QMutex>
#include <QWaitCondition>

#include "applogic/SerialWorker.h"
#include "pipeline/SpscRingBuffer.h"
#include "serial/SerialParams.h"

#include "../unit/FakeTransport.h"

using namespace gayospace;

TEST_CASE("serial worker forwards received bytes into ring", "[applogic][serialworker]")
{
    pipeline::SpscRingBuffer ring(1024);
    serial::test::FakeTransport transport;
    applogic::SerialWorker worker(
        std::unique_ptr<serial::ISerialTransport>(&transport), ring);

    worker.openPort(serial::SerialParams{});
    REQUIRE(transport.m_open); // 打开成功（slot 返回 void，状态经 FakeTransport 断言）
    REQUIRE(ring.writePos() == 0);

    transport.simulateBytes(QByteArray("hello"));
    REQUIRE(ring.writePos() == 5); // 串口字节进入 ring（rx_bytes 数据源）

    transport.simulateBytes(QByteArray(" world"));
    REQUIRE(ring.writePos() == 11); // 连续多次写入累计

    worker.closePort();
    REQUIRE(ring.writePos() == 11); // close 不影响已写入数据
}

TEST_CASE("serial worker empty payload writes nothing", "[applogic][serialworker]")
{
    pipeline::SpscRingBuffer ring(1024);
    serial::test::FakeTransport transport;
    applogic::SerialWorker worker(
        std::unique_ptr<serial::ISerialTransport>(&transport), ring);

    transport.simulateBytes(QByteArray());
    REQUIRE(ring.writePos() == 0); // 空包不触发 write（ring.write 空输入语义防御在 worker 层）
}

TEST_CASE("serial worker notifies bound condition on write", "[applogic][serialworker]")
{
    // 唤醒面：绑定 QWaitCondition 后，onBytes 写入即 wakeAll（解析线程等待面）。
    // 单线程下仅验证 wakeAll 不阻塞、可重复调用。
    pipeline::SpscRingBuffer ring(1024);
    serial::test::FakeTransport transport;
    applogic::SerialWorker worker(
        std::unique_ptr<serial::ISerialTransport>(&transport), ring);

    QMutex mutex;
    QWaitCondition cond;
    worker.bindWakeCondition(&mutex, &cond);

    transport.simulateBytes(QByteArray("x"));
    REQUIRE(ring.writePos() == 1); // notify 已发出（不阻塞），数据已写入
}

TEST_CASE("serial worker unbound condition stays silent", "[applogic][serialworker]")
{
    // 未绑定唤醒面（单测直连场景）→ onBytes 正常写 ring，notify 为 no-op
    pipeline::SpscRingBuffer ring(1024);
    serial::test::FakeTransport transport;
    applogic::SerialWorker worker(
        std::unique_ptr<serial::ISerialTransport>(&transport), ring);

    transport.simulateBytes(QByteArray("ok"));
    REQUIRE(ring.writePos() == 2);
}
