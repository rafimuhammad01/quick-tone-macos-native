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

    Layout: <root>/<YYYY-MM-DDTHH-MM-SS>/slot-NNN.bin plus manifest.json
    ([{"slot":int,"name":str,"sha256":str}]). A backup is written into <folder>.partial and then
    renamed, so a folder without a manifest (an interrupted save) is never listed. After a
    successful save only the newest `keep` backups are kept.
*/
class BackupStore
{
  public:
    static constexpr int maxSlot = 999; // slot numbers are written as three digits

    explicit BackupStore (juce::File root, int keep = 50);

    [[nodiscard]] BackupResult save (const std::vector<PresetBlob>& presets, juce::Time now);

    /** Complete backups, newest first. */
    [[nodiscard]] std::vector<juce::File> list() const;

    /** The presets of one backup, or an empty vector when the manifest or any slot file is
        missing, unreadable or does not match its recorded SHA-256. */
    [[nodiscard]] std::vector<PresetBlob> load (const juce::File& folder) const;

  private:
    void prune() const;

    juce::File root;
    int keep;
};
} // namespace qtn
