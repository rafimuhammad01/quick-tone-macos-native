# Everything that is not UI lives in qtn_core, linked by the app and the tests.
add_library (qtn_core STATIC
    src/Version.cpp)
target_include_directories (qtn_core PUBLIC src)
target_compile_features (qtn_core PUBLIC cxx_std_20)
target_compile_options (qtn_core PRIVATE ${QTN_WARNING_FLAGS})

juce_add_gui_app (QuickToneNative
    PRODUCT_NAME "QuickTone Native"
    COMPANY_NAME "local"
    BUNDLE_ID "local.quicktone-native"
    VERSION ${PROJECT_VERSION})

target_sources (QuickToneNative PRIVATE
    src/Main.cpp)

target_compile_definitions (QuickToneNative PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:QuickToneNative,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:QuickToneNative,JUCE_VERSION>")

target_compile_options (QuickToneNative PRIVATE ${QTN_WARNING_FLAGS})

target_link_libraries (QuickToneNative
    PRIVATE
        qtn_core
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
