#include "backup/BackupStore.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

namespace
{
namespace fs = std::filesystem;

/** A fresh, empty folder per test case, deleted afterwards. Folders made read-only with
    makeUnwritable() get their permissions back first, so they can be deleted. */
class TempDir
{
  public:
    TempDir()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getNonexistentChildFile ("qtn-backup-test", "", false))
    {
        REQUIRE (folder.createDirectory().wasOk());
    }

    ~TempDir()
    {
        for (const auto& locked : lockedFolders)
            fs::permissions (locked.getFullPathName().toStdString(), fs::perms::owner_all, fs::perm_options::replace);

        folder.deleteRecursively();
    }

    TempDir (const TempDir&) = delete;
    TempDir& operator= (const TempDir&) = delete;

    [[nodiscard]] juce::File get() const { return folder; }

    void makeUnwritable (const juce::File& dir)
    {
        lockedFolders.push_back (dir);
        fs::permissions (dir.getFullPathName().toStdString(),
                         fs::perms::owner_read | fs::perms::owner_exec,
                         fs::perm_options::replace);
    }

  private:
    juce::File folder;
    std::vector<juce::File> lockedFolders;
};

// JUCE months are 0-based: month 9 is October.
juce::Time at (int minute, int second)
{
    return juce::Time (2026, 9, 24, 13, minute, second);
}

std::vector<qtn::PresetBlob> samplePresets()
{
    return {
        {0, "Clean", {0xF0, 0x00, 0x11, 0xF7}},
        {7, "Crunch \xC3\xA9\xE2\x9C\x93", {0x00, 0xFF, 0x7F}}, // UTF-8 "Crunch é✓"
        {199, "", {}},
    };
}

void checkSamePresets (const std::vector<qtn::PresetBlob>& actual, const std::vector<qtn::PresetBlob>& expected)
{
    REQUIRE (actual.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        CHECK (actual[i].slot == expected[i].slot);
        CHECK (actual[i].name == expected[i].name);
        CHECK (actual[i].raw == expected[i].raw);
    }
}
} // namespace

TEST_CASE ("save_then_load_roundtrip")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());
    const auto presets = samplePresets();

    const auto result = store.save (presets, at (5, 9));

    REQUIRE (result.ok);
    CHECK (result.error.isEmpty());
    checkSamePresets (store.load (result.folder), presets);
}

TEST_CASE ("save_writes_timestamped_folder_with_slot_files_and_manifest")
{
    TempDir temp;
    qtn::BackupStore store (temp.get().getChildFile ("Backups"));

    const auto result = store.save ({{3, "Lead", {'a', 'b', 'c'}}}, at (5, 9));

    REQUIRE (result.ok);
    CHECK (result.folder == temp.get().getChildFile ("Backups/2026-10-24T13-05-09"));
    CHECK (result.folder.getChildFile ("slot-003.bin").existsAsFile());
    CHECK (! temp.get().getChildFile ("Backups/2026-10-24T13-05-09.partial").exists());

    const auto manifest = juce::JSON::parse (result.folder.getChildFile ("manifest.json"));
    REQUIRE (manifest.isArray());
    REQUIRE (manifest.size() == 1);
    CHECK (static_cast<int> (manifest[0]["slot"]) == 3);
    CHECK (manifest[0]["name"].toString() == "Lead");
    CHECK (manifest[0]["sha256"].toString() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE ("list_returns_newest_first")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());
    REQUIRE (store.save (samplePresets(), at (5, 10)).ok);
    REQUIRE (store.save (samplePresets(), at (5, 30)).ok);
    REQUIRE (store.save (samplePresets(), at (5, 20)).ok);

    const auto backups = store.list();

    REQUIRE (backups.size() == 3);
    CHECK (backups[0].getFileName() == "2026-10-24T13-05-30");
    CHECK (backups[1].getFileName() == "2026-10-24T13-05-20");
    CHECK (backups[2].getFileName() == "2026-10-24T13-05-10");
}

TEST_CASE ("keeps_only_newest_50")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());

    for (int i = 0; i < 52; ++i)
        REQUIRE (store.save (samplePresets(), at (i / 60, i % 60)).ok);

    const auto backups = store.list();
    REQUIRE (backups.size() == 50);
    CHECK (backups.front().getFileName() == "2026-10-24T13-00-51");
    CHECK (backups.back().getFileName() == "2026-10-24T13-00-02");
    CHECK (! temp.get().getChildFile ("2026-10-24T13-00-00").exists());
    CHECK (! temp.get().getChildFile ("2026-10-24T13-00-01").exists());
}

TEST_CASE ("partial_folder_not_listed")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());
    REQUIRE (store.save (samplePresets(), at (5, 9)).ok);

    const auto partial = temp.get().getChildFile ("2026-10-24T13-06-00.partial");
    REQUIRE (partial.createDirectory().wasOk());
    REQUIRE (partial.getChildFile ("manifest.json").replaceWithText ("[]"));
    const auto noManifest = temp.get().getChildFile ("2026-10-24T13-07-00");
    REQUIRE (noManifest.createDirectory().wasOk());
    REQUIRE (noManifest.getChildFile ("slot-000.bin").replaceWithText ("x"));

    const auto backups = store.list();

    REQUIRE (backups.size() == 1);
    CHECK (backups[0].getFileName() == "2026-10-24T13-05-09");
}

TEST_CASE ("save_failure_reports_error")
{
    TempDir temp;
    temp.makeUnwritable (temp.get());
    qtn::BackupStore store (temp.get());

    const auto result = store.save (samplePresets(), at (5, 9));

    CHECK_FALSE (result.ok);
    CHECK (result.error.isNotEmpty());
    CHECK (store.list().empty());
}

TEST_CASE ("save_rejects_slot_out_of_range")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());

    const auto negative = store.save ({{-1, "A", {1}}}, at (5, 9));
    const auto tooBig = store.save ({{qtn::BackupStore::maxSlot + 1, "B", {1}}}, at (5, 10));

    CHECK_FALSE (negative.ok);
    CHECK (negative.error.isNotEmpty());
    CHECK_FALSE (tooBig.ok);
    CHECK (tooBig.error.isNotEmpty());
    CHECK (store.list().empty());
}

TEST_CASE ("save_accepts_highest_slot")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());
    const std::vector<qtn::PresetBlob> presets{{qtn::BackupStore::maxSlot, "Last", {9}}};

    const auto result = store.save (presets, at (5, 9));

    REQUIRE (result.ok);
    CHECK (result.folder.getChildFile ("slot-999.bin").existsAsFile());
    checkSamePresets (store.load (result.folder), presets);
}

TEST_CASE ("save_rejects_duplicate_slots")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());

    const auto result = store.save ({{4, "A", {1}}, {4, "B", {2}}}, at (5, 9));

    CHECK_FALSE (result.ok);
    CHECK (result.error.isNotEmpty());
    CHECK (store.list().empty());
}

TEST_CASE ("save_in_same_second_reports_error_and_keeps_first_backup")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());
    const auto presets = samplePresets();
    const auto first = store.save (presets, at (5, 9));
    REQUIRE (first.ok);

    const auto second = store.save ({{1, "Other", {5}}}, at (5, 9));

    CHECK_FALSE (second.ok);
    CHECK (second.error.isNotEmpty());
    REQUIRE (store.list().size() == 1);
    checkSamePresets (store.load (first.folder), presets);
}

TEST_CASE ("load_rejects_corrupted_slot_file")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());
    const auto result = store.save (samplePresets(), at (5, 9));
    REQUIRE (result.ok);

    REQUIRE (result.folder.getChildFile ("slot-007.bin").replaceWithText ("tampered"));

    CHECK (store.load (result.folder).empty());
}

TEST_CASE ("load_rejects_missing_slot_file")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());
    const auto result = store.save (samplePresets(), at (5, 9));
    REQUIRE (result.ok);

    REQUIRE (result.folder.getChildFile ("slot-000.bin").deleteFile());

    CHECK (store.load (result.folder).empty());
}

TEST_CASE ("load_rejects_folder_without_manifest")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());
    const auto result = store.save (samplePresets(), at (5, 9));
    REQUIRE (result.ok);

    REQUIRE (result.folder.getChildFile ("manifest.json").deleteFile());

    CHECK (store.load (result.folder).empty());
}

TEST_CASE ("save_discards_leftover_partial_folder")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());
    const auto leftover = temp.get().getChildFile ("2026-10-24T13-05-09.partial");
    REQUIRE (leftover.createDirectory().wasOk());
    REQUIRE (leftover.getChildFile ("slot-050.bin").replaceWithText ("stale"));

    const auto result = store.save (samplePresets(), at (5, 9));

    REQUIRE (result.ok);
    CHECK (! result.folder.getChildFile ("slot-050.bin").exists());
    checkSamePresets (store.load (result.folder), samplePresets());
}
