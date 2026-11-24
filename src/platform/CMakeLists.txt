# ---- gayospace_platform：平台专属收敛层（唯一允许平台 API 的目录，TS-SW 第 11 节）----
# v1.0 已知内容：崩溃转储钩子（Win32 MiniDumpWriteDump，未落地）、replaceFile 原子替换
# （批次② R-103：Windows MoveFileEx / POSIX rename，见 FileOps.cpp）。
# 批次②起改为编译型库：源码含平台分支，必须保持 core-only 构建可用
# （R-108 教训：Windows API 全部 #ifdef _WIN32 包裹，Linux job 也要编过）。
add_library(gayospace_platform STATIC
    FileOps.h
    FileOps.cpp
    Interrupt.h
    Interrupt.cpp
)
target_compile_features(gayospace_platform PUBLIC cxx_std_20)
# include 根 = src/：与 core/cli 的 "platform/FileOps.h" 引用风格一致
target_include_directories(gayospace_platform
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(gayospace_platform PUBLIC Qt6::Core)

if(WIN32)
    # MoveFileExW 所在（kernel32 默认已链，显式声明以防工具链差异）
    target_link_libraries(gayospace_platform PRIVATE kernel32)
endif()
