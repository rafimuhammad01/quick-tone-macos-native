#include "ui/laf/AssetLibrary.h"
#include "ui/laf/AssetManifest.h"
#include "ui/laf/Language.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <map>

namespace
{
/** A fresh, empty folder per test case, deleted afterwards. */
class TempDir
{
  public:
    TempDir()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getNonexistentChildFile ("qtn-assets-test", "", false))
    {
        REQUIRE (folder.createDirectory().wasOk());
    }

    ~TempDir() { folder.deleteRecursively(); }

    TempDir (const TempDir&) = delete;
    TempDir& operator= (const TempDir&) = delete;

    const juce::File& path() const noexcept { return folder; }

  private:
    juce::File folder;
};

void writePng (const juce::File& file, int width, int height, juce::Colour colour)
{
    juce::Image image (juce::Image::ARGB, width, height, true);
    image.clear (image.getBounds(), colour);
    juce::FileOutputStream out (file);
    REQUIRE (out.openedOk());
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, out));
}

qtn::AssetManifest parseOrFail (const juce::String& text)
{
    auto parsed = qtn::AssetManifest::parse (text);
    INFO (parsed.error);
    REQUIRE (parsed.manifest.has_value());
    return std::move (*parsed.manifest);
}

bool isPlaceholder (const juce::Image& image)
{
    return image.isValid() && image.getWidth() == 16 && image.getHeight() == 16 && image.getPixelAt (0, 0) == juce::Colours::magenta && image.getPixelAt (15, 15) == juce::Colours::magenta;
}

// ---- real extracted assets (QTN_ASSETS_DIR), skipped when not configured -----------------------

bool realAssetsAvailable()
{
    return juce::String (QTN_ASSETS_DIR).isNotEmpty() && juce::File (QTN_ASSETS_DIR).getChildFile ("names.csv").existsAsFile() && juce::File (QTN_ASSET_MANIFEST).existsAsFile();
}

struct NamesRow
{
    int index{};
    int64_t size{};
    juce::String offset;
    bool usedByNme300{};
};

/** names.csv: resourceName,originalFilename,offset,size,usedByNME300; data row i is index i. */
std::map<juce::String, NamesRow> readNamesCsv()
{
    std::map<juce::String, NamesRow> rows;
    juce::StringArray lines;
    lines.addLines (juce::File (QTN_ASSETS_DIR).getChildFile ("names.csv").loadFileAsString());
    lines.removeEmptyStrings();
    for (int i = 1; i < lines.size(); ++i)
    {
        const auto cols = juce::StringArray::fromTokens (lines[i], ",", "");
        REQUIRE (cols.size() == 5);
        rows[cols[1]] = NamesRow{i - 1, cols[3].getLargeIntValue(), cols[2], cols[4] == "1"};
    }
    return rows;
}

/** Pixel size of the resource at `offset`, from manifest.csv (offset,size,type,sha256,width,height). */
std::pair<int, int> pixelSizeAt (const juce::String& offset)
{
    juce::StringArray lines;
    lines.addLines (juce::File (QTN_ASSETS_DIR).getChildFile ("manifest.csv").loadFileAsString());
    for (const auto& line : lines)
    {
        const auto cols = juce::StringArray::fromTokens (line, ",", "");
        if (cols.size() == 6 && cols[0] == offset)
            return {cols[4].getIntValue(), cols[5].getIntValue()};
    }
    FAIL ("offset " << offset << " not in manifest.csv");
    return {};
}

std::unique_ptr<qtn::AssetLibrary> realLibrary()
{
    auto parsed = qtn::AssetManifest::parse (juce::File (QTN_ASSET_MANIFEST).loadFileAsString());
    INFO (parsed.error);
    REQUIRE (parsed.manifest.has_value());
    return std::make_unique<qtn::AssetLibrary> (std::move (*parsed.manifest),
                                                juce::File (QTN_ASSETS_DIR).getChildFile ("named"));
}
} // namespace

// ---- manifest --------------------------------------------------------------------------------

TEST_CASE ("manifest_parses_rows_and_finds_by_index_and_name")
{
    const auto manifest = parseOrFail ("index,filename,size\r\n0,language_cn.txt,9032\r\n\r\n414,unnamed_temp_binary_data_414.png,566\r\n");

    CHECK (manifest.entries().size() == 2);
    const auto* byIndex = manifest.findByIndex (414);
    REQUIRE (byIndex != nullptr);
    CHECK (byIndex->filename == "unnamed_temp_binary_data_414.png");
    CHECK (byIndex->size == 566);
    const auto* byName = manifest.findByName ("language_cn.txt");
    REQUIRE (byName != nullptr);
    CHECK (byName->index == 0);
    CHECK (manifest.findByIndex (1) == nullptr);
    CHECK (manifest.findByName ("nope.png") == nullptr);
}

TEST_CASE ("manifest_rejects_malformed_rows")
{
    const auto body = GENERATE (as<std::string>{},
                                "1,a.png",                 // too few columns
                                "1,a.png,10,extra",        // too many columns
                                "x,a.png,10",              // index not a number
                                "-1,a.png,10",             // negative index
                                "1,a.png,ten",             // size not a number
                                "1,a.png,-5",              // negative size
                                "1,,10",                   // empty filename
                                "1,a.png,10\n1,b.png,20",  // duplicate index
                                "1,a.png,10\n2,a.png,20"); // duplicate filename

    const auto parsed = qtn::AssetManifest::parse ("index,filename,size\n" + juce::String (body));

    INFO (body);
    CHECK_FALSE (parsed.manifest.has_value());
    CHECK (parsed.error.contains ("line"));
}

TEST_CASE ("manifest_rejects_missing_header")
{
    CHECK_FALSE (qtn::AssetManifest::parse ("1,a.png,10\n").manifest.has_value());
    CHECK_FALSE (qtn::AssetManifest::parse ("").manifest.has_value());
}

// ---- images ----------------------------------------------------------------------------------

TEST_CASE ("loads_image_from_folder_by_name_and_by_index")
{
    TempDir dir;
    writePng (dir.path().getChildFile ("Knob.png"), 4, 3, juce::Colours::red);
    qtn::AssetLibrary library (parseOrFail ("index,filename,size\n7,Knob.png,1\n"), dir.path());

    const auto byName = library.image ("Knob.png");
    const auto byIndex = library.image (7);

    CHECK (byName.getWidth() == 4);
    CHECK (byName.getHeight() == 3);
    CHECK (byName.getPixelAt (0, 0) == juce::Colours::red);
    CHECK (byIndex.getWidth() == 4);
}

TEST_CASE ("missing_image_returns_placeholder")
{
    TempDir dir;
    writePng (dir.path().getChildFile ("Broken.png"), 2, 2, juce::Colours::red);
    dir.path().getChildFile ("Garbage.png").replaceWithText ("not a png");
    qtn::AssetLibrary library (parseOrFail ("index,filename,size\n1,Listed.png,10\n2,Garbage.png,9\n"), dir.path());

    CHECK (isPlaceholder (library.image ("Unlisted.png"))); // not in the manifest
    CHECK (isPlaceholder (library.image (99)));             // index not in the manifest
    CHECK (isPlaceholder (library.image ("Listed.png")));   // listed, file absent
    CHECK (isPlaceholder (library.image (2)));              // listed, not decodable
    CHECK (isPlaceholder (qtn::AssetLibrary::placeholderImage()));
}

TEST_CASE ("localised_image_name_uses_r5_suffixes")
{
    using qtn::Language;
    CHECK (qtn::localisedImageFilename ("UI_Button_OK_N", Language::english) == "UI_Button_OK_N.png");
    CHECK (qtn::localisedImageFilename ("UI_Button_OK_N", Language::chineseSimplified) == "UI_Button_OK_N_CN.png");
    CHECK (qtn::localisedImageFilename ("UI_Button_OK_N", Language::chineseTraditional) == "UI_Button_OK_N_FT.png");
    CHECK (qtn::localisedImageFilename ("X", Language::french) == "X_French.png");
    CHECK (qtn::localisedImageFilename ("X", Language::german) == "X_German.png");
    CHECK (qtn::localisedImageFilename ("X", Language::italian) == "X_Italian.png");
    CHECK (qtn::localisedImageFilename ("X", Language::korean) == "X_Korean.png");
    CHECK (qtn::localisedImageFilename ("X", Language::spanish) == "X_Spanish.png");
    CHECK (qtn::localisedImageFilename ("X", Language::thai) == "X.png");
    CHECK (qtn::localisedImageFilename ("X", Language::russian) == "X_Russian.png");
}

TEST_CASE ("localised_image_falls_back_to_english_for_thai")
{
    TempDir dir;
    writePng (dir.path().getChildFile ("Title.png"), 2, 2, juce::Colours::red);
    writePng (dir.path().getChildFile ("Title_CN.png"), 3, 3, juce::Colours::blue);
    qtn::AssetLibrary library (parseOrFail ("index,filename,size\n1,Title.png,1\n2,Title_CN.png,1\n"), dir.path());

    CHECK (library.image ("Title", qtn::Language::thai).getWidth() == 2);
    CHECK (library.image ("Title", qtn::Language::english).getWidth() == 2);
    CHECK (library.image ("Title", qtn::Language::chineseSimplified).getWidth() == 3);
}

TEST_CASE ("open_reads_manifest_from_folder")
{
    TempDir dir;
    writePng (dir.path().getChildFile ("Knob.png"), 5, 5, juce::Colours::red);
    dir.path().getChildFile (qtn::AssetLibrary::manifestFilename).replaceWithText ("index,filename,size\n3,Knob.png,1\n");

    const auto library = qtn::AssetLibrary::open (dir.path());

    REQUIRE (library != nullptr);
    CHECK (library->image (3).getWidth() == 5);
}

TEST_CASE ("open_returns_null_for_missing_or_malformed_manifest")
{
    TempDir dir;
    CHECK (qtn::AssetLibrary::open (dir.path()) == nullptr);

    dir.path().getChildFile (qtn::AssetLibrary::manifestFilename).replaceWithText ("index,filename,size\nx,Knob.png,1\n");
    CHECK (qtn::AssetLibrary::open (dir.path()) == nullptr);
}

// ---- fonts -----------------------------------------------------------------------------------

TEST_CASE ("font_file_for_language_follows_r5")
{
    using qtn::Language;
    CHECK (qtn::fontFilename (Language::english, false) == "Plain.ttf");
    CHECK (qtn::fontFilename (Language::english, true) == "Bold.ttf");
    CHECK (qtn::fontFilename (Language::chineseSimplified, false) == "Font_SY_Plain.ttf");
    CHECK (qtn::fontFilename (Language::russian, true) == "Font_SY_Bold.ttf");
    CHECK (qtn::fontFilename (Language::korean, false) == "Font_SY_Plain_Korean.ttf");
    CHECK (qtn::fontFilename (Language::korean, true) == "Font_SY_Plain_Korean.ttf");
    CHECK (qtn::fontFilename (Language::thai, false) == "Font_SY_Plain_Thai.ttf");
    CHECK (qtn::fontFilename (Language::thai, true) == "Font_SY_Plain_Thai.ttf");
}

TEST_CASE ("missing_font_falls_back_to_default_of_same_height")
{
    TempDir dir;
    dir.path().getChildFile ("Fake.ttf").replaceWithText ("not a font");
    qtn::AssetLibrary library (parseOrFail ("index,filename,size\n1,Fake.ttf,10\n"), dir.path());

    const auto unlisted = library.font ("Nope.ttf", 13.0f);
    const auto broken = library.font ("Fake.ttf", 12.0f);

    CHECK (unlisted.getHeight() == 13.0f);
    CHECK (unlisted.getTypefaceName() == juce::Font::getDefaultSansSerifFontName());
    CHECK (broken.getHeight() == 12.0f);
    CHECK (broken.getTypefaceName() == juce::Font::getDefaultSansSerifFontName());
}

// ---- real extracted assets -------------------------------------------------------------------

TEST_CASE ("manifest_matches_names_csv")
{
    if (! realAssetsAvailable())
        SKIP ("QTN_ASSETS_DIR not set or missing");

    const auto library = realLibrary();
    const auto names = readNamesCsv();

    int used = 0;
    for (const auto& [filename, row] : names)
        used += row.usedByNme300 ? 1 : 0;
    CHECK (library->manifest().entries().size() == static_cast<size_t> (used));

    for (const auto& entry : library->manifest().entries())
    {
        INFO (entry.filename);
        const auto it = names.find (entry.filename);
        REQUIRE (it != names.end());
        CHECK (it->second.index == entry.index);
        CHECK (it->second.size == entry.size);
        CHECK (it->second.usedByNme300);
    }
}

TEST_CASE ("loads_known_image_by_name")
{
    if (! realAssetsAvailable())
        SKIP ("QTN_ASSETS_DIR not set or missing");

    const auto library = realLibrary();
    const auto names = readNamesCsv();
    const juce::String filename{"UI_Button_OK_N.png"};
    const auto& row = names.at (filename);
    const auto* entry = library->manifest().findByName (filename);
    REQUIRE (entry != nullptr);
    CHECK (entry->size == row.size);

    const auto image = library->image (filename);

    const auto [width, height] = pixelSizeAt (row.offset);
    CHECK (image.getWidth() == width);
    CHECK (image.getHeight() == height);
}

TEST_CASE ("loads_known_image_by_index")
{
    if (! realAssetsAvailable())
        SKIP ("QTN_ASSETS_DIR not set or missing");

    const auto library = realLibrary();
    const auto names = readNamesCsv();
    const auto& row = names.at ("unnamed_temp_binary_data_414.png"); // harmony LED glyph (R2 §5)
    REQUIRE (row.index == 414);

    const auto image = library->image (414);

    const auto [width, height] = pixelSizeAt (row.offset);
    CHECK (image.getWidth() == width);
    CHECK (image.getHeight() == height);
    CHECK_FALSE (isPlaceholder (image));
}

TEST_CASE ("font_loads_from_ttf")
{
    if (! realAssetsAvailable())
        SKIP ("QTN_ASSETS_DIR not set or missing");

    const auto library = realLibrary();

    const auto plain = library->font ("Plain.ttf", 14.0f);
    const auto thai = library->font (qtn::Language::thai, false, 16.0f);

    CHECK (plain.getHeight() == 14.0f);
    CHECK (plain.getTypefaceName() == "Carlito");
    CHECK (thai.getTypefaceName() == "Prompt");
}
