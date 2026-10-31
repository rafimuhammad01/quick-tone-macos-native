#pragma once

#include <juce_core/juce_core.h>

namespace qtn
{
/** UI languages, numbered like the original LanguageManager::setType ids (R5 §0.4). */
enum class Language
{
    english = 1,
    chineseSimplified = 2,
    chineseTraditional = 3,
    french = 4,
    german = 5,
    italian = 6,
    korean = 7,
    spanish = 8,
    thai = 9,
    russian = 10
};

/** The suffix the original appends to an image base name (CommonCode::getCurrentLanguageSuffix,
    R5 §0.4). English and Thai have none, so Thai shows the English images. */
[[nodiscard]] juce::String languageSuffix (Language language);

/** Original filename of a localised image: <base><suffix>.png. */
[[nodiscard]] juce::String localisedImageFilename (const juce::String& baseName, Language language);

/** Original filename of the font the UI uses for a language (R5 §0.4, R2 §5 Fonts): English uses
    Carlito (Plain/Bold), Korean and Thai have one plain face each, the others use Source Han Sans
    CN (Font_SY_Plain/Bold). */
[[nodiscard]] juce::String fontFilename (Language language, bool bold);
} // namespace qtn
