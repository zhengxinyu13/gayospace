# ---- GayoSpace：GUI 前端（TS-SW 第 5 节；Glassmorphism 随 spike 落地）----
add_executable(GayoSpace WIN32
    main.cpp
)
target_link_libraries(GayoSpace PRIVATE gayospace_core Qt6::Widgets)
