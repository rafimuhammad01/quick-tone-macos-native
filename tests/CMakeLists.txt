list (APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
add_executable (qtn_tests
    smoke_test.cpp
    backup_test.cpp
    transport_test.cpp
    port_connection_test.cpp
    sysex_queue_test.cpp
    session_test.cpp)
target_link_libraries (qtn_tests PRIVATE qtn_core Catch2::Catch2WithMain)
target_compile_options (qtn_tests PRIVATE ${QTN_WARNING_FLAGS})

include (Catch)
catch_discover_tests (qtn_tests)
