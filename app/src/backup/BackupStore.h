#pragma once

#include "Bytes.h"

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace qtn
{
/** One preset as read from the pedal: its slot number, its name (UTF-8) and its raw bytes. */
struct PresetBlob
{
    int slot{};
    std::string name;
    Bytes raw;
};

/** Outcome of BackupStore::save. On failure `ok` is false and `error` says why. */
struct BackupResult
{
    bool ok{};
    juce::File folder;
    juce::String error;
};

/** Timestamped preset backups on disk.

    Layout: <root>/<YYYY-MM-DDTHH-MM-SS.mmmZ>/slot-NNN.bin plus manifest.json
    ([{"slot":int,"name":str,"sha256":str}]). The folder name is the save time in UTC with
    milliseconds. A backup is written into <folder>.partial and then renamed, so a folder without
    a manifest (an interrupted save) is never listed.

    Only folders named like a backup are backups: the UTC name above or the previous build's
    local-time "YYYY-MM-DDTHH-MM-SS" (still listed and loaded, ordered by the instant it names).
    Any other folder in the root is never listed and never deleted.

    A save whose content (slot numbers and per-slot SHA-256) equals the newest intact backup
    writes nothing and returns that backup. After a save that wrote a new backup, the newest
    `keep` backups and the oldest one (the first ever taken) are kept; the rest are deleted.
*/
class BackupStore
{
  public:
    static constexpr int maxSlot = 999; // slot numbers are written as three digits

    explicit BackupStore (juce::File root, int keep = 50);

    [[nodiscard]] BackupResult save (const std::vector<PresetBlob>& presets, juce::Time now);

    /** Complete backups, newest first by the instant in their folder name. */
    [[nodiscard]] std::vector<juce::File> list() const;

    /** The presets of one backup, or an empty vector when the manifest or any slot file is
        missing, unreadable or does not match its recorded SHA-256. */
    [[nodiscard]] std::vector<PresetBlob> load (const juce::File& folder) const;

  private:
    [[nodiscard]] bool holds (const juce::File& folder, const std::vector<PresetBlob>& presets) const;
    void prune() const;

    juce::File root;
    int keep;
};
} // namespace qtn
