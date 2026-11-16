# ---- gayospace_platform：平台专属收敛层（唯一允许平台 API 的目录，TS-SW 第 11 节）----
# v1.0 已知内容：崩溃转储钩子（Win32 MiniDumpWriteDump）。
add_library(gayospace_platform INTERFACE)
target_compile_features(gayospace_platform INTERFACE cxx_std_20)
