cmake_minimum_required (VERSION 3.28)

set (CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architectures" FORCE)
set (CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version" FORCE)

project (QuickToneNative VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)

include (FetchContent)

FetchContent_Declare (juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)

FetchContent_Declare (Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable (juce Catch2)

# Warnings for the project's own targets only (not third-party code).
set (QTN_WARNING_FLAGS -Wall -Wextra -Werror)

# Folder made by R6 (tools/name_assets.py): names.csv, nme300.txt and named/<originalFilename>.
# Kept out of the repository. The NME-300 files are copied into the app bundle at build time.
set (QTN_ASSETS_DIR "${PROJECT_SOURCE_DIR}/assets/extracted" CACHE PATH "Extracted QuickTone assets (R6 output)")
set (QTN_ASSET_MANIFEST "${PROJECT_BINARY_DIR}/asset-manifest.csv")

add_subdirectory (app)

enable_testing ()
add_subdirectory (tests)
