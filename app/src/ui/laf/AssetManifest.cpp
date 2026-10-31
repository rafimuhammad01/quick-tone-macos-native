#include "ui/laf/AssetManifest.h"

namespace qtn
{
namespace
{
constexpr const char* header = "index,filename,size";

/** A non-negative decimal integer, or nullopt. */
std::optional<int64_t> parseCount (const juce::String& text)
{
    if (text.isEmpty() || text.length() > 15 || ! text.containsOnly ("0123456789"))
        return std::nullopt;
    return text.getLargeIntValue();
}
} // namespace

AssetManifest::ParseResult AssetManifest::parse (const juce::String& csvText)
{
    juce::StringArray lines;
    lines.addLines (csvText);

    const auto fail = [] (int lineNumber, const juce::String& why)
    {
        return ParseResult{std::nullopt, "line " + juce::String (lineNumber) + ": " + why};
    };

    AssetManifest manifest;
    bool sawHeader = false;
    for (int i = 0; i < lines.size(); ++i)
    {
        const auto line = lines[i].trimCharactersAtEnd ("\r");
        const int lineNumber = i + 1;
        if (line.isEmpty())
            continue;

        if (! sawHeader)
        {
            if (line != header)
                return fail (lineNumber, "expected header '" + juce::String (header) + "'");
            sawHeader = true;
            continue;
        }

        const auto cols = juce::StringArray::fromTokens (line, ",", "");
        if (cols.size() != 3)
            return fail (lineNumber, "expected 3 columns, got " + juce::String (cols.size()));

        const auto index = parseCount (cols[0]);
        const auto size = parseCount (cols[2]);
        if (! index.has_value() || *index > std::numeric_limits<int>::max())
            return fail (lineNumber, "bad index '" + cols[0] + "'");
        if (! size.has_value())
            return fail (lineNumber, "bad size '" + cols[2] + "'");
        if (cols[1].isEmpty())
            return fail (lineNumber, "empty filename");

        AssetEntry entry{static_cast<int> (*index), cols[1], *size};
        if (manifest.byIndex.count (entry.index) != 0)
            return fail (lineNumber, "duplicate index " + cols[0]);
        if (manifest.byName.count (entry.filename) != 0)
            return fail (lineNumber, "duplicate filename " + cols[1]);

        manifest.byIndex.emplace (entry.index, manifest.rows.size());
        manifest.byName.emplace (entry.filename, manifest.rows.size());
        manifest.rows.push_back (std::move (entry));
    }

    if (! sawHeader)
        return fail (1, "missing header '" + juce::String (header) + "'");

    return ParseResult{std::move (manifest), {}};
}

const AssetEntry* AssetManifest::findByIndex (int index) const
{
    const auto it = byIndex.find (index);
    return it != byIndex.end() ? &rows[it->second] : nullptr;
}

const AssetEntry* AssetManifest::findByName (const juce::String& filename) const
{
    const auto it = byName.find (filename);
    return it != byName.end() ? &rows[it->second] : nullptr;
}
} // namespace qtn
