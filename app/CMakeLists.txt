# Everything that is not UI lives in qtn_core, linked by the app and the tests.
# qtn_core is also the one target that compiles the JUCE modules (JUCE's "shared code" pattern):
# modules are linked PRIVATE and their include dirs / definitions are re-exported, so the app and
# the tests see the headers without compiling the module sources a second time.
add_library (qtn_core STATIC
    src/Version.cpp
    src/app/AppController.cpp
    src/backup/BackupStore.cpp
    src/backup/ExportCompare.cpp
    src/device/Allowlist.cpp
    src/device/Connector.cpp
    src/device/DeviceInfo.cpp
    src/device/DeviceSession.cpp
    src/device/MidiBytes.cpp
    src/device/PortConnection.cpp
    src/device/SysexQueue.cpp
    src/ir/IrConvert.cpp
    src/ir/IrData.cpp
    src/ir/IrFrames.cpp
    src/model/DeviceState.cpp
    src/model/EffectCatalog.cpp
    src/model/Preset.cpp
    src/model/PresetFile.cpp
    src/proto/Codec.cpp)
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
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags)
target_include_directories (qtn_core INTERFACE
    $<TARGET_PROPERTY:qtn_core,INCLUDE_DIRECTORIES>)
target_compile_definitions (qtn_core INTERFACE
    $<TARGET_PROPERTY:qtn_core,COMPILE_DEFINITIONS>)

# UI building blocks that unit tests need (assets, fonts, look-and-feel). Uses the JUCE headers
# re-exported by qtn_core; the module code itself is compiled once, in qtn_core.
add_library (qtn_ui STATIC
    src/ui/laf/AssetLibrary.cpp
    src/ui/laf/AssetManifest.cpp
    src/ui/laf/Language.cpp
    src/ui/laf/NuxLookAndFeel.cpp
    src/ui/widgets/FaderComponent.cpp
    src/ui/widgets/FaderLogic.cpp
    src/ui/widgets/FFT.cpp
    src/ui/widgets/HarmonyLED.cpp
    src/ui/widgets/NuxFader.cpp
    src/ui/widgets/NuxImageButton.cpp
    src/ui/widgets/NuxKnob.cpp
    src/ui/widgets/NuxPlistButton.cpp
    src/ui/widgets/NuxToggle.cpp
    src/ui/widgets/NuxVolume.cpp
    src/ui/widgets/IRFader.cpp
    src/ui/widgets/PatchLevel.cpp
    src/ui/widgets/SettingVolume.cpp)
target_compile_options (qtn_ui PRIVATE ${QTN_WARNING_FLAGS})
target_link_libraries (qtn_ui PUBLIC qtn_core)

juce_add_gui_app (QuickToneNative
    PRODUCT_NAME "QuickTone Native"
    COMPANY_NAME "local"
    BUNDLE_ID "local.quicktone-native"
    VERSION ${PROJECT_VERSION})

target_sources (QuickToneNative PRIVATE
    src/Main.cpp
    src/app/StatusComponent.cpp
    src/device/JuceMidiTransport.cpp)

# The allowlist is embedded so the app cannot load a different one at run time.
juce_add_binary_data (QuickToneNativeData SOURCES resources/allowlist.json)

target_compile_definitions (QuickToneNative PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:QuickToneNative,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:QuickToneNative,JUCE_VERSION>")

target_compile_options (QuickToneNative PRIVATE ${QTN_WARNING_FLAGS})

target_link_libraries (QuickToneNative
    PRIVATE
        qtn_core
        qtn_ui
        QuickToneNativeData
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

include (${CMAKE_CURRENT_SOURCE_DIR}/cmake/QtnAssets.cmake)
qtn_bundle_assets (QuickToneNative "${QTN_ASSETS_DIR}" "${QTN_ASSET_MANIFEST}")
