// Catch2 冒烟用例：验证测试框架与版本注入链路（cmake/version.h.in → generated/version.h）。
// 这是"框架工程先于业务代码"的最小可验证单元（DoD 2.1）。

#include <catch2/catch_test_macros.hpp>

#include "applogic/Version.h"
#include "version.h"

TEST_CASE("version is injected at build time", "[version][smoke]")
{
    REQUIRE(gayospace::kVersion != nullptr);
    REQUIRE(std::string_view(gayospace::kVersion) == std::string_view(GAYO_EXPECTED_VERSION));
}

TEST_CASE("version string composes build id", "[version][smoke]")
{
    const QString v = gayospace::versionString();
    REQUIRE(v.contains(gayospace::kVersion));
    REQUIRE(v.contains(gayospace::kBuildId));
}
