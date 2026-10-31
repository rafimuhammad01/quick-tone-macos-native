#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace qtn
{
/** Combo box text height of CustomLookAndFeel::getComboBoxFont: min(15, 0.85 * box height). */
[[nodiscard]] float comboBoxFontHeight (int boxHeight) noexcept;

/** Scrollbar thumb colour of CustomLookAndFeel::drawScrollbar: on hover each RGB channel becomes
    255 - 0.8 * (255 - c) (20 % of the way to white, truncated); alpha is kept. */
[[nodiscard]] juce::Colour scrollbarThumbColour (juce::Colour thumb, bool mouseOver) noexcept;

/** The original's shared CustomLookAndFeel (side panel, setting panel and most components;
    R5 "Shared look-and-feel"). drawBubble is JUCE's (the original's override draws the same
    filled bubble path with colour ids 0x1000af0/0x1000af1). */
class NuxLookAndFeel : public juce::LookAndFeel_V4
{
  public:
    static constexpr float popupMenuFontHeight = 17.0f;

    NuxLookAndFeel();

    juce::Font getComboBoxFont (juce::ComboBox& box) override;
    juce::Font getPopupMenuFont() override;
    void drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NuxLookAndFeel)
};

/** The original's NuxCustomMenu::LookAndFeelLocal, used by the window's popup menus
    (R5 §0.1 "Window L&F"). */
class NuxMenuLookAndFeel : public juce::LookAndFeel_V4
{
  public:
    static constexpr float popupMenuFontHeight = 18.0f;

    NuxMenuLookAndFeel();

    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NuxMenuLookAndFeel)
};
} // namespace qtn
