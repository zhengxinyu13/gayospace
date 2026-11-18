cmake_minimum_required(VERSION 3.21)

project(GayoSpace
    VERSION 0.1.0
    DESCRIPTION "GayoSpace — open-source serial debugging tool with real-time waveforms"
    LANGUAGES CXX
)

# ---- 全局编译设定（TS-SW 第 1 节：C++20 / Qt 6.8 LTS / MSVC 2022 x64）----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译期注入版本宏（TS-SW 第 1 节：GAYOSPACE_VERSION 运行时可读出）
set(GAYOSPACE_BUILD_ID "dev" CACHE STRING "Build identifier injected into the binary")
configure_file(cmake/version.h.in ${CMAKE_BINARY_DIR}/generated/gayospace_version.h @ONLY)
# 自检：生成失败立即暴露，不在编译期以难懂的 C2065 呈现
if(NOT EXISTS ${CMAKE_BINARY_DIR}/generated/gayospace_version.h)
    message(FATAL_ERROR "gayospace_version.h 未生成，检查 configure_file 输入 cmake/version.h.in")
endif()

# ---- 跨平台结构探针开关（R-15）：ON 时只构建 core 与 core 单测，供 Linux CI job 使用 ----
option(GAYOSPACE_CORE_ONLY "Build only the core layer and its unit tests (cross-platform CI probe)" OFF)

if(GAYOSPACE_CORE_ONLY)
    # 探针只用发行版 Qt（无 Widgets、版本可低于 6.8）；core 对 6.8 的 API 一致性由 Windows job 兜底
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Test)
else()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Test)
endif()
qt_standard_project_setup()

include(FetchContent)
# Catch2 v3（BSL-1.0）。选型定死：FetchContent 拉取，不依赖 vcpkg（TS-SW 第 1 节）
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(catch2)
# Catch2 v3 的 catch_discover_tests 命令由 extras/Catch.cmake 提供，FetchContent 不自动引入
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/platform)
if(NOT GAYOSPACE_CORE_ONLY)
    add_subdirectory(src/cli)
    add_subdirectory(src/gui)
    add_subdirectory(src/app)
endif()
add_subdirectory(tests)
