#include "ui/laf/AssetLibrary.h"

namespace qtn
{
AssetLibrary::AssetLibrary (AssetManifest manifest, juce::File filesDirectory)
    : assets (std::move (manifest)),
      directory (std::move (filesDirectory))
{
}

std::unique_ptr<AssetLibrary> AssetLibrary::open (const juce::File& folder)
{
    const auto file = folder.getChildFile (manifestFilename);
    if (! file.existsAsFile())
    {
        juce::Logger::writeToLog ("AssetLibrary: no manifest at " + file.getFullPathName());
        return nullptr;
    }

    auto parsed = AssetManifest::parse (file.loadFileAsString());
    if (! parsed.manifest.has_value())
    {
        juce::Logger::writeToLog ("AssetLibrary: " + file.getFullPathName() + " " + parsed.error);
        return nullptr;
    }
    return std::make_unique<AssetLibrary> (std::move (*parsed.manifest), folder);
}

juce::File AssetLibrary::bundleAssetsDirectory()
{
    return juce::File::getSpecialLocation (juce::File::currentApplicationFile)
        .getChildFile ("Contents/Resources/assets");
}

juce::Image AssetLibrary::image (const juce::String& originalFilename)
{
    if (const auto* entry = assets.findByName (originalFilename); entry != nullptr)
        return load (*entry);

    juce::Logger::writeToLog ("AssetLibrary: no asset named " + originalFilename);
    return placeholderImage();
}

juce::Image AssetLibrary::image (int index)
{
    if (const auto* entry = assets.findByIndex (index); entry != nullptr)
        return load (*entry);

    juce::Logger::writeToLog ("AssetLibrary: no asset with index " + juce::String (index));
    return placeholderImage();
}

juce::Image AssetLibrary::image (const juce::String& baseName, Language language)
{
    return image (localisedImageFilename (baseName, language));
}

juce::Image AssetLibrary::load (const AssetEntry& entry)
{
    if (const auto it = images.find (entry.index); it != images.end())
        return it->second;

    auto decoded = juce::ImageFileFormat::loadFrom (directory.getChildFile (entry.filename));
    if (! decoded.isValid())
    {
        juce::Logger::writeToLog ("AssetLibrary: cannot load " + entry.filename);
        decoded = placeholderImage();
    }
    images.emplace (entry.index, decoded);
    return decoded;
}

juce::Font AssetLibrary::font (const juce::String& originalFilename, float height)
{
    const auto* entry = assets.findByName (originalFilename);
    if (entry == nullptr)
    {
        juce::Logger::writeToLog ("AssetLibrary: no font named " + originalFilename);
        return juce::Font (juce::FontOptions (height));
    }

    auto it = typefaces.find (entry->index);
    if (it == typefaces.end())
    {
        juce::MemoryBlock data;
        juce::Typeface::Ptr typeface;
        if (directory.getChildFile (entry->filename).loadFileAsData (data) && data.getSize() > 0)
            typeface = juce::Typeface::createSystemTypefaceFor (data.getData(), data.getSize());
        if (typeface == nullptr)
            juce::Logger::writeToLog ("AssetLibrary: cannot load font " + entry->filename);
        it = typefaces.emplace (entry->index, typeface).first;
    }

    if (it->second == nullptr)
        return juce::Font (juce::FontOptions (height));
    return juce::Font (juce::FontOptions (it->second).withHeight (height));
}

juce::Font AssetLibrary::font (Language language, bool bold, float height)
{
    return font (fontFilename (language, bold), height);
}

juce::Image AssetLibrary::placeholderImage()
{
    juce::Image placeholder (juce::Image::ARGB, 16, 16, false);
    placeholder.clear (placeholder.getBounds(), juce::Colours::magenta);
    return placeholder;
}
} // namespace qtn
