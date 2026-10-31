#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace qtn
{
/** One embedded resource of the original app: its BinaryData index N (temp_binary_data_N), its
    original filename and its size in bytes. */
struct AssetEntry
{
    int index{};
    juce::String filename;
    int64_t size{};
};

/** The asset manifest generated at build time (asset-manifest.csv).

    Format: a header line `index,filename,size`, then one row per asset. Blank lines are ignored,
    CRLF is accepted. Index and size are non-negative integers, filename is non-empty, and both
    index and filename are unique.
*/
struct ManifestParseResult;

class AssetManifest
{
  public:
    using ParseResult = ManifestParseResult;

    [[nodiscard]] static ParseResult parse (const juce::String& csvText);

    [[nodiscard]] const AssetEntry* findByIndex (int index) const;
    [[nodiscard]] const AssetEntry* findByName (const juce::String& filename) const;
    [[nodiscard]] const std::vector<AssetEntry>& entries() const noexcept { return rows; }

  private:
    std::vector<AssetEntry> rows;
    std::map<int, size_t> byIndex;
    std::map<juce::String, size_t> byName;
};

struct ManifestParseResult
{
    std::optional<AssetManifest> manifest;
    juce::String error; // set when manifest is empty; names the offending line
};
} // namespace qtn
