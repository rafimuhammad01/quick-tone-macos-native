#include "model/PresetFile.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
constexpr size_t recordSize = 0x20A0;
constexpr size_t legacyRecordSize = 0x209F;
constexpr std::string_view key = "MG300 Preset Data";

void putU32 (qtn::Bytes& out, uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back (static_cast<uint8_t> (value >> shift));
}

/** A record built by hand from R4 §4, with a recognisable pattern in every area. */
qtn::Bytes syntheticRecord (uint32_t slot, bool withIr, size_t presetSize = qtn::Preset::size)
{
    qtn::Bytes out;
    putU32 (out, 1);
    putU32 (out, slot);
    putU32 (out, 0x66);
    out.push_back (withIr ? 1 : 0);
    for (size_t i = 0; i < presetSize; ++i)
        out.push_back (static_cast<uint8_t> ((i * 3 + slot) % 100));
    for (size_t i = 0; i < 44; ++i)
        out.push_back (withIr ? static_cast<uint8_t> (0xA0 + i) : 0);
    for (size_t i = 0; i < 8192; ++i)
        out.push_back (withIr ? static_cast<uint8_t> ((i * 7 + slot) & 0xFF) : 0);
    return out;
}

qtn::Bytes container (const qtn::Bytes& data)
{
    qtn::Bytes out;
    putU32 (out, static_cast<uint32_t> (12 + key.size() + data.size()));
    putU32 (out, static_cast<uint32_t> (key.size()));
    out.insert (out.end(), key.begin(), key.end());
    putU32 (out, static_cast<uint32_t> (data.size()));
    out.insert (out.end(), data.begin(), data.end());
    return out;
}

qtn::Bytes syntheticAllFile (size_t presetSize = qtn::Preset::size)
{
    qtn::Bytes data;
    putU32 (data, 1);
    putU32 (data, 0x66);
    for (uint32_t slot = 0; slot < 36; ++slot)
    {
        const auto record = syntheticRecord (slot, slot % 5 == 0, presetSize);
        data.insert (data.end(), record.begin(), record.end());
    }
    return container (data);
}

qtn::Bytes readWholeFile (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    return {std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>()};
}

/** The owner's backup, or an empty buffer when QTN_G0_BACKUP is not set or the file is missing. */
qtn::Bytes realBackup()
{
    const std::string path = QTN_G0_BACKUP;
    if (path.empty() || ! std::filesystem::exists (path))
        return {};
    return readWholeFile (path);
}
} // namespace

TEST_CASE ("single_file_roundtrip_byte_exact")
{
    const auto file = container (syntheticRecord (7, true));
    REQUIRE (file.size() == 8381);

    const auto result = qtn::PresetFile::read (file);
    REQUIRE (result.contents.has_value());
    CHECK (result.contents->kind == qtn::PresetFileKind::single);
    REQUIRE (result.contents->records.size() == 1);

    const auto written = qtn::PresetFile::writeSingle (result.contents->records.front());
    REQUIRE (written.has_value());
    CHECK (*written == file);
}

TEST_CASE ("all_presets_file_roundtrip_byte_exact")
{
    const auto file = syntheticAllFile();
    REQUIRE (file.size() == 300709);

    const auto result = qtn::PresetFile::read (file);
    REQUIRE (result.contents.has_value());
    CHECK (result.contents->kind == qtn::PresetFileKind::all);
    REQUIRE (result.contents->records.size() == 36);

    const auto written = qtn::PresetFile::writeAll (result.contents->records);
    REQUIRE (written.has_value());
    CHECK (*written == file);
}

TEST_CASE ("reader_exposes_record_fields")
{
    const auto result = qtn::PresetFile::read (syntheticAllFile());
    REQUIRE (result.contents.has_value());
    const auto& records = result.contents->records;
    REQUIRE (records.size() == 36);

    CHECK (records[0].slot == 0);
    CHECK (records[35].slot == 35);
    const auto raw3 = syntheticRecord (3, false);
    CHECK (records[3].preset.encode() == qtn::Bytes (raw3.begin() + 13, raw3.begin() + 13 + 103));

    const auto withIr = records[5].ir();
    REQUIRE (withIr.has_value());
    CHECK (withIr->wavHeader.size() == 44);
    CHECK (withIr->wavHeader.front() == 0xA0);
    CHECK (withIr->samples.size() == 8192);
    CHECK (withIr->samples[1] == static_cast<uint8_t> (7 + 5));
    CHECK_FALSE (records[4].ir().has_value());
}

TEST_CASE ("single_file_accepts_template_slot_minus_one")
{
    const auto result = qtn::PresetFile::read (container (syntheticRecord (0xFFFFFFFF, false)));
    REQUIRE (result.contents.has_value());
    CHECK (result.contents->records.front().slot == -1);
}

TEST_CASE ("reader_rejects_truncated_and_wrong_key")
{
    const auto file = syntheticAllFile();

    for (const size_t cut : {size_t{0}, size_t{3}, size_t{4}, size_t{20}, size_t{29}, size_t{1000}, file.size() - 1})
    {
        CAPTURE (cut);
        const qtn::Bytes truncated (file.begin(), file.begin() + static_cast<std::ptrdiff_t> (cut));
        const auto result = qtn::PresetFile::read (truncated);
        CHECK_FALSE (result.contents.has_value());
        CHECK_FALSE (result.error.empty());
    }

    auto wrongKey = file;
    wrongKey[8] = 'N';
    const auto result = qtn::PresetFile::read (wrongKey);
    CHECK_FALSE (result.contents.has_value());
    CHECK_FALSE (result.error.empty());

    auto hugeKeyLength = file;
    hugeKeyLength[4] = 0xFF;
    hugeKeyLength[7] = 0xFF;
    CHECK_FALSE (qtn::PresetFile::read (hugeKeyLength).contents.has_value());
}

TEST_CASE ("reader_rejects_oversized_and_inconsistent_lengths")
{
    auto oversized = syntheticAllFile();
    oversized.push_back (0);
    CHECK_FALSE (qtn::PresetFile::read (oversized).contents.has_value());

    // Header claims the extra byte too, but the data length no longer matches a known layout.
    auto padded = container (qtn::Bytes (100));
    CHECK_FALSE (qtn::PresetFile::read (padded).contents.has_value());

    auto wrongDataLength = syntheticAllFile();
    wrongDataLength[25] ^= 0x01;
    CHECK_FALSE (qtn::PresetFile::read (wrongDataLength).contents.has_value());
}

TEST_CASE ("reader_rejects_wrong_format_tag")
{
    auto single = container (syntheticRecord (1, false));
    single[29 + 8] = 0x65;
    CHECK_FALSE (qtn::PresetFile::read (single).contents.has_value());

    auto all = syntheticAllFile();
    all[29 + 4] = 0x65;
    CHECK_FALSE (qtn::PresetFile::read (all).contents.has_value());

    auto allRecord = syntheticAllFile();
    allRecord[29 + 8 + 3 * recordSize + 8] = 0x65;
    CHECK_FALSE (qtn::PresetFile::read (allRecord).contents.has_value());
}

TEST_CASE ("legacy_all_presets_file_converts_to_103_bytes")
{
    auto file = syntheticAllFile (102);
    REQUIRE (file.size() == 300673);
    // Slot 2's cab byte = 26 (old USER IR); conversion maps it to 100.
    file[29 + 8 + 2 * legacyRecordSize + 13 + 9] = 26;

    const auto result = qtn::PresetFile::read (file);
    REQUIRE (result.contents.has_value());
    CHECK (result.contents->kind == qtn::PresetFileKind::allLegacy);
    REQUIRE (result.contents->records.size() == 36);

    const auto& slot2 = result.contents->records[2];
    CHECK (slot2.slot == 2);
    CHECK (slot2.preset.type (qtn::Module::cab) == 100);
    CHECK (slot2.preset.patchLevel == 50);
    CHECK (slot2.preset.encode()[0x65] == static_cast<uint8_t> ((0x65 * 3 + 2) % 100));
    CHECK (result.contents->records[1].preset.type (qtn::Module::cab) == static_cast<uint8_t> ((9 * 3 + 1) % 100));

    const auto written = qtn::PresetFile::writeAll (result.contents->records);
    REQUIRE (written.has_value());
    CHECK (written->size() == 300709);
}

TEST_CASE ("writer_rejects_wrong_record_count_and_area_sizes")
{
    const auto result = qtn::PresetFile::read (syntheticAllFile());
    REQUIRE (result.contents.has_value());
    auto records = result.contents->records;

    records.pop_back();
    CHECK_FALSE (qtn::PresetFile::writeAll (records).has_value());

    auto record = result.contents->records.front();
    record.wavHeader.pop_back();
    CHECK_FALSE (qtn::PresetFile::writeSingle (record).has_value());
}

TEST_CASE ("real_backup_parses_36_records")
{
    const auto file = realBackup();
    if (file.empty())
        SKIP ("QTN_G0_BACKUP not set or file missing");

    const auto result = qtn::PresetFile::read (file);
    INFO (result.error);
    REQUIRE (result.contents.has_value());
    CHECK (result.contents->kind == qtn::PresetFileKind::all);
    REQUIRE (result.contents->records.size() == 36);
    for (int slot = 0; slot < 36; ++slot)
        CHECK (result.contents->records[static_cast<size_t> (slot)].slot == slot);
}

TEST_CASE ("real_backup_rewrites_byte_identical")
{
    const auto file = realBackup();
    if (file.empty())
        SKIP ("QTN_G0_BACKUP not set or file missing");

    const auto result = qtn::PresetFile::read (file);
    REQUIRE (result.contents.has_value());
    const auto written = qtn::PresetFile::writeAll (result.contents->records);
    REQUIRE (written.has_value());
    CHECK (*written == file);
}

TEST_CASE ("real_backup_names_decode")
{
    const auto file = realBackup();
    if (file.empty())
        SKIP ("QTN_G0_BACKUP not set or file missing");

    const auto result = qtn::PresetFile::read (file);
    REQUIRE (result.contents.has_value());
    std::string listing;
    for (const auto& record : result.contents->records)
    {
        CAPTURE (record.slot);
        const auto name = record.preset.name();
        REQUIRE (name.has_value());
        const auto irName = record.preset.irName();
        CHECK (record.hasIr() == record.preset.hasIrName());
        if (record.preset.hasIrName())
            CHECK (irName.has_value());
        listing += "\nslot " + std::to_string (record.slot) + " '" + *name + "' IR " + (irName ? "'" + *irName + "'" : std::string ("-"));
    }
    // Printed with the run so the decoded names can be reported.
    WARN ("decoded names:" << listing);
}
