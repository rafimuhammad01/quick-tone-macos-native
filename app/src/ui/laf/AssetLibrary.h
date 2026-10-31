#pragma once

#include "ui/laf/AssetManifest.h"
#include "ui/laf/Language.h"

#include <juce_graphics/juce_graphics.h>

#include <map>
#include <memory>

namespace qtn
{
/** Images and fonts of the original app, read from a folder of extracted files described by an
    AssetManifest.

    In the app the folder is the bundle's Contents/Resources/assets (see bundleAssetsDirectory());
    tests pass any folder. Decoded images and typefaces are cached. A name or index that is not in
    the manifest, or a file that is missing or cannot be decoded, gives a 16x16 magenta placeholder
    image (and a log line), so a gap is visible on screen instead of crashing.

    Not thread-safe: use it from the message thread only, and never load inside paint().
*/
class AssetLibrary
{
  public:
    static constexpr const char* manifestFilename = "asset-manifest.csv";

    AssetLibrary (AssetManifest manifest, juce::File filesDirectory);

    /** Reads <directory>/asset-manifest.csv. Returns nullptr (and logs) when it is missing or
        malformed. */
    [[nodiscard]] static std::unique_ptr<AssetLibrary> open (const juce::File& directory);

    /** Contents/Resources/assets of the running app bundle. */
    [[nodiscard]] static juce::File bundleAssetsDirectory();

    [[nodiscard]] const AssetManifest& manifest() const noexcept { return assets; }

    /** By original filename, e.g. "UI_Button_OK_N.png". */
    [[nodiscard]] juce::Image image (const juce::String& originalFilename);

    /** By BinaryData index N (the data_N that R5 cites). */
    [[nodiscard]] juce::Image image (int index);

    /** Localised image: <base><language suffix>.png (R5 §0.4). */
    [[nodiscard]] juce::Image image (const juce::String& baseName, Language language);

    /** A font using the embedded typeface file, e.g. "Plain.ttf". Falls back to the default sans
        serif font of that height (and logs) when the file is missing or not a font. */
    [[nodiscard]] juce::Font font (const juce::String& originalFilename, float height);

    /** The UI font for a language (fontFilename()). */
    [[nodiscard]] juce::Font font (Language language, bool bold, float height);

    [[nodiscard]] static juce::Image placeholderImage();

  private:
    [[nodiscard]] juce::Image load (const AssetEntry& entry);

    AssetManifest assets;
    juce::File directory;
    std::map<int, juce::Image> images;
    std::map<int, juce::Typeface::Ptr> typefaces;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AssetLibrary)
};
} // namespace qtn
