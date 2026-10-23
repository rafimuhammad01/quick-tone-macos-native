# QuickTone Native

Native arm64 macOS editor for the first-gen NUX MG-300, rebuilt in C++20 + JUCE 8.

## Requirements

- macOS 14.0 or later, Apple silicon (arm64 only)
- Xcode Command Line Tools
- `brew install cmake ninja clang-format`

## Pinned dependencies

Fetched by CMake `FetchContent` at configure time:

| Dependency | Tag |
|---|---|
| JUCE | `8.0.15` |
| Catch2 | `v3.16.1` |

Tool versions used to set up the scaffold: CMake 4.4.4, Ninja 1.13.2, clang-format 23.1.4, Apple clang 21.0.0.

## Build and test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The app bundle is built at `build/app/QuickToneNative_artefacts/Debug/QuickTone Native.app`.

## Targets

- `QuickToneNative`: the GUI app (bundle id `local.quicktone-native`)
- `qtn_core`: static library with everything that is not UI
- `qtn_tests`: Catch2 unit tests, linked to `qtn_core`

## Formatting

Run `clang-format -i` with the repo's `.clang-format` on every C++ file you change.
