# ---- gayospace-cli：headless 前端（TS-SW 第 6 节 CLI 契约的宿主）----
# cli-core：参数解析与校验（批次①）+ 落盘器（批次②），静态库供 exe 与 tests/cli 共用——
# 可执行目标不能被测试链接，故拆库（DoD 2.1 测试先行）。
add_library(gayospace-cli-core STATIC
    CliOptions.h
    CliOptions.cpp
    CaptureWriter.h
    CaptureWriter.cpp
    CaptureSession.h
    CaptureSession.cpp
)
# include 根 = src/：与 core 的 "serial/…" 引用风格一致（#include "cli/CliOptions.h"）
target_include_directories(gayospace-cli-core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)
# gayospace_version.h 生成于 ${CMAKE_BINARY_DIR}/generated（顶层 CMakeLists configure_file）
target_include_directories(gayospace-cli-core
    PUBLIC ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(gayospace-cli-core PUBLIC gayospace_core gayospace_platform Qt6::Core)

add_executable(gayospace-cli
    main.cpp
)
target_link_libraries(gayospace-cli PRIVATE gayospace-cli-core gayospace_core Qt6::Core)
