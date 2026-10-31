#pragma once

#include "Bytes.h"
#include "model/Preset.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qtn
{
/** The user IR stored with a preset: the 44-byte WAV header and 8192 bytes of samples. */
struct IrBlob
{
    Bytes wavHeader;
    Bytes samples;
};

/** One preset record of a `.nuxpreset` file (R4 §4). The WAV header and IR samples are kept as
    read, even when the has-IR flag is 0, so a rewrite reproduces the file byte for byte. */
struct PresetRecord
{
    static constexpr size_t wavHeaderSize = 44;
    static constexpr size_t irSampleSize = 8192;

    uint32_t recordVersion{1}; // first u32 of the record; QuickTone writes 1
    int32_t slot{};            // 0-35; -1 in QuickTone's template
    uint8_t irFlag{};          // QuickTone writes 1 when preset byte 0x57 <= 10, else 0
    Preset preset;
    Bytes wavHeader = Bytes (wavHeaderSize);
    Bytes irSamples = Bytes (irSampleSize);

    [[nodiscard]] bool hasIr() const noexcept { return irFlag != 0; }
    /** The IR when the record says it has one, else std::nullopt. */
    [[nodiscard]] std::optional<IrBlob> ir() const;
};

enum class PresetFileKind
{
    single,   // one record, data length 0x20A0
    all,      // 36 records, data length 0x49688
    allLegacy // 36 records with 102-byte presets, data length 0x49664; converted to 103 bytes
};

struct PresetFileContents
{
    PresetFileKind kind{};
    std::vector<PresetRecord> records;
};

/** Outcome of PresetFile::read: `contents` on success, otherwise `error` says why. */
struct PresetFileReadResult
{
    std::optional<PresetFileContents> contents;
    std::string error;
};

/** Reads and writes QuickTone's `.nuxpreset` files (juce::ConfigLoader container, key
    "MG300 Preset Data"; R4 §4). Pure byte-level code: no file or device access. */
namespace PresetFile
{
inline constexpr std::string_view key = "MG300 Preset Data";
inline constexpr size_t allPresetsRecordCount = 36;

/** Parses a whole file. Any size, key or tag mismatch gives an error, never a crash. */
[[nodiscard]] PresetFileReadResult read (std::span<const uint8_t> file);

/** A single-preset file; std::nullopt when the record's WAV header or IR area has the wrong size. */
[[nodiscard]] std::optional<Bytes> writeSingle (const PresetRecord& record);

/** An all-presets file; std::nullopt unless there are exactly 36 well-formed records. */
[[nodiscard]] std::optional<Bytes> writeAll (std::span<const PresetRecord> records);
} // namespace PresetFile
} // namespace qtn
