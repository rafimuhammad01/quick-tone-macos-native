#include "ui/laf/NuxLookAndFeel.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

TEST_CASE ("look_and_feel_uses_r5_colours")
{
    // Window popup menus: SFW:L2320-2324 (R5 §0.1 "Window L&F").
    qtn::NuxMenuLookAndFeel menu;
    CHECK (menu.findColour (juce::PopupMenu::backgroundColourId) == juce::Colour (0xfe2f393fu));
    CHECK (menu.findColour (juce::PopupMenu::textColourId) == juce::Colour (0xffffffffu));
    CHECK (menu.findColour (juce::PopupMenu::highlightedBackgroundColourId) == juce::Colour (0xff565656u));
    CHECK (menu.findColour (juce::PopupMenu::highlightedTextColourId) == juce::Colour (0xff81ecc0u));
    CHECK (menu.findColour (juce::PopupMenu::headerTextColourId) == juce::Colour (0xff81ecc0u));

    // Tips bubble: NuxTipsComponent (R5), colour id 0x1000af0.
    qtn::NuxLookAndFeel shared;
    CHECK (shared.findColour (juce::BubbleComponent::backgroundColourId) == juce::Colour (0xffffffffu));
}

TEST_CASE ("look_and_feel_fonts_follow_r5")
{
    CHECK (qtn::comboBoxFontHeight (30) == 15.0f);                                       // capped at 15
    CHECK (qtn::comboBoxFontHeight (10) == 8.5f);                                        // 0.85 * height
    CHECK_THAT (qtn::comboBoxFontHeight (17), Catch::Matchers::WithinAbs (14.45, 1e-4)); // under the cap

    qtn::NuxLookAndFeel shared;
    qtn::NuxMenuLookAndFeel menu;
    CHECK (shared.getPopupMenuFont().getHeight() == 17.0f);
    CHECK (menu.getPopupMenuFont().getHeight() == 18.0f);
}

TEST_CASE ("scrollbar_thumb_brightens_on_hover")
{
    const juce::Colour thumb (0x80102030u);

    CHECK (qtn::scrollbarThumbColour (thumb, false) == thumb);

    const auto hover = qtn::scrollbarThumbColour (thumb, true);
    // 255 - 0.8 * (255 - c), truncated like the original's (int) cast; alpha kept.
    CHECK (hover.getRed() == 63);   // 255 - 0.8 * 239 = 63.8
    CHECK (hover.getGreen() == 76); // 255 - 0.8 * 223 = 76.6
    CHECK (hover.getBlue() == 89);  // 255 - 0.8 * 207 = 89.4
    CHECK (hover.getAlpha() == 0x80);
}
