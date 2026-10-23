#include "backup/BackupStore.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <optional>
#include <set>

namespace qtn
{
namespace
{
const juce::String manifestName{"manifest.json"};
const juce::String partialSuffix{".partial"};

juce::String slotFileName (int slot)
{
    return "slot-" + juce::String (slot).paddedLeft ('0', 3) + ".bin";
}

juce::String sha256Of (const Bytes& bytes)
{
    return juce::SHA256 (bytes.data(), bytes.size()).toHexString();
}

/** Empty when the presets can be saved, otherwise the reason they cannot. */
juce::String validate (const std::vector<PresetBlob>& presets)
{
    std::set<int> seen;
    for (const auto& preset : presets)
    {
        if (preset.slot < 0 || preset.slot > BackupStore::maxSlot)
            return "Preset slot " + juce::String (preset.slot) + " is out of range 0.." + juce::String (BackupStore::maxSlot);

        if (! seen.insert (preset.slot).second)
            return "Preset slot " + juce::String (preset.slot) + " appears twice";
    }
    return {};
}

juce::String writePresets (const juce::File& folder, const std::vector<PresetBlob>& presets)
{
    juce::Array<juce::var> manifest;

    for (const auto& preset : presets)
    {
        const auto file = folder.getChildFile (slotFileName (preset.slot));
        // replaceWithData deletes the file when given no bytes, so an empty preset needs create().
        const auto written = preset.raw.empty() ? file.create().wasOk()
                                                : file.replaceWithData (preset.raw.data(), preset.raw.size());
        if (! written)
            return "Could not write " + file.getFullPathName();

        auto entry = std::make_unique<juce::DynamicObject>();
        entry->setProperty ("slot", preset.slot);
        entry->setProperty ("name", juce::String::fromUTF8 (preset.name.c_str(), static_cast<int> (preset.name.size())));
        entry->setProperty ("sha256", sha256Of (preset.raw));
        manifest.add (juce::var (entry.release()));
    }

    // The manifest is written last: a folder with a manifest has all its slot files.
    const auto manifestFile = folder.getChildFile (manifestName);
    if (! manifestFile.replaceWithText (juce::JSON::toString (juce::var (manifest))))
        return "Could not write " + manifestFile.getFullPathName();

    return {};
}

std::optional<PresetBlob> loadPreset (const juce::File& folder, const juce::var& entry)
{
    const auto* object = entry.getDynamicObject();
    if (object == nullptr || ! object->hasProperty ("slot") || ! object->hasProperty ("name") || ! object->hasProperty ("sha256"))
        return std::nullopt;

    const auto slotValue = object->getProperty ("slot");
    if (! slotValue.isInt() && ! slotValue.isInt64())
        return std::nullopt;

    const auto slot = static_cast<int> (slotValue);
    if (slot < 0 || slot > BackupStore::maxSlot)
        return std::nullopt;

    juce::MemoryBlock data;
    if (! folder.getChildFile (slotFileName (slot)).loadFileAsData (data))
        return std::nullopt;

    const auto* begin = static_cast<const uint8_t*> (data.getData());
    Bytes raw (begin, begin + data.getSize());
    if (sha256Of (raw) != object->getProperty ("sha256").toString())
        return std::nullopt;

    return PresetBlob{slot, object->getProperty ("name").toString().toStdString(), std::move (raw)};
}
} // namespace

BackupStore::BackupStore (juce::File rootFolder, int keepCount)
    : root (std::move (rootFolder)), keep (keepCount)
{
}

BackupResult BackupStore::save (const std::vector<PresetBlob>& presets, juce::Time now)
{
    if (const auto problem = validate (presets); problem.isNotEmpty())
        return {false, {}, problem};

    const auto folder = root.getChildFile (now.formatted ("%Y-%m-%dT%H-%M-%S"));
    if (const auto created = root.createDirectory(); created.failed())
        return {false, {}, "Could not create backup folder " + root.getFullPathName() + ": " + created.getErrorMessage()};

    const auto partial = root.getChildFile (folder.getFileName() + partialSuffix);
    partial.deleteRecursively(); // leftover from an interrupted save

    if (const auto created = partial.createDirectory(); created.failed())
        return {false, {}, "Could not create " + partial.getFullPathName() + ": " + created.getErrorMessage()};

    if (const auto problem = writePresets (partial, presets); problem.isNotEmpty())
    {
        partial.deleteRecursively();
        return {false, {}, problem};
    }

    // Fails, leaving it untouched, when a backup with the same timestamp already holds files.
    if (! partial.moveFileTo (folder))
    {
        partial.deleteRecursively();
        return {false, {}, "Could not rename " + partial.getFullPathName() + " to " + folder.getFileName()};
    }

    prune();
    return {true, folder, {}};
}

std::vector<juce::File> BackupStore::list() const
{
    std::vector<juce::File> backups;
    for (const auto& dir : root.findChildFiles (juce::File::findDirectories, false, "*", juce::File::FollowSymlinks::no))
        if (! dir.getFileName().endsWith (partialSuffix) && dir.getChildFile (manifestName).existsAsFile())
            backups.push_back (dir);

    // Folder names are zero-padded timestamps, so name order is time order.
    std::sort (backups.begin(), backups.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileName() > b.getFileName(); });
    return backups;
}

std::vector<PresetBlob> BackupStore::load (const juce::File& folder) const
{
    const auto manifest = juce::JSON::parse (folder.getChildFile (manifestName));
    const auto* entries = manifest.getArray();
    if (entries == nullptr)
        return {};

    std::vector<PresetBlob> presets;
    for (const auto& entry : *entries)
    {
        auto preset = loadPreset (folder, entry);
        if (! preset.has_value())
            return {};
        presets.push_back (std::move (*preset));
    }
    return presets;
}

void BackupStore::prune() const
{
    const auto backups = list();
    for (size_t i = static_cast<size_t> (std::max (keep, 0)); i < backups.size(); ++i)
        backups[i].deleteRecursively();
}
} // namespace qtn
