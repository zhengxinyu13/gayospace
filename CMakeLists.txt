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
configure_file(cmake/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Test)
qt_standard_project_setup()

include(FetchContent)
# Catch2 v3（BSL-1.0）。选型定死：FetchContent 拉取，不依赖 vcpkg（TS-SW 第 1 节）
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(catch2)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
