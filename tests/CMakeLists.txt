list (APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
add_executable (qtn_tests
    smoke_test.cpp
    backup_test.cpp
    transport_test.cpp
    port_connection_test.cpp
    sysex_queue_test.cpp
    midi_bytes_test.cpp
    session_test.cpp
    codec_test.cpp
    preset_test.cpp
    effect_catalog_test.cpp
    preset_file_test.cpp
    device_state_test.cpp
    connector_test.cpp
    assets_test.cpp
    look_and_feel_test.cpp
    app_controller_test.cpp
    export_compare_test.cpp
    capture_replay_test.cpp
    effect_widgets_test.cpp
    graph_widgets_test.cpp
    widgets_test.cpp
    ir_frames_test.cpp
    ir_convert_test.cpp)
target_link_libraries (qtn_tests PRIVATE qtn_ui qtn_core Catch2::Catch2WithMain)
target_compile_options (qtn_tests PRIVATE ${QTN_WARNING_FLAGS})

# Optional path to the owner's real all-presets export. It is never copied into the repo; the tests
# that need it are skipped when it is empty or missing.
set (QTN_G0_BACKUP "" CACHE FILEPATH "Owner's MG300AllPreset.nuxpreset, for real-data tests")
target_compile_definitions (qtn_tests PRIVATE
    QTN_G0_BACKUP="${QTN_G0_BACKUP}"
    QTN_RESOURCES_DIR="${PROJECT_SOURCE_DIR}/app/resources"
    QTN_ASSETS_DIR="${QTN_ASSETS_DIR}"
    QTN_ASSET_MANIFEST="${QTN_ASSET_MANIFEST}")

# Optional folder of the owner's decoded MIDI Monitor captures (research/captures). Never copied
# into the repo; the capture tests are skipped when it is empty or missing.
set (QTN_CAPTURES_DIR "" CACHE PATH "Folder with decoded captures (001-connect.txt), for real-data tests")
target_compile_definitions (qtn_tests PRIVATE QTN_CAPTURES_DIR="${QTN_CAPTURES_DIR}")

include (Catch)
catch_discover_tests (qtn_tests)
