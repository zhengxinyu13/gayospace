// 串口参数校验与错误映射单测（TS-SW §8；CODE-REVIEW 第 5 轮验收要求 1：串口参数校验、错误映射）。
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "serial/SerialError.h"
#include "serial/SerialParams.h"

using namespace gayospace::serial;

// ---- SerialParams 校验 ----

TEST_CASE("default params are invalid until port name is set", "[serial][params]")
{
    SerialParams p;
    // 默认构造无端口名——必须显式配置才有效（宁可显式报错，不静默猜端口）
    REQUIRE_FALSE(p.isValid());
    REQUIRE_FALSE(p.validationErrors().isEmpty());
    REQUIRE(p.portName.isEmpty());
    REQUIRE(p.baudRate == 115200);

    p.portName = "COM3";
    REQUIRE(p.isValid());
    REQUIRE(p.validationErrors().isEmpty());
}

TEST_CASE("empty port name is invalid", "[serial][params]")
{
    SerialParams p;
    p.portName = "";
    REQUIRE_FALSE(p.isValid());
    REQUIRE_FALSE(p.validationErrors().isEmpty());

    p.portName = "   ";
    REQUIRE_FALSE(p.isValid()); // 纯空白视同空

    p.portName = " COM3 ";
    REQUIRE(p.validationErrors().isEmpty()); // 带首尾空格的端口名不在此层否定
}

TEST_CASE("non-positive baud rate is invalid", "[serial][params]")
{
    SerialParams p;
    p.portName = "COM3";
    p.baudRate = 0;
    REQUIRE_FALSE(p.isValid());

    p.baudRate = -115200;
    REQUIRE_FALSE(p.isValid());

    p.baudRate = 115200;
    REQUIRE(p.isValid());
}

TEST_CASE("validation errors are listed independently", "[serial][params]")
{
    SerialParams p;
    p.portName = "";
    p.baudRate = 0;
    REQUIRE(p.validationErrors().size() == 2);
}

TEST_CASE("params equality is member-wise", "[serial][params]")
{
    SerialParams a;
    SerialParams b;
    REQUIRE(a == b);
    b.portName = "COM5";
    REQUIRE_FALSE(a == b);
}

// ---- 错误映射（QSerialPort::SerialPortError → 领域错误）----

TEST_CASE("qt errors map to domain kinds", "[serial][error]")
{
    using E = QSerialPort::SerialPortError;
    REQUIRE(mapQtError(E::NoError) == SerialErrorKind::None);
    REQUIRE(mapQtError(E::DeviceNotFoundError) == SerialErrorKind::DeviceNotFound);
    REQUIRE(mapQtError(E::PermissionError) == SerialErrorKind::AccessDenied);
    REQUIRE(mapQtError(E::OpenError) == SerialErrorKind::OpenFailed);
    REQUIRE(mapQtError(E::WriteError) == SerialErrorKind::Write);
    REQUIRE(mapQtError(E::ReadError) == SerialErrorKind::Read);
    REQUIRE(mapQtError(E::ResourceError) == SerialErrorKind::ResourceLost);
    REQUIRE(mapQtError(E::UnsupportedOperationError) == SerialErrorKind::Unsupported);
    REQUIRE(mapQtError(E::TimeoutError) == SerialErrorKind::Timeout);
    REQUIRE(mapQtError(E::NotOpenError) == SerialErrorKind::NotOpen);
}

TEST_CASE("occupied / unsupported / not-open are not retryable", "[serial][error]")
{
    using K = SerialErrorKind;
    // TS §8.3 硬要求：端口被占用不重试
    REQUIRE_FALSE(isRetryable(K::AccessDenied));
    REQUIRE_FALSE(isRetryable(K::Unsupported));
    REQUIRE_FALSE(isRetryable(K::NotOpen));
    REQUIRE_FALSE(isRetryable(K::None));

    REQUIRE(isRetryable(K::DeviceNotFound));
    REQUIRE(isRetryable(K::OpenFailed));
    REQUIRE(isRetryable(K::Read));
    REQUIRE(isRetryable(K::Write));
    REQUIRE(isRetryable(K::ResourceLost));
    REQUIRE(isRetryable(K::Timeout));
    REQUIRE(isRetryable(K::Unknown));
}

TEST_CASE("user messages are non-empty and occupied mentions sharing", "[serial][error]")
{
    using K = SerialErrorKind;
    const K all[] = {K::None, K::DeviceNotFound, K::AccessDenied, K::OpenFailed,
        K::Read, K::Write, K::ResourceLost, K::Unsupported, K::Timeout,
        K::NotOpen, K::Unknown};
    for (const auto k : all) {
        INFO(static_cast<int>(k));
        REQUIRE_FALSE(userMessage(k).isEmpty());
    }
    // CLI 端口占用提示契约（TS §6：打不开端口时注明"可能被 GUI 或其他程序占用"）
    REQUIRE_THAT(userMessage(K::AccessDenied).toStdString(),
        Catch::Matchers::ContainsSubstring("占用"));
}
