#include "ui/widgets/NuxFader.h"

#include "ui/laf/AssetLibrary.h"

#include <algorithm>
#include <cmath>

namespace qtn
{
// ---- FaderModel --------------------------------------------------------------------------------

void FaderModel::setRange (int minimum, int maximum) noexcept
{
    min = static_cast<float> (std::min (minimum, maximum));
    max = static_cast<float> (std::max (minimum, maximum));
    current = std::clamp (current, min, max);
    updateThumb();
}

void FaderModel::setGeometry (int newHeight, int newThumbWidth, int newThumbHeight) noexcept
{
    height = newHeight;
    thumbWidth = newThumbWidth;
    thumbHeight = newThumbHeight;
    updateThumb();
}

void FaderModel::updateThumb() noexcept
{
    // thumb y = clamp((max - v - min) / (max - min), 0, 1) * travel
    // (decomp:NuxFaderComponent/setValue@100087a40.c:L28-L33,L52-L53).
    const float p = max > min ? std::clamp ((max - current - min) / (max - min), 0.0f, 1.0f) : 0.0f;
    thumbTop = std::max (0.0f, travel()) * p;
}

bool FaderModel::setValue (float newValue) noexcept
{
    const int before = value();
    current = std::clamp (newValue, min, max);
    updateThumb();
    return value() != before;
}

void FaderModel::mouseDown (juce::Point<int> position) noexcept
{
    // Armed only when the press lands on the thumb (decomp:NuxFaderComponent/mouseDown@1000877f0.c).
    const int top = static_cast<int> (thumbTop);
    dragging = position.x >= 0 && position.y >= top && position.x < thumbWidth && position.y < top + thumbHeight;
}

bool FaderModel::mouseDrag (int y) noexcept
{
    if (! dragging)
        return false;

    // thumb y = clamp(y - thumbH / 2, 0, travel); v = max - convertFrom0to1(thumb y / travel)
    // (decomp:NuxFaderComponent/mouseDrag@100087840.c:L22-L41). The original computes
    // end - (start + p * (end - start)); with its fixed 0..99 range that equals max - p * (max - min).
    const float span = std::max (1.0f, travel());
    const float top = std::clamp (static_cast<float> (y - thumbHeight / 2), 0.0f, span);
    const float p = std::clamp (top / span, 0.0f, 1.0f);
    const int before = value();
    current = max - p * (max - min);
    thumbTop = top;
    return value() != before;
}

bool FaderModel::mouseWheel (juce::Point<int> position, float deltaY) noexcept
{
    // Only over the thumb column; (int) value +- ceil/floor(deltaY), clamped
    // (decomp:NuxFaderComponent/mouseWheelMove@100087990.c).
    if (position.x < 0 || position.y < 0 || position.x >= thumbWidth || position.y >= height)
        return false;
    const float step = deltaY > 0.0f ? std::ceil (deltaY) : std::floor (deltaY);
    return setValue (static_cast<float> (value()) + step);
}

// ---- NuxFader ----------------------------------------------------------------------------------

NuxFader::NuxFader (AssetLibrary& assets)
{
    // Ctor: thumb 35x16 showing data_509, component 35x120 (NuxFaderComponent@100088110.c).
    thumbImage = assets.image (defaultThumbImageIndex);
    setSize (35, 120);
    model.setGeometry (120, thumbWidth, thumbHeight);
}

void NuxFader::changed (bool valueChanged, juce::NotificationType notification)
{
    if (! valueChanged)
        return;
    repaint();
    if (notification != juce::dontSendNotification && onValueChange != nullptr)
        onValueChange (model.value());
}

void NuxFader::setValue (int newValue, juce::NotificationType notification)
{
    changed (model.setValue (static_cast<float> (newValue)), notification);
    repaint();
}

void NuxFader::setRange (int minimum, int maximum)
{
    model.setRange (minimum, maximum);
    repaint();
}

void NuxFader::resetDisplay (int width, int height, const juce::Image& thumb, const juce::Image& disabledThumb)
{
    thumbImage = thumb;
    disabledThumbImage = disabledThumb;
    const auto& shown = thumbImageFor (isEnabled(), thumbImage, disabledThumbImage);
    thumbWidth = shown.getWidth() / 2;
    thumbHeight = shown.getHeight() / 2;
    setSize (width, height);
    model.setGeometry (height, thumbWidth, thumbHeight);
    repaint();
}

const juce::Image& NuxFader::thumbImageFor (bool enabled, const juce::Image& normal, const juce::Image& disabled) noexcept
{
    // Deviation: falls back to the normal image when no disabled one is set (showElement does
    // that fallback in the caller, decomp:NuxInstrumentPanel/showElement@100245f50.c case 3).
    return (! enabled && disabled.isValid()) ? disabled : normal;
}

void NuxFader::paint (juce::Graphics& g)
{
    const auto& image = thumbImageFor (isEnabled(), thumbImage, disabledThumbImage);
    if (image.isValid())
        g.drawImage (image,
                     juce::Rectangle<float> (0.0f, std::floor (model.thumbY()), static_cast<float> (thumbWidth),
                                             static_cast<float> (thumbHeight)),
                     juce::RectanglePlacement::centred);
}

void NuxFader::mouseDown (const juce::MouseEvent& e)
{
    model.mouseDown (e.getPosition());
}

void NuxFader::mouseDrag (const juce::MouseEvent& e)
{
    changed (model.mouseDrag (e.getPosition().y), juce::sendNotificationSync);
    repaint();
}

void NuxFader::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    changed (model.mouseWheel (e.getPosition(), wheel.deltaY), juce::sendNotificationSync);
}
} // namespace qtn
