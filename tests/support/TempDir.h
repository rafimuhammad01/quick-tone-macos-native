#pragma once

#include <juce_core/juce_core.h>

namespace qtn::test
{
/** A fresh, empty folder per test case, deleted afterwards. */
class TempDir
{
  public:
    TempDir()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getNonexistentChildFile ("qtn-test", "", false))
    {
        [[maybe_unused]] const bool created = folder.createDirectory().wasOk();
        jassert (created);
    }

    ~TempDir() { folder.deleteRecursively(); }

    TempDir (const TempDir&) = delete;
    TempDir& operator= (const TempDir&) = delete;

    [[nodiscard]] juce::File get() const { return folder; }

  private:
    juce::File folder;
};
} // namespace qtn::test
