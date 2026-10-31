#include "backup/ExportCompare.h"

#include "Bytes.h"
#include "backup/BackupStore.h"
#include "model/PresetFile.h"

#include <array>
#include <optional>
#include <span>

namespace qtn
{
namespace
{
constexpr int comparedSlots = static_cast<int> (PresetFile::allPresetsRecordCount); // 0-35

using SlotBytes = std::array<std::optional<Bytes>, comparedSlots>;

bool inRange (int slot) noexcept { return slot >= 0 && slot < comparedSlots; }

/** Slot -> bytes; a slot seen twice is dropped, so it counts as different. */
void put (SlotBytes& slots, std::array<int, comparedSlots>& seen, int slot, Bytes bytes)
{
    if (! inRange (slot))
        return;

    const auto i = static_cast<size_t> (slot);
    if (++seen[i] == 1)
        slots[i] = std::move (bytes);
    else
        slots[i].reset();
}
} // namespace

ExportComparison compareWithExport (const juce::File& backupFolder, const juce::File& nuxpresetFile)
{
    // BackupStore::load reads only the folder it is given, so its root does not matter here.
    const auto blobs = BackupStore (backupFolder.getParentDirectory()).load (backupFolder);
    if (blobs.empty())
        return {{}, "Backup could not be read"};

    juce::MemoryBlock data;
    if (! nuxpresetFile.loadFileAsData (data))
        return {{}, "Cannot read " + nuxpresetFile.getFullPathName()};

    const auto parsed = PresetFile::read (std::span (static_cast<const uint8_t*> (data.getData()), data.getSize()));
    if (! parsed.contents)
        return {{}, juce::String (parsed.error)};

    SlotBytes fromBackup;
    std::array<int, comparedSlots> backupSeen{};
    for (const auto& blob : blobs)
        put (fromBackup, backupSeen, blob.slot, blob.raw);

    SlotBytes fromExport;
    std::array<int, comparedSlots> exportSeen{};
    for (const auto& record : parsed.contents->records)
        put (fromExport, exportSeen, record.slot, Bytes (record.preset.raw.begin(), record.preset.raw.end()));

    ExportComparison result;
    for (int slot = 0; slot < comparedSlots; ++slot)
    {
        const auto& a = fromBackup[static_cast<size_t> (slot)];
        const auto& b = fromExport[static_cast<size_t> (slot)];
        result.slots.push_back ({slot, a.has_value() && b.has_value() && *a == *b});
    }
    return result;
}

juce::String describe (const ExportComparison& comparison)
{
    if (comparison.error.isNotEmpty())
        return "Compare failed: " + comparison.error;

    int identical = 0;
    juce::StringArray different;
    for (const auto& s : comparison.slots)
    {
        if (s.equal)
            ++identical;
        else
            different.add (juce::String (s.slot));
    }

    auto text = juce::String (identical) + "/" + juce::String (static_cast<int> (comparison.slots.size())) + " identical";
    if (! different.isEmpty())
        text << "; different: slots " << different.joinIntoString (", ");
    return text;
}
} // namespace qtn
