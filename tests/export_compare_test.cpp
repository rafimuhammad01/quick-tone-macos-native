#include "backup/BackupStore.h"
#include "backup/ExportCompare.h"
#include "model/PresetFile.h"
#include "support/TempDir.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
using qtn::Bytes;
using qtn::test::TempDir;

/** Preset bytes for a slot: a recognisable 7-bit pattern. */
Bytes presetBytes (int slot)
{
    Bytes b (qtn::Preset::size);
    for (size_t i = 0; i < b.size(); ++i)
        b[i] = static_cast<uint8_t> ((i + static_cast<size_t> (slot) * 5) % 100);
    return b;
}

/** A backup of `count` slots in `root`, as the Connector writes it (plus the two extra blobs). */
juce::File saveBackup (const juce::File& root, int count = 72)
{
    std::vector<qtn::PresetBlob> blobs;
    for (int slot = 0; slot < count; ++slot)
        blobs.push_back ({slot, "", presetBytes (slot)});
    blobs.push_back ({100, "global-parameters", Bytes (17, 0x01)});
    blobs.push_back ({101, "edit-buffer", presetBytes (3)});

    qtn::BackupStore store (root);
    const auto result = store.save (blobs, juce::Time (2026, 9, 31, 12, 0, 0));
    REQUIRE (result.ok);
    return result.folder;
}

std::vector<qtn::PresetRecord> exportRecords()
{
    std::vector<qtn::PresetRecord> records (qtn::PresetFile::allPresetsRecordCount);
    for (size_t i = 0; i < records.size(); ++i)
    {
        records[i].slot = static_cast<int32_t> (i);
        records[i].preset = *qtn::Preset::decode (presetBytes (static_cast<int> (i)));
    }
    return records;
}

juce::File writeExport (const juce::File& folder, const std::vector<qtn::PresetRecord>& records)
{
    const auto bytes = qtn::PresetFile::writeAll (records);
    REQUIRE (bytes.has_value());
    const auto file = folder.getChildFile ("MG300AllPreset.nuxpreset");
    REQUIRE (file.replaceWithData (bytes->data(), bytes->size()));
    return file;
}

std::vector<int> differentSlots (const qtn::ExportComparison& c)
{
    std::vector<int> out;
    for (const auto& s : c.slots)
        if (! s.equal)
            out.push_back (s.slot);
    return out;
}

Bytes readWholeFile (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    return {std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>()};
}
} // namespace

TEST_CASE ("compare_identical_backup_and_export")
{
    TempDir dir;
    const auto backup = saveBackup (dir.get().getChildFile ("backups"));
    const auto file = writeExport (dir.get(), exportRecords());

    const auto result = qtn::compareWithExport (backup, file);

    CHECK (result.error.isEmpty());
    REQUIRE (result.slots.size() == 36);
    for (int slot = 0; slot < 36; ++slot)
    {
        CAPTURE (slot);
        CHECK (result.slots[static_cast<size_t> (slot)].slot == slot);
        CHECK (result.slots[static_cast<size_t> (slot)].equal);
    }
    CHECK (qtn::describe (result) == "36/36 identical");
}

TEST_CASE ("compare_lists_differing_slots")
{
    TempDir dir;
    const auto backup = saveBackup (dir.get().getChildFile ("backups"));
    auto records = exportRecords();
    records[0].preset.raw[0x00] ^= 0x01; // unknown bytes: encode() keeps them
    records[35].preset.raw[0x63] ^= 0x01;
    const auto file = writeExport (dir.get(), records);

    const auto result = qtn::compareWithExport (backup, file);

    CHECK (differentSlots (result) == std::vector<int>{0, 35});
    CHECK (qtn::describe (result) == "34/36 identical; different: slots 0, 35");
}

TEST_CASE ("compare_uses_record_slot_not_record_order")
{
    TempDir dir;
    const auto backup = saveBackup (dir.get().getChildFile ("backups"));
    auto records = exportRecords();
    std::swap (records[4], records[9]);
    const auto file = writeExport (dir.get(), records);

    CHECK (differentSlots (qtn::compareWithExport (backup, file)).empty());
}

TEST_CASE ("compare_treats_slot_missing_from_backup_as_different")
{
    TempDir dir;
    const auto backup = saveBackup (dir.get().getChildFile ("backups"), 30);
    const auto file = writeExport (dir.get(), exportRecords());

    CHECK (differentSlots (qtn::compareWithExport (backup, file)) == std::vector<int>{30, 31, 32, 33, 34, 35});
}

TEST_CASE ("compare_treats_slot_missing_from_export_as_different")
{
    TempDir dir;
    const auto backup = saveBackup (dir.get().getChildFile ("backups"));
    auto records = exportRecords();
    records[7].slot = 6; // slot 6 twice, slot 7 never
    const auto file = writeExport (dir.get(), records);

    CHECK (differentSlots (qtn::compareWithExport (backup, file)) == std::vector<int>{6, 7});
}

TEST_CASE ("compare_reports_unreadable_export")
{
    TempDir dir;
    const auto backup = saveBackup (dir.get().getChildFile ("backups"));
    const auto file = dir.get().getChildFile ("junk.nuxpreset");
    REQUIRE (file.replaceWithText ("not a preset file"));

    const auto result = qtn::compareWithExport (backup, file);
    CHECK (result.slots.empty());
    CHECK_FALSE (result.error.isEmpty());
    CHECK (qtn::describe (result).startsWith ("Compare failed: "));

    CHECK_FALSE (qtn::compareWithExport (backup, dir.get().getChildFile ("missing.nuxpreset")).error.isEmpty());
}

TEST_CASE ("compare_reports_unreadable_backup")
{
    TempDir dir;
    const auto file = writeExport (dir.get(), exportRecords());

    const auto result = qtn::compareWithExport (dir.get().getChildFile ("no-backup"), file);
    CHECK (result.slots.empty());
    CHECK (result.error == "Backup could not be read");
}

TEST_CASE ("compare_real_export_with_backup_built_from_it")
{
    const std::string path = QTN_G0_BACKUP;
    if (path.empty() || ! std::filesystem::exists (path))
        SKIP ("QTN_G0_BACKUP not set or file missing");

    const auto parsed = qtn::PresetFile::read (readWholeFile (path));
    REQUIRE (parsed.contents.has_value());

    // What a pedal read would store if the pedal holds exactly what QuickTone exported.
    std::vector<qtn::PresetBlob> blobs;
    for (const auto& record : parsed.contents->records)
        blobs.push_back ({record.slot, "", Bytes (record.preset.raw.begin(), record.preset.raw.end())});
    for (int slot = 36; slot < 72; ++slot)
        blobs.push_back ({slot, "", presetBytes (slot)});

    TempDir dir;
    qtn::BackupStore store (dir.get());
    const auto saved = store.save (blobs, juce::Time (2026, 9, 31, 12, 0, 0));
    REQUIRE (saved.ok);

    const auto result = qtn::compareWithExport (saved.folder, juce::File (juce::String (path)));
    CHECK (qtn::describe (result) == "36/36 identical");

    // One changed byte in the backup is found.
    blobs[12].raw[20] ^= 0x01;
    const auto changed = store.save (blobs, juce::Time (2026, 9, 31, 12, 0, 1));
    REQUIRE (changed.ok);
    CHECK (differentSlots (qtn::compareWithExport (changed.folder, juce::File (juce::String (path)))) == std::vector<int>{parsed.contents->records[12].slot});
}
