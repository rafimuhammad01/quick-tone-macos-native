#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace qtn
{
/** Whether one preset slot of a backup equals the same slot of a QuickTone export. */
struct SlotComparison
{
    int slot{};
    bool equal{};
};

/** Outcome of compareWithExport: one entry per slot 0-35 on success, otherwise `error` says
    why and `slots` is empty. */
struct ExportComparison
{
    std::vector<SlotComparison> slots;
    juce::String error;
};

/** Compares the 103 preset bytes of slots 0-35 in a BackupStore backup folder with the records
    of a QuickTone all-presets `.nuxpreset` file (matched by the record's slot number, not its
    position). A slot missing on either side counts as different. Reads files only. */
[[nodiscard]] ExportComparison compareWithExport (const juce::File& backupFolder, const juce::File& nuxpresetFile);

/** "36/36 identical", "34/36 identical; different: slots 0, 35" or "Compare failed: <error>". */
[[nodiscard]] juce::String describe (const ExportComparison& comparison);
} // namespace qtn
