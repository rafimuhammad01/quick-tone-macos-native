#pragma once

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <vector>

namespace qtn::test
{
/** A fresh, empty folder per test case, deleted afterwards: <temp>/qtn-test-<uuid>. The UUID keeps
    test processes that run in parallel (ctest -j) apart; a name probed on disk ("first free
    name") could be picked by two processes at once.

    Folders made read-only with makeUnwritable() get their permissions back first, so they can be
    deleted. */
class TempDir
{
  public:
    TempDir()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("qtn-test-" + juce::Uuid().toString()))
    {
        REQUIRE (! folder.exists());
        REQUIRE (folder.createDirectory().wasOk());
    }

    ~TempDir()
    {
        for (const auto& locked : lockedFolders)
            std::filesystem::permissions (locked.getFullPathName().toStdString(),
                                          std::filesystem::perms::owner_all,
                                          std::filesystem::perm_options::replace);
        folder.deleteRecursively();
    }

    TempDir (const TempDir&) = delete;
    TempDir& operator= (const TempDir&) = delete;

    [[nodiscard]] juce::File get() const { return folder; }

    /** Makes `dir` (this folder or one inside it) read-only until this TempDir is destroyed. */
    void makeUnwritable (const juce::File& dir)
    {
        lockedFolders.push_back (dir);
        std::filesystem::permissions (dir.getFullPathName().toStdString(),
                                      std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
                                      std::filesystem::perm_options::replace);
    }

    /** Makes this folder read-only until this TempDir is destroyed. */
    void makeUnwritable() { makeUnwritable (folder); }

  private:
    juce::File folder;
    std::vector<juce::File> lockedFolders;
};
} // namespace qtn::test
