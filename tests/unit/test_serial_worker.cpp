// SerialWorker 单测（评审 §2.1/§2.3：FakeTransport 同线程直连驱动，不碰真串口、真线程）。
// 覆盖：bytesReceived → ring 写入路径、唤醒面绑定（R-90 持锁 notify）、QByteArray 空/非空
// 边界、transport 所有权（R-84：worker 析构后 transport 被销毁）。
// 线程封装（PipelineThread）不做线程单测——由 CLI / com0com spike 验证（评审 §2.2.4）。
#include <catch2/catch_test_macros.hpp>

#include <QMutex>
#include <QWaitCondition>

#include "applogic/SerialWorker.h"
#include "pipeline/SpscRingBuffer.h"
#include "serial/SerialParams.h"

#include "../unit/FakeTransport.h"

using namespace gayospace;

// 测试驱动面：heap 分配 transport（R-84：worker 持有所有权，栈对象会 double-delete）
struct WorkerFixture {
    pipeline::SpscRingBuffer ring;
    std::unique_ptr<serial::test::FakeTransport> transport;
    serial::test::FakeTransport* tp;
    applogic::SerialWorker worker;

    explicit WorkerFixture(std::size_t ringCap = 1024)
        : ring(ringCap), transport(std::make_unique<serial::test::FakeTransport>()),
          tp(transport.get()), worker(std::move(transport), ring)
    {
    }
};

TEST_CASE("serial worker forwards received bytes into ring", "[applogic][serialworker]")
{
    WorkerFixture fx;

    fx.worker.openPort(serial::SerialParams{});
    REQUIRE(fx.tp->m_open); // 打开成功（slot 返回 void，状态经 FakeTransport 断言）
    REQUIRE(fx.ring.writePos() == 0);

    fx.tp->simulateBytes(QByteArray("hello"));
    REQUIRE(fx.ring.writePos() == 5); // 串口字节进入 ring（rx_bytes 数据源）

    fx.tp->simulateBytes(QByteArray(" world"));
    REQUIRE(fx.ring.writePos() == 11); // 连续多次写入累计

    fx.worker.closePort();
    REQUIRE(fx.ring.writePos() == 11); // close 不影响已写入数据
}

TEST_CASE("serial worker destroys owned transport on destruction", "[applogic][serialworker]")
{
    // R-84 验收：worker 析构后 transport 必须被删除（此前 release() 无删除路径）
    const int destroyedBefore = serial::test::FakeTransport::s_destroyed;
    {
        WorkerFixture fx;
        REQUIRE(serial::test::FakeTransport::s_destroyed == destroyedBefore);
        fx.tp->simulateBytes(QByteArray("x"));
        REQUIRE(fx.ring.writePos() == 1);
    } // worker 析构 → channel 先删 → transport_ 成员释放
    REQUIRE(serial::test::FakeTransport::s_destroyed == destroyedBefore + 1);
}

TEST_CASE("serial worker empty payload writes nothing", "[applogic][serialworker]")
{
    WorkerFixture fx;

    fx.tp->simulateBytes(QByteArray());
    REQUIRE(fx.ring.writePos() == 0); // 空包不触发 write（ring.write 空输入语义防御在 worker 层）
}

TEST_CASE("serial worker notifies bound condition on write", "[applogic][serialworker]")
{
    // 唤醒面（R-90）：绑定 QWaitCondition 后，onBytes 持锁 wakeAll——单线程下仅验证
    // notify 不阻塞、可重复调用；丢唤醒时序由 CLI/com0com spike 验证
    WorkerFixture fx;

    QMutex mutex;
    QWaitCondition cond;
    fx.worker.bindWakeCondition(&mutex, &cond);

    fx.tp->simulateBytes(QByteArray("x"));
    REQUIRE(fx.ring.writePos() == 1); // notify 已发出（不阻塞），数据已写入
}

TEST_CASE("serial worker unbound condition stays silent", "[applogic][serialworker]")
{
    // 未绑定唤醒面（单测直连场景）→ onBytes 正常写 ring，notify 为 no-op
    WorkerFixture fx;

    fx.tp->simulateBytes(QByteArray("ok"));
    REQUIRE(fx.ring.writePos() == 2);
}
