# ---- gayospace_core：无 UI、无平台 API 的核心层（TS-SW 第 2 节分层铁律）----
add_library(gayospace_core STATIC
    applogic/Version.cpp
)
target_include_directories(gayospace_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC  ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(gayospace_core PUBLIC Qt6::Core)
