# ---- gayospace_core：无 UI、无平台 API 的核心层（TS-SW 第 2 节分层铁律）----
add_library(gayospace_core STATIC
    applogic/Version.cpp
    serial/SerialParams.cpp
    serial/SerialError.cpp
)
target_include_directories(gayospace_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC  ${CMAKE_BINARY_DIR}/generated
)
# SerialPort 属 Qt 模块而非平台 API（TS-SW §2 分层铁律 2：core 仅用 Qt Core/SerialPort + 标准库）
target_link_libraries(gayospace_core PUBLIC Qt6::Core Qt6::SerialPort)
