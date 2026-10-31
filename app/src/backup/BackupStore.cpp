#include "backup/BackupStore.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace qtn
{
namespace
{
const juce::String manifestName{"manifest.json"};
const juce::String partialSuffix{".partial"};

constexpr juce::int64 msPerDay = 86'400'000;

/** "YYYY-MM-DDTHH-MM-SS.mmmZ": the instant in UTC, so the order does not depend on the time zone. */
juce::String folderName (juce::Time now)
{
    namespace chr = std::chrono;
    const auto ms = now.toMilliseconds();
    auto days = ms / msPerDay;
    auto ofDay = ms % msPerDay;
    if (ofDay < 0)
    {
        ofDay += msPerDay;
        --days;
    }

    const chr::year_month_day date{chr::sys_days{chr::days{days}}};
    char text[40]{};
    std::snprintf (text, sizeof (text), "%04d-%02u-%02uT%02lld-%02lld-%02lld.%03lldZ",
                   static_cast<int> (date.year()), static_cast<unsigned> (date.month()), static_cast<unsigned> (date.day()),
                   static_cast<long long> (ofDay / 3'600'000), static_cast<long long> (ofDay / 60'000 % 60),
                   static_cast<long long> (ofDay / 1000 % 60), static_cast<long long> (ofDay % 1000));
    return text;
}

/** The number in name[start, start + count), or -1 if any of those characters is not a digit. */
int digitsAt (const juce::String& name, int start, int count)
{
    int value = 0;
    for (int i = start; i < start + count; ++i)
    {
        const auto c = name[i];
        if (c < '0' || c > '9')
            return -1;
        value = value * 10 + static_cast<int> (c - '0');
    }
    return value;
}

/** The instant a backup folder name stands for: the current "YYYY-MM-DDTHH-MM-SS.mmmZ" (UTC) or
    the previous build's "YYYY-MM-DDTHH-MM-SS" (local time). Empty for any other name, which is
    then not a backup: never listed, never pruned. */
std::optional<juce::int64> folderInstant (const juce::String& name)
{
    const bool utc = name.length() == 24;
    if (! utc && name.length() != 19)
        return std::nullopt;

    for (const auto [index, separator] : {std::pair{4, '-'}, {7, '-'}, {10, 'T'}, {13, '-'}, {16, '-'}})
        if (name[index] != separator)
            return std::nullopt;
    if (utc && (name[19] != '.' || name[23] != 'Z'))
        return std::nullopt;

    const auto year = digitsAt (name, 0, 4);
    const auto month = digitsAt (name, 5, 2);
    const auto day = digitsAt (name, 8, 2);
    const auto hour = digitsAt (name, 11, 2);
    const auto minute = digitsAt (name, 14, 2);
    const auto second = digitsAt (name, 17, 2);
    const auto milli = utc ? digitsAt (name, 20, 3) : 0;

    namespace chr = std::chrono;
    const chr::year_month_day date{chr::year{year}, chr::month{static_cast<unsigned> (month)}, chr::day{static_cast<unsigned> (day)}};
    if (year < 0 || month < 0 || day < 0 || milli < 0 || ! date.ok() || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return std::nullopt;

    if (! utc) // JUCE months are 0-based
        return juce::Time (year, month - 1, day, hour, minute, second, 0, true).toMilliseconds();

    const auto days = chr::sys_days{date}.time_since_epoch().count();
    return juce::int64{days} * msPerDay + ((hour * 60 + minute) * 60 + second) * juce::int64{1000} + milli;
}

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

bool BackupStore::holds (const juce::File& folder, const std::vector<PresetBlob>& presets) const
{
    // load() checks every slot file against the SHA-256 in the manifest, so a damaged backup is
    // never taken as a match.
    const auto stored = load (folder);
    if (stored.empty() || stored.size() != presets.size())
        return false;

    std::map<int, juce::String> wanted;
    for (const auto& preset : presets)
        wanted[preset.slot] = sha256Of (preset.raw);

    return std::all_of (stored.begin(), stored.end(), [&] (const PresetBlob& blob)
                        {
                            const auto it = wanted.find (blob.slot);
                            return it != wanted.end() && it->second == sha256Of (blob.raw); });
}

BackupResult BackupStore::save (const std::vector<PresetBlob>& presets, juce::Time now)
{
    if (const auto problem = validate (presets); problem.isNotEmpty())
        return {false, {}, problem};

    // Same content as the newest backup: reuse it, so pruning never trades a good backup for a
    // copy of itself.
    if (const auto backups = list(); ! backups.empty() && holds (backups.front(), presets))
        return {true, backups.front(), {}};

    const auto folder = root.getChildFile (folderName (now));
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

    prune (folder);
    return {true, folder, {}};
}

std::vector<juce::File> BackupStore::list() const
{
    std::vector<std::pair<juce::int64, juce::File>> found;
    for (const auto& dir : root.findChildFiles (juce::File::findDirectories, false, "*", juce::File::FollowSymlinks::no))
        if (const auto instant = folderInstant (dir.getFileName()); instant.has_value() && dir.getChildFile (manifestName).existsAsFile())
            found.emplace_back (*instant, dir);

    // By the instant in the name (UTC and legacy local-time names mixed), then by name.
    std::sort (found.begin(), found.end(), [] (const auto& a, const auto& b)
               { return a.first != b.first ? a.first > b.first : a.second.getFileName() > b.second.getFileName(); });

    std::vector<juce::File> backups;
    backups.reserve (found.size());
    for (auto& [instant, dir] : found)
        backups.push_back (std::move (dir));
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

void BackupStore::prune (const juce::File& justSaved) const
{
    // The oldest backup (the first one ever taken) is never deleted, nor the one just written:
    // when the clock went backwards it is dated before newer ones and may fall outside `keep`.
    const auto backups = list();
    for (size_t i = static_cast<size_t> (std::max (keep, 0)); i + 1 < backups.size(); ++i)
        if (backups[i] != justSaved)
            backups[i].deleteRecursively();
}
} // namespace qtn
