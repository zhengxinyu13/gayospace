# ---- 单元测试：Catch2 v3（core）+ Qt Test（信号槽/Qt API 相关）----
# TDD 纪律（DoD 2.1）：业务代码合入前，本目录的脚手架与空实现测试必须已存在并先红后绿。

add_executable(gayospace-tests-core
    test_version_smoke.cpp
    test_serial_params.cpp
    test_serial_reconnect_policy.cpp
)
target_link_libraries(gayospace-tests-core PRIVATE gayospace_core Catch2::Catch2WithMain)
# 期望版本由构建系统注入，测试断言版本注入链路一致
target_compile_definitions(gayospace-tests-core PRIVATE GAYO_EXPECTED_VERSION="${PROJECT_VERSION}")
catch_discover_tests(gayospace-tests-core)

# Qt Test 冒烟需要 Widgets/完整 Qt（core-only 探针模式跳过，R-15）
if(NOT GAYOSPACE_CORE_ONLY)
add_executable(gayospace-tests-qt
    tst_smoke_qt.cpp
)
target_link_libraries(gayospace-tests-qt PRIVATE gayospace_core Qt6::Core Qt6::Test)
add_test(NAME gayospace-tests-qt COMMAND gayospace-tests-qt)
# CI 无显示环境：Qt 测试统一离屏平台
set_tests_properties(gayospace-tests-qt PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
