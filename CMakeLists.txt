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

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Test)
qt_standard_project_setup()

include(FetchContent)
# Catch2 v3（BSL-1.0）。选型定死：FetchContent 拉取，不依赖 vcpkg（TS-SW 第 1 节）。
# R-12：GIT_TAG 锁定 commit SHA（tag 可移动，供应链风险）+ GIT_SHALLOW 减少网络量
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        9827c148c397289df17d3967692619a198032a24  # v3.7.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(catch2)
# Catch2 v3 的 catch_discover_tests 命令由 extras/Catch.cmake 提供，FetchContent 不自动引入
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

# ---- 统一编译选项（R-05）----
# 位置刻意在 FetchContent_MakeAvailable 之后：add_compile_options 只影响其后创建的
# target（自有代码），Catch2 等第三方 target 不受 /W4 /WX 影响。
# /utf-8：源码为 UTF-8 无 BOM 且含中文注释，MSVC 无此开关按活动代码页解析（CI 出 C4819，
# 中文字面量可能乱码甚至尾字节撞 0x5C 语法错误）。
if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)  # gcc/clang 暂不开 -Werror，随 Linux CI 稳定后再开
endif()

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
