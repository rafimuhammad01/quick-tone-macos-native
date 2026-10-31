#pragma once

#include <juce_graphics/juce_graphics.h>

namespace qtn
{
/** Fixed layout of one of the original's horizontal faders (NuxIRFaderComponent,
    NuxSettingVolumeComponent, NuxPatchLevelComponent). Values are integer device units. */
struct FaderGeometry
{
    int minValue{};
    int maxValue{};
    juce::Point<int> size;       // component width x height
    juce::Rectangle<int> thumb;  // thumb bounds at minValue
    float minThumbX{};           // thumb x at minValue
    float maxThumbX{};           // thumb x at maxValue
    bool snapThumbToValue{};     // while dragging: thumb jumps to the value's x (IR/settings) or follows the mouse (patch level)
    juce::Rectangle<float> fill; // fill bar; its width is thumb x + fillWidthOffset
    float fillWidthOffset{};
    juce::uint32 fillColour{}; // solid fill colour; 0 = the yellow gradient of FaderComponent
};

enum class ThumbState
{
    normal,
    hover,
    pressed
};

/** All decisions of the faders: value <-> thumb position, mouse handling, thumb image state.
    No JUCE components, so it is unit-tested without a message loop. */
class FaderLogic
{
  public:
    explicit FaderLogic (FaderGeometry geometry);

    /** x = (int) ((maxX - minX) * (v - min) / (max - min) + minX), v clamped to the range. */
    [[nodiscard]] static int thumbXForValue (const FaderGeometry& geometry, int value);

    /** v = (int) (min + (max - min) * clamp01 ((x - minX) / (maxX - minX))), truncated like the
        original's (int) cast of NormalisableRange::convertFrom0to1. */
    [[nodiscard]] static int valueForThumbX (const FaderGeometry& geometry, float x);

    [[nodiscard]] int value() const noexcept { return current; }
    [[nodiscard]] juce::Rectangle<int> thumbBounds() const noexcept { return thumb; }
    [[nodiscard]] ThumbState thumbState() const noexcept { return state; }
    [[nodiscard]] juce::Rectangle<float> fillBounds() const noexcept;
    [[nodiscard]] const FaderGeometry& geometry() const noexcept { return layout; }

    /** Clamps to the range and moves the thumb. Returns true when the value changed. */
    bool setValue (int newValue);

    void mouseMove (juce::Point<int> position);
    void mouseExit();
    void mouseDown (juce::Point<int> position);
    /** Returns true when the drag changed the value. */
    bool mouseDrag (juce::Point<int> position);
    void mouseUp (juce::Point<int> position);

  private:
    [[nodiscard]] bool onThumb (juce::Point<int> position) const noexcept;

    FaderGeometry layout;
    int current{};
    juce::Rectangle<int> thumb;
    ThumbState state{ThumbState::normal};
    bool dragging{};
};
} // namespace qtn
