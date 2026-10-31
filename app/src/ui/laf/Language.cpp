#include "ui/laf/Language.h"

namespace qtn
{
juce::String languageSuffix (Language language)
{
    switch (language)
    {
    case Language::chineseSimplified:
        return "_CN";
    case Language::chineseTraditional:
        return "_FT";
    case Language::french:
        return "_French";
    case Language::german:
        return "_German";
    case Language::italian:
        return "_Italian";
    case Language::korean:
        return "_Korean";
    case Language::spanish:
        return "_Spanish";
    case Language::russian:
        return "_Russian";
    case Language::english:
    case Language::thai:
        break;
    }
    return {};
}

juce::String localisedImageFilename (const juce::String& baseName, Language language)
{
    return baseName + languageSuffix (language) + ".png";
}

juce::String fontFilename (Language language, bool bold)
{
    switch (language)
    {
    case Language::english:
        return bold ? "Bold.ttf" : "Plain.ttf";
    case Language::korean:
        return "Font_SY_Plain_Korean.ttf";
    case Language::thai:
        return "Font_SY_Plain_Thai.ttf";
    case Language::chineseSimplified:
    case Language::chineseTraditional:
    case Language::french:
    case Language::german:
    case Language::italian:
    case Language::spanish:
    case Language::russian:
        break;
    }
    return bold ? "Font_SY_Bold.ttf" : "Font_SY_Plain.ttf";
}
} // namespace qtn
