# Everything that is not UI lives in qtn_core, linked by the app and the tests.
# qtn_core is also the one target that compiles the JUCE modules (JUCE's "shared code" pattern):
# modules are linked PRIVATE and their include dirs / definitions are re-exported, so the app and
# the tests see the headers without compiling the module sources a second time.
add_library (qtn_core STATIC
    src/Version.cpp
    src/backup/BackupStore.cpp
    src/device/PortConnection.cpp
    src/device/SysexQueue.cpp)
target_include_directories (qtn_core PUBLIC src)
target_compile_features (qtn_core PUBLIC cxx_std_20)
target_compile_options (qtn_core PRIVATE ${QTN_WARNING_FLAGS})
target_compile_definitions (qtn_core PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)
target_link_libraries (qtn_core
    PRIVATE
        juce::juce_gui_extra
        juce::juce_cryptography
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags)
target_include_directories (qtn_core INTERFACE
    $<TARGET_PROPERTY:qtn_core,INCLUDE_DIRECTORIES>)
target_compile_definitions (qtn_core INTERFACE
    $<TARGET_PROPERTY:qtn_core,COMPILE_DEFINITIONS>)

juce_add_gui_app (QuickToneNative
    PRODUCT_NAME "QuickTone Native"
    COMPANY_NAME "local"
    BUNDLE_ID "local.quicktone-native"
    VERSION ${PROJECT_VERSION})

target_sources (QuickToneNative PRIVATE
    src/Main.cpp
    src/device/JuceMidiTransport.cpp)

target_compile_definitions (QuickToneNative PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:QuickToneNative,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:QuickToneNative,JUCE_VERSION>")

target_compile_options (QuickToneNative PRIVATE ${QTN_WARNING_FLAGS})

target_link_libraries (QuickToneNative
    PRIVATE
        qtn_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
