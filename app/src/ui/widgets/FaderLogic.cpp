#include "ui/widgets/FaderLogic.h"

#include <algorithm>

namespace qtn
{
namespace
{
float clamp01 (float proportion) noexcept
{
    return std::clamp (proportion, 0.0f, 1.0f);
}
} // namespace

FaderLogic::FaderLogic (FaderGeometry geometry)
    : layout (geometry),
      current (geometry.minValue),
      thumb (geometry.thumb.withX (thumbXForValue (geometry, geometry.minValue)))
{
}

int FaderLogic::thumbXForValue (const FaderGeometry& geometry, int value)
{
    // NormalisableRange::convertTo0to1 with skew 1, clamped; then the thumb setBounds
    // (decomp:NuxIRFaderComponent/setValue@100064b00.c; NuxPatchLevelComponent/setValue adds minX).
    const auto span = static_cast<float> (geometry.maxValue - geometry.minValue);
    const auto proportion = clamp01 (static_cast<float> (value - geometry.minValue) / span);
    return static_cast<int> ((geometry.maxThumbX - geometry.minThumbX) * proportion + geometry.minThumbX);
}

int FaderLogic::valueForThumbX (const FaderGeometry& geometry, float x)
{
    // decomp:NuxPatchLevelComponent/mouseDrag@10006cae0.c: (int) convertFrom0to1 (clamp01 (...)).
    const auto proportion = clamp01 ((x - geometry.minThumbX) / (geometry.maxThumbX - geometry.minThumbX));
    const auto span = static_cast<float> (geometry.maxValue - geometry.minValue);
    return static_cast<int> (static_cast<float> (geometry.minValue) + span * proportion);
}

juce::Rectangle<float> FaderLogic::fillBounds() const noexcept
{
    const auto width = std::max (0.0f, static_cast<float> (thumb.getX()) + layout.fillWidthOffset);
    return layout.fill.withWidth (width);
}

bool FaderLogic::setValue (int newValue)
{
    const auto clamped = std::clamp (newValue, layout.minValue, layout.maxValue);
    thumb.setX (thumbXForValue (layout, clamped));
    if (clamped == current)
        return false;
    current = clamped;
    return true;
}

bool FaderLogic::onThumb (juce::Point<int> position) const noexcept
{
    return thumb.contains (position);
}

void FaderLogic::mouseMove (juce::Point<int> position)
{
    state = onThumb (position) ? ThumbState::hover : ThumbState::normal;
}

void FaderLogic::mouseExit()
{
    state = ThumbState::normal;
}

void FaderLogic::mouseDown (juce::Point<int> position)
{
    dragging = onThumb (position);
    if (dragging)
        state = ThumbState::pressed;
}

bool FaderLogic::mouseDrag (juce::Point<int> position)
{
    if (! dragging)
        return false;

    // Thumb centred on the mouse, kept on the track (…/mouseDrag: x - thumbWidth / 2, clamped).
    const auto x = std::clamp (static_cast<float> (position.x - thumb.getWidth() / 2), layout.minThumbX, layout.maxThumbX);
    const auto newValue = valueForThumbX (layout, x);
    const auto changed = newValue != current;
    current = newValue;
    thumb.setX (layout.snapThumbToValue ? thumbXForValue (layout, newValue) : static_cast<int> (x));
    return changed;
}

void FaderLogic::mouseUp (juce::Point<int> position)
{
    dragging = false;
    state = onThumb (position) ? ThumbState::hover : ThumbState::normal;
}
} // namespace qtn
