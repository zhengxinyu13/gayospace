// 自动重连退避策略单测（TS-SW §8.3：指数退避 1s → 2s → 4s → 5s（封顶）× 共 10 次）。
// 纯逻辑类，无 Qt 依赖，秒级数值直接断言（默认策略），时序梯度可在测试中注入。
#include <catch2/catch_test_macros.hpp>

#include "serial/ReconnectPolicy.h"

using namespace gayospace::serial;

TEST_CASE("default policy follows 1s-2s-4s-5s-cap backoff", "[serial][reconnect]")
{
    const auto p = ReconnectPolicy::defaultPolicy();
    REQUIRE(p.maxAttempts == 10);
    REQUIRE(p.delayBefore(1) == std::chrono::seconds{1});
    REQUIRE(p.delayBefore(2) == std::chrono::seconds{2});
    REQUIRE(p.delayBefore(3) == std::chrono::seconds{4});
    REQUIRE(p.delayBefore(4) == std::chrono::seconds{5});
    REQUIRE(p.delayBefore(10) == std::chrono::seconds{5});
    REQUIRE(p.delayBefore(11) == std::chrono::milliseconds{0}); // 超界防御
    REQUIRE(p.delayBefore(0) == std::chrono::milliseconds{0});
}

TEST_CASE("policy exhaustion is at maxAttempts", "[serial][reconnect]")
{
    const auto p = ReconnectPolicy::defaultPolicy();
    REQUIRE_FALSE(p.exhausted(9));
    REQUIRE(p.exhausted(10));
}

TEST_CASE("injected policy uses its own step delays and cap", "[serial][reconnect]")
{
    ReconnectPolicy p;
    p.stepDelays = {std::chrono::milliseconds{1}, std::chrono::milliseconds{2},
        std::chrono::milliseconds{4}};
    p.capDelay = std::chrono::milliseconds{5};
    p.maxAttempts = 10;

    REQUIRE(p.delayBefore(1) == std::chrono::milliseconds{1});
    REQUIRE(p.delayBefore(2) == std::chrono::milliseconds{2});
    REQUIRE(p.delayBefore(3) == std::chrono::milliseconds{4});
    REQUIRE(p.delayBefore(4) == std::chrono::milliseconds{5});
    REQUIRE(p.delayBefore(10) == std::chrono::milliseconds{5});
}
