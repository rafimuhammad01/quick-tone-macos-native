#pragma once

#include "ui/laf/AssetLibrary.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace qtn
{
/** Index 0..11 of the harmony LED and its up/down stepping
    (decomp:NuxHarmonyLEDComponent/NuxHarmonyLEDComponent@100062c30.c: min +0xc4 = 0, max +0xc8 = 11;
     decomp:NuxHarmonyLEDComponent/buttonClicked@100061ec0.c: down decrements while > min, up
     increments while < max). */
class HarmonyLedLogic
{
  public:
    static constexpr int minIndex = 0;
    static constexpr int maxIndex = 11;

    [[nodiscard]] int index() const noexcept { return current; }
    /** Clamps; returns true when the index changed. */
    bool setIndex (int newIndex) noexcept;
    bool stepUp() noexcept;
    bool stepDown() noexcept;

  private:
    int current{minIndex};
};

/** NuxHarmonyLEDComponent(2): the 12-step LED selector the NME-300 instrument panel creates for
    element type 0x5a (decomp:NuxInstrumentPanel/showElement@100245f50.c:L1494).

    Layout (decomp:NuxHarmonyLEDComponent/NuxHarmonyLEDComponent@100062c30.c): 51x79; background
    data_450 at (0,0,51,61); down button (0,61,26,18) data_452 normal/over, data_451 down; up button
    (27,61,26,18) data_454 normal/over, data_453 down; glyph (5,15,42,32) = data_414 + index.

    Widget contract (T6): integer device units, onValueChange, setValue (int, NotificationType).
*/
class HarmonyLED : public juce::Component
{
  public:
    static constexpr int backgroundImage = 450;
    static constexpr int downPressedImage = 451;
    static constexpr int downNormalImage = 452;
    static constexpr int upPressedImage = 453;
    static constexpr int upNormalImage = 454;

    [[nodiscard]] static constexpr int glyphImageIndex (int index) noexcept { return 414 + index; }

    explicit HarmonyLED (AssetLibrary& assets);

    std::function<void (int)> onValueChange;

    void setValue (int newValue, juce::NotificationType notification);
    [[nodiscard]] int getValue() const noexcept { return logic.index(); }

    [[nodiscard]] juce::ImageButton& downButton() noexcept { return down; }
    [[nodiscard]] juce::ImageButton& upButton() noexcept { return up; }

    void paint (juce::Graphics& g) override;

  private:
    void changed (juce::NotificationType notification);

    HarmonyLedLogic logic;
    juce::Image background;
    std::array<juce::Image, HarmonyLedLogic::maxIndex + 1> glyphs;
    juce::ImageButton down, up;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonyLED)
};
} // namespace qtn
