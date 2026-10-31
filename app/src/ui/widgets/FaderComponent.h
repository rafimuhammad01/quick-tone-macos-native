#pragma once

#include "ui/laf/AssetLibrary.h"
#include "ui/widgets/FaderLogic.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace qtn
{
/** The look shared by the original's horizontal faders: a background image stretched to the
    bounds, a yellow gradient fill bar up to the thumb, and the thumb image. Decisions live in
    FaderLogic; this class only forwards mouse events and paints.

    Widget contract (T6): integer device units, onValueChange, setValue (int, NotificationType).
*/
struct FaderImages
{
    int background{};
    int thumbNormal{};
    int thumbHover{};
    int thumbPressed{};
};

class FaderComponent : public juce::Component
{
  public:
    FaderComponent (AssetLibrary& assets, const FaderGeometry& geometry, const FaderImages& images);

    std::function<void (int)> onValueChange;

    void setValue (int newValue, juce::NotificationType notification);
    [[nodiscard]] int getValue() const noexcept { return logic.value(); }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

  protected:
    void notify (juce::NotificationType notification);

  private:
    void renderFillBar();
    [[nodiscard]] const juce::Image& thumbImage() const noexcept;

    FaderLogic logic;
    juce::Image background, thumbNormal, thumbHover, thumbPressed;
    juce::Image fillBar; // pre-rendered at 2x in resized(); paint() only draws a slice of it

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaderComponent)
};

/** Gradient of every fader's fill bar: vertical, 0xfff4cc10 at y 8 to 0xffffe04f at y 20
    (decomp:NuxSettingVolumeComponent/paint@100076d90.c:L33-43;
     decomp:NuxIRFaderComponent/paint@100064640.c:L63-73). */
inline constexpr juce::uint32 fillTopColour = 0xfff4cc10;
inline constexpr juce::uint32 fillBottomColour = 0xffffe04f;
} // namespace qtn
