# ---- gayospace-cli：headless 前端（TS-SW 第 6 节 CLI 契约的宿主）----
add_executable(gayospace-cli
    main.cpp
)
target_link_libraries(gayospace-cli PRIVATE gayospace_core Qt6::Core)
