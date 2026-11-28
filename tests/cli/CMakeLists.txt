# ---- CLI 单元测试（批次①起；链接 gayospace-cli-core 静态库）----
add_executable(gayospace-tests-cli
    test_cli_options.cpp
    test_send_codec.cpp
    test_capture_writer.cpp
    test_capture_session.cpp
    test_port_lister.cpp
)
target_link_libraries(gayospace-tests-cli PRIVATE gayospace-cli-core Catch2::Catch2WithMain)
catch_discover_tests(gayospace-tests-cli)
