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

constexpr juce::int64 baseUtcMs = 1'792'846'800'000; // 2026-10-24T13:00:00.000Z

/** 2026-10-24 13:mm:ss.ms UTC. */
juce::Time at (int minute, int second, int ms = 0)
{
    return juce::Time (baseUtcMs + (minute * 60 + second) * 1000 + ms);
}

/** The backup folder name for at (minute, second, ms). */
juce::String nameAt (int minute, int second, int ms = 0)
{
    return "2026-10-24T13-" + juce::String (minute).paddedLeft ('0', 2) + "-" + juce::String (second).paddedLeft ('0', 2) + "." + juce::String (ms).paddedLeft ('0', 3) + "Z";
}

/** One preset whose bytes depend on `n`, so backups of different `n` differ. */
std::vector<qtn::PresetBlob> numberedPresets (int n)
{
    return {{0, "Clean", {static_cast<uint8_t> (n & 0x7F), static_cast<uint8_t> (n >> 7)}}};
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
    CHECK (result.folder == temp.get().getChildFile ("Backups/2026-10-24T13-05-09.000Z"));
    CHECK (result.folder.getChildFile ("slot-003.bin").existsAsFile());
    CHECK (! temp.get().getChildFile ("Backups/2026-10-24T13-05-09.000Z.partial").exists());

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
    REQUIRE (store.save (numberedPresets (1), at (5, 10)).ok);
    REQUIRE (store.save (numberedPresets (2), at (5, 30)).ok);
    REQUIRE (store.save (numberedPresets (3), at (5, 20)).ok);

    const auto backups = store.list();

    REQUIRE (backups.size() == 3);
    CHECK (backups[0].getFileName() == nameAt (5, 30));
    CHECK (backups[1].getFileName() == nameAt (5, 20));
    CHECK (backups[2].getFileName() == nameAt (5, 10));
}

TEST_CASE ("keeps_newest_50_and_the_first")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());

    for (int i = 0; i < 52; ++i)
        REQUIRE (store.save (numberedPresets (i), at (i / 60, i % 60)).ok);

    const auto backups = store.list();
    REQUIRE (backups.size() == 51);
    CHECK (backups.front().getFileName() == nameAt (0, 51));
    CHECK (backups[49].getFileName() == nameAt (0, 2));
    CHECK (backups.back().getFileName() == nameAt (0, 0)); // the first backup is never pruned
    CHECK (! temp.get().getChildFile (nameAt (0, 1)).exists());
}

TEST_CASE ("partial_folder_not_listed")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());
    REQUIRE (store.save (samplePresets(), at (5, 9)).ok);

    const auto partial = temp.get().getChildFile (nameAt (6, 0) + ".partial");
    REQUIRE (partial.createDirectory().wasOk());
    REQUIRE (partial.getChildFile ("manifest.json").replaceWithText ("[]"));
    const auto noManifest = temp.get().getChildFile (nameAt (7, 0));
    REQUIRE (noManifest.createDirectory().wasOk());
    REQUIRE (noManifest.getChildFile ("slot-000.bin").replaceWithText ("x"));

    const auto backups = store.list();

    REQUIRE (backups.size() == 1);
    CHECK (backups[0].getFileName() == nameAt (5, 9));
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

TEST_CASE ("save_in_same_millisecond_reports_error_and_keeps_first_backup")
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
    const auto leftover = temp.get().getChildFile (nameAt (5, 9) + ".partial");
    REQUIRE (leftover.createDirectory().wasOk());
    REQUIRE (leftover.getChildFile ("slot-050.bin").replaceWithText ("stale"));

    const auto result = store.save (samplePresets(), at (5, 9));

    REQUIRE (result.ok);
    CHECK (! result.folder.getChildFile ("slot-050.bin").exists());
    checkSamePresets (store.load (result.folder), samplePresets());
}

// --- Folder names in UTC with milliseconds (F1) -------------------------------------------------

TEST_CASE ("folder_name_is_utc_with_milliseconds")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());

    const auto result = store.save (samplePresets(), juce::Time (juce::int64{1'793'440'564'123}));

    REQUIRE (result.ok);
    CHECK (result.folder.getFileName() == "2026-10-31T09-56-04.123Z");
}

TEST_CASE ("saves_in_the_same_second_are_kept_apart_and_ordered")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());
    REQUIRE (store.save (numberedPresets (1), at (5, 9, 999)).ok);
    REQUIRE (store.save (numberedPresets (2), at (5, 10, 0)).ok);
    REQUIRE (store.save (numberedPresets (3), at (5, 9, 1)).ok);

    const auto backups = store.list();

    REQUIRE (backups.size() == 3);
    CHECK (backups[0].getFileName() == nameAt (5, 10, 0));
    CHECK (backups[1].getFileName() == nameAt (5, 9, 999));
    CHECK (backups[2].getFileName() == nameAt (5, 9, 1));
}

TEST_CASE ("unrelated_folder_in_root_is_ignored_and_not_pruned")
{
    TempDir temp;
    qtn::BackupStore store (temp.get(), 1);
    const auto first = store.save (numberedPresets (1), at (5, 0));
    REQUIRE (first.ok);

    // Folders with a manifest but a name that is not a backup timestamp.
    std::vector<juce::File> unrelated;
    for (const auto* name : {"Notes", "zzzz", "2026-10-24T13-05-09.000Z-copy", "9999-99-99T99-99-99.999Z"})
    {
        const auto dir = temp.get().getChildFile (name);
        REQUIRE (dir.createDirectory().wasOk());
        REQUIRE (first.folder.getChildFile ("manifest.json").copyFileTo (dir.getChildFile ("manifest.json")));
        REQUIRE (first.folder.getChildFile ("slot-000.bin").copyFileTo (dir.getChildFile ("slot-000.bin")));
        unrelated.push_back (dir);
    }

    for (int i = 2; i < 6; ++i)
        REQUIRE (store.save (numberedPresets (i), at (5, i)).ok);

    const auto backups = store.list();
    REQUIRE (backups.size() == 2); // the newest and the first
    CHECK (backups[0].getFileName() == nameAt (5, 5));
    CHECK (backups[1].getFileName() == nameAt (5, 0));
    for (const auto& dir : unrelated)
    {
        CAPTURE (dir.getFileName());
        CHECK (dir.getChildFile ("manifest.json").existsAsFile());
    }
}

TEST_CASE ("legacy_local_time_folder_is_still_listed_in_time_order")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());

    // A backup written by the previous build: named in local time, no milliseconds.
    const auto old = store.save (numberedPresets (4), at (4, 0));
    REQUIRE (old.ok);
    const auto legacy = temp.get().getChildFile (at (4, 0).formatted ("%Y-%m-%dT%H-%M-%S"));
    REQUIRE (old.folder.moveFileTo (legacy));

    REQUIRE (store.save (numberedPresets (3), at (3, 0)).ok);
    REQUIRE (store.save (numberedPresets (5), at (5, 0)).ok);

    const auto backups = store.list();
    REQUIRE (backups.size() == 3);
    CHECK (backups[0].getFileName() == nameAt (5, 0));
    CHECK (backups[1] == legacy);
    CHECK (backups[2].getFileName() == nameAt (3, 0));
    checkSamePresets (store.load (legacy), numberedPresets (4));
}

// --- Identical content reuses the newest backup; the first is never pruned (F1) -----------------

TEST_CASE ("identical_content_reuses_newest_backup")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());
    const auto first = store.save (samplePresets(), at (5, 0));
    REQUIRE (first.ok);

    const auto again = store.save (samplePresets(), at (6, 0));

    CHECK (again.ok);
    CHECK (again.error.isEmpty());
    CHECK (again.folder == first.folder);
    CHECK (store.list().size() == 1);
    CHECK_FALSE (temp.get().getChildFile (nameAt (6, 0)).exists());
}

TEST_CASE ("changed_content_writes_a_new_backup")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());
    REQUIRE (store.save (numberedPresets (1), at (5, 0)).ok);

    const auto changed = store.save (numberedPresets (2), at (6, 0));

    REQUIRE (changed.ok);
    CHECK (changed.folder.getFileName() == nameAt (6, 0));
    CHECK (store.list().size() == 2);
}

TEST_CASE ("damaged_newest_backup_is_not_reused")
{
    TempDir temp;
    qtn::BackupStore store (temp.get());
    const auto first = store.save (samplePresets(), at (5, 0));
    REQUIRE (first.ok);
    REQUIRE (first.folder.getChildFile ("slot-007.bin").replaceWithText ("tampered"));

    const auto again = store.save (samplePresets(), at (6, 0));

    REQUIRE (again.ok);
    CHECK (again.folder.getFileName() == nameAt (6, 0));
    checkSamePresets (store.load (again.folder), samplePresets());
}

TEST_CASE ("prune_never_deletes_the_first_backup")
{
    TempDir temp;
    qtn::BackupStore store (temp.get(), 2);
    for (int i = 0; i < 5; ++i)
        REQUIRE (store.save (numberedPresets (i), at (5, i)).ok);

    const auto backups = store.list();

    REQUIRE (backups.size() == 3);
    CHECK (backups[0].getFileName() == nameAt (5, 4));
    CHECK (backups[1].getFileName() == nameAt (5, 3));
    CHECK (backups[2].getFileName() == nameAt (5, 0));
}
