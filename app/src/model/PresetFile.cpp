#include "model/PresetFile.h"

#include <algorithm>
#include <array>

namespace qtn
{
namespace
{
constexpr uint32_t formatTag = 0x66;
constexpr size_t containerHeaderSize = 12; // file size, key length, data length
constexpr size_t recordHeaderSize = 13;    // version, slot, tag, has-IR flag
constexpr size_t allHeaderSize = 8;        // u32 1, u32 0x66
constexpr size_t legacyPresetSize = 102;

constexpr size_t recordSize (size_t presetSize)
{
    return recordHeaderSize + presetSize + PresetRecord::wavHeaderSize + PresetRecord::irSampleSize;
}

constexpr size_t singleDataSize = recordSize (Preset::size);
constexpr size_t allDataSize = allHeaderSize + PresetFile::allPresetsRecordCount * recordSize (Preset::size);
constexpr size_t legacyAllDataSize = allHeaderSize + PresetFile::allPresetsRecordCount * recordSize (legacyPresetSize);

static_assert (singleDataSize == 0x20A0);
static_assert (allDataSize == 0x49688);
static_assert (legacyAllDataSize == 0x49664);

// Legacy conversion (R4 §4, importAllPresetList): old USER cab 26 becomes 100, patch level 50.
constexpr uint8_t legacyUserCab = 26;
constexpr uint8_t userCab = 100;
constexpr uint8_t legacyPatchLevel = 50;

/** Bounds-checked little-endian reader over a byte span. */
class Reader
{
  public:
    explicit Reader (std::span<const uint8_t> bytesToRead) : bytes (bytesToRead) {}

    [[nodiscard]] size_t remaining() const noexcept { return bytes.size() - pos; }

    [[nodiscard]] std::optional<uint32_t> u32()
    {
        const auto chunk = take (4);
        if (! chunk)
            return std::nullopt;
        uint32_t value = 0;
        for (size_t i = 0; i < 4; ++i)
            value |= static_cast<uint32_t> ((*chunk)[i]) << (8 * i);
        return value;
    }

    [[nodiscard]] std::optional<std::span<const uint8_t>> take (size_t count)
    {
        if (count > remaining())
            return std::nullopt;
        const auto chunk = bytes.subspan (pos, count);
        pos += count;
        return chunk;
    }

  private:
    std::span<const uint8_t> bytes;
    size_t pos = 0;
};

void putU32 (Bytes& out, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
        out.push_back (static_cast<uint8_t> (value >> (8 * i)));
}

void putBytes (Bytes& out, std::span<const uint8_t> bytes)
{
    out.insert (out.end(), bytes.begin(), bytes.end());
}

PresetFileReadResult failure (std::string why)
{
    return {std::nullopt, std::move (why)};
}

/** One record; the caller has already checked that `reader` holds enough bytes. */
std::optional<PresetRecord> readRecord (Reader& reader, size_t presetSize, std::string& error)
{
    PresetRecord record;
    const auto version = reader.u32();
    const auto slot = reader.u32();
    const auto tag = reader.u32();
    const auto flag = reader.take (1);
    const auto presetBytes = reader.take (presetSize);
    const auto wav = reader.take (PresetRecord::wavHeaderSize);
    const auto ir = reader.take (PresetRecord::irSampleSize);
    if (! version || ! slot || ! tag || ! flag || ! presetBytes || ! wav || ! ir)
    {
        error = "record is truncated";
        return std::nullopt;
    }

    // QuickTone checks the tag on 103-byte records only; legacy records' tag is not documented.
    if (presetSize == Preset::size && *tag != formatTag)
    {
        error = "record format tag is " + std::to_string (*tag) + ", expected 102";
        return std::nullopt;
    }

    Bytes raw (presetBytes->begin(), presetBytes->end());
    const bool legacy = presetSize == legacyPresetSize;
    if (legacy)
        raw.push_back (legacyPatchLevel);

    auto preset = Preset::decode (raw);
    if (! preset)
    {
        error = "preset has the wrong size";
        return std::nullopt;
    }
    if (legacy && preset->type (Module::cab) == legacyUserCab)
        preset->setType (Module::cab, userCab); // encode() writes the converted value

    record.recordVersion = *version;
    record.slot = static_cast<int32_t> (*slot);
    record.irFlag = (*flag)[0];
    record.preset = *preset;
    record.wavHeader.assign (wav->begin(), wav->end());
    record.irSamples.assign (ir->begin(), ir->end());
    return record;
}

PresetFileReadResult readAll (Reader& reader, size_t presetSize, PresetFileKind kind)
{
    const auto version = reader.u32();
    const auto tag = reader.u32();
    if (! version || ! tag)
        return failure ("all-presets header is truncated");
    if (kind == PresetFileKind::all && *tag != formatTag)
        return failure ("all-presets format tag is " + std::to_string (*tag) + ", expected 102");

    PresetFileContents contents{kind, {}};
    contents.records.reserve (PresetFile::allPresetsRecordCount);
    for (size_t i = 0; i < PresetFile::allPresetsRecordCount; ++i)
    {
        std::string error;
        auto record = readRecord (reader, presetSize, error);
        if (! record)
            return failure ("record " + std::to_string (i) + ": " + error);
        contents.records.push_back (std::move (*record));
    }
    return {std::move (contents), {}};
}

bool wellFormed (const PresetRecord& record) noexcept
{
    return record.wavHeader.size() == PresetRecord::wavHeaderSize && record.irSamples.size() == PresetRecord::irSampleSize;
}

void putRecord (Bytes& out, const PresetRecord& record)
{
    putU32 (out, record.recordVersion);
    putU32 (out, static_cast<uint32_t> (record.slot));
    putU32 (out, formatTag);
    out.push_back (record.irFlag);
    putBytes (out, record.preset.encode());
    putBytes (out, record.wavHeader);
    putBytes (out, record.irSamples);
}

Bytes wrapInContainer (const Bytes& data)
{
    Bytes out;
    out.reserve (containerHeaderSize + PresetFile::key.size() + data.size());
    putU32 (out, static_cast<uint32_t> (containerHeaderSize + PresetFile::key.size() + data.size()));
    putU32 (out, static_cast<uint32_t> (PresetFile::key.size()));
    out.insert (out.end(), PresetFile::key.begin(), PresetFile::key.end());
    putU32 (out, static_cast<uint32_t> (data.size()));
    putBytes (out, data);
    return out;
}
} // namespace

std::optional<IrBlob> PresetRecord::ir() const
{
    if (! hasIr())
        return std::nullopt;
    return IrBlob{wavHeader, irSamples};
}

namespace PresetFile
{
PresetFileReadResult read (std::span<const uint8_t> file)
{
    Reader reader (file);
    const auto fileSize = reader.u32();
    const auto keyLength = reader.u32();
    if (! fileSize || ! keyLength)
        return failure ("file is too short for a container header");
    if (*fileSize != file.size())
        return failure ("size field says " + std::to_string (*fileSize) + " bytes, file has " + std::to_string (file.size()));
    if (*keyLength != key.size())
        return failure ("key length is " + std::to_string (*keyLength) + ", expected 17");

    const auto keyBytes = reader.take (key.size());
    if (! keyBytes || ! std::equal (keyBytes->begin(), keyBytes->end(), key.begin(), key.end()))
        return failure ("key is not \"MG300 Preset Data\"");

    const auto dataLength = reader.u32();
    if (! dataLength || *dataLength != reader.remaining())
        return failure ("data length does not match the file size");

    switch (*dataLength)
    {
    case singleDataSize:
    {
        std::string error;
        auto record = readRecord (reader, Preset::size, error);
        if (! record)
            return failure (error);
        return {PresetFileContents{PresetFileKind::single, {std::move (*record)}}, {}};
    }
    case allDataSize:
        return readAll (reader, Preset::size, PresetFileKind::all);
    case legacyAllDataSize:
        return readAll (reader, legacyPresetSize, PresetFileKind::allLegacy);
    default:
        return failure ("unknown data length " + std::to_string (*dataLength));
    }
}

std::optional<std::string> validateForImport (const PresetFileContents& contents)
{
    if (contents.kind == PresetFileKind::single)
        return std::nullopt;

    constexpr auto lastSlot = static_cast<int32_t> (allPresetsRecordCount) - 1;
    std::array<bool, allPresetsRecordCount> seen{};
    for (size_t i = 0; i < contents.records.size(); ++i)
    {
        const auto slot = contents.records[i].slot;
        const auto where = "record " + std::to_string (i) + ": slot " + std::to_string (slot);
        if (slot < 0 || slot > lastSlot)
            return where + " is outside 0-" + std::to_string (lastSlot);

        auto& slotSeen = seen[static_cast<size_t> (slot)];
        if (slotSeen)
            return where + " appears twice";
        slotSeen = true;
    }
    return std::nullopt;
}

std::optional<Bytes> writeSingle (const PresetRecord& record)
{
    if (! wellFormed (record))
        return std::nullopt;

    Bytes data;
    data.reserve (singleDataSize);
    putRecord (data, record);
    return wrapInContainer (data);
}

std::optional<Bytes> writeAll (std::span<const PresetRecord> records)
{
    if (records.size() != allPresetsRecordCount || ! std::all_of (records.begin(), records.end(), wellFormed))
        return std::nullopt;

    Bytes data;
    data.reserve (allDataSize);
    putU32 (data, 1);
    putU32 (data, formatTag);
    for (const auto& record : records)
        putRecord (data, record);
    return wrapInContainer (data);
}
} // namespace PresetFile
} // namespace qtn
