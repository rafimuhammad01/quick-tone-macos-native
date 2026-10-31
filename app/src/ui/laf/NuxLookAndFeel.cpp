#include "ui/laf/NuxLookAndFeel.h"

#include <algorithm>

namespace qtn
{
namespace
{
// Corner radius of the rounded thumb and menu background. The Rectangle arguments are lost in the
// decomp (R5 open questions), so this is a visual guess. UNVERIFIED.
constexpr float cornerRadius = 4.0f;

juce::uint8 towardsWhite (juce::uint8 channel) noexcept
{
    return static_cast<juce::uint8> (static_cast<int> (255.0 - 0.8 * (255 - channel)));
}
} // namespace

float comboBoxFontHeight (int boxHeight) noexcept
{
    return std::min (15.0f, 0.85f * static_cast<float> (boxHeight));
}

juce::Colour scrollbarThumbColour (juce::Colour thumb, bool mouseOver) noexcept
{
    if (! mouseOver)
        return thumb;
    return juce::Colour (towardsWhite (thumb.getRed()), towardsWhite (thumb.getGreen()),
                         towardsWhite (thumb.getBlue()), thumb.getAlpha());
}

// ---- NuxLookAndFeel ------------------------------------------------------------------------------

NuxLookAndFeel::NuxLookAndFeel()
{
    // App default font (QuickApplication::initialise, R5 §0.1).
    setDefaultSansSerifTypefaceName ("Arial Unicode MS");
    // Tips bubble (NuxTipsComponent, R5): background 0xffffffff.
    setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xffffffffu));
}

juce::Font NuxLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return juce::Font (juce::FontOptions (comboBoxFontHeight (box.getHeight())));
}

juce::Font NuxLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (popupMenuFontHeight));
}

void NuxLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height,
                                    bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                    bool isMouseOver, bool /*isMouseDown*/)
{
    const auto thumb = isScrollbarVertical
                           ? juce::Rectangle<int> (x, y + thumbStartPosition, width, thumbSize)
                           : juce::Rectangle<int> (x + thumbStartPosition, y, thumbSize, height);
    g.setColour (scrollbarThumbColour (bar.findColour (juce::ScrollBar::thumbColourId), isMouseOver));
    g.fillRoundedRectangle (thumb.toFloat(), cornerRadius);
}

// ---- NuxMenuLookAndFeel --------------------------------------------------------------------------

NuxMenuLookAndFeel::NuxMenuLookAndFeel()
{
    // StandaloneFilterWindow ctor, SFW:L2320-2324 (R5 §0.1 "Window L&F").
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xfe2f393fu));
    setColour (juce::PopupMenu::textColourId, juce::Colour (0xffffffffu));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff565656u));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff81ecc0u));
    setColour (juce::PopupMenu::headerTextColourId, juce::Colour (0xff81ecc0u));
}

juce::Font NuxMenuLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (popupMenuFontHeight));
}

void NuxMenuLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.setColour (findColour (juce::PopupMenu::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), cornerRadius);
}
} // namespace qtn
