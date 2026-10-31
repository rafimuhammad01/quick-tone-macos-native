#include "ui/widgets/NuxVolume.h"

#include "ui/laf/AssetLibrary.h"

#include <algorithm>

namespace qtn
{
// ---- VolumeModel -------------------------------------------------------------------------------

void VolumeModel::placeThumb (float value) noexcept
{
    // x = value / (max - min) * (xMax - xMin) (decomp:NuxVolumeComponent/setVolume@10007c650.c:L48-L50).
    thumbLeft = value / maximum * travel();
}

bool VolumeModel::takeValueFromThumb() noexcept
{
    // (uint)((max - min) * (x / (xMax - xMin))) & 0xff (updateCurrentVolume@10007c7d0.c:L36-L38).
    const int before = current;
    current = static_cast<int> (static_cast<unsigned> (maximum * (thumbLeft / travel())) & 0xffu);
    return current != before;
}

bool VolumeModel::setValue (int newValue) noexcept
{
    const int clamped = std::clamp (newValue, 0, static_cast<int> (maximum));
    placeThumb (static_cast<float> (clamped));
    const bool changed = clamped != current;
    current = clamped;
    return changed;
}

bool VolumeModel::onThumb (juce::Point<int> position) const noexcept
{
    const int left = static_cast<int> (thumbLeft);
    return position.x >= left && position.y >= thumbTop && position.x < left + thumbWidth && position.y < thumbTop + thumbHeight;
}

void VolumeModel::mouseMove (juce::Point<int> position) noexcept
{
    image = onThumb (position) ? ThumbImage::over : ThumbImage::normal; // mouseMove@10007c210.c
}

void VolumeModel::mouseDown (juce::Point<int> position) noexcept
{
    dragging = onThumb (position); // mouseDown@10007c2e0.c
    if (dragging)
        image = ThumbImage::down;
}

bool VolumeModel::mouseDrag (int x) noexcept
{
    if (! dragging)
        return false;
    // x = clamp(mouseX - thumbW / 2, 0, xMax) (mouseDrag@10007c3a0.c:L13-L19).
    thumbLeft = std::clamp (static_cast<float> (x - thumbWidth / 2), 0.0f, travel());
    return takeValueFromThumb();
}

void VolumeModel::mouseUp (juce::Point<int> position) noexcept
{
    dragging = false;
    image = onThumb (position) ? ThumbImage::over : ThumbImage::normal; // mouseUp@10007c430.c
}

bool VolumeModel::doubleClick() noexcept
{
    // setVolume(convertFrom0to1(0.5)) on the 0..100 range at +0xf0, then the value is read back
    // from the thumb position (mouseDoubleClick@10007c4f0.c; range SFW:L452-L455).
    placeThumb (50.0f);
    return takeValueFromThumb();
}

juce::String VolumeModel::tipText() const
{
    // value / 100 (range +0xf0, 0..100) -> -12..+12 dB (range +0x1b0, SFW:L462-L463), "%.2fdB".
    const double db = -12.0 + 24.0 * (static_cast<double> (current) / 100.0);
    return juce::String::formatted ("%.2fdB", db);
}

juce::Rectangle<float> VolumeModel::barArea() const noexcept
{
    return {3.0f, 13.0f, static_cast<float> (static_cast<int> (thumbLeft)), 5.0f};
}

// ---- NuxVolume ---------------------------------------------------------------------------------

NuxVolume::NuxVolume (AssetLibrary& assets)
    : track (assets.image (trackImageIndex)),
      thumbNormal (assets.image (thumbNormalImageIndex)),
      thumbOver (assets.image (thumbOverImageIndex)),
      thumbDown (assets.image (thumbDownImageIndex))
{
    setSize (VolumeModel::width, VolumeModel::height);
    renderBackground();
}

void NuxVolume::renderBackground()
{
    // Track stretched to the bounds, then the gradient bar (decomp:NuxVolumeComponent/paint@10007c530.c;
    // geometry asm@0x10007c5ee). Rendered at 2x once per value change so paint() never allocates.
    constexpr int scale = 2;
    background = juce::Image (juce::Image::ARGB, VolumeModel::width * scale, VolumeModel::height * scale, true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (static_cast<float> (scale)));
    if (track.isValid())
        g.drawImage (track, juce::Rectangle<float> (0.0f, 0.0f, VolumeModel::width, VolumeModel::height),
                     juce::RectanglePlacement::stretchToFit);
    g.setGradientFill (juce::ColourGradient (juce::Colour (barTopColour), 0.0f, 8.0f, juce::Colour (barBottomColour),
                                             0.0f, 20.0f, false));
    g.fillRoundedRectangle (model.barArea(), barCorner);
}

const juce::Image& NuxVolume::currentThumb() const noexcept
{
    switch (model.thumbImage())
    {
    case VolumeModel::ThumbImage::over:
        return thumbOver;
    case VolumeModel::ThumbImage::down:
        return thumbDown;
    case VolumeModel::ThumbImage::normal:
        break;
    }
    return thumbNormal;
}

void NuxVolume::changed (bool valueChanged, juce::NotificationType notification)
{
    if (! valueChanged)
        return;
    renderBackground();
    repaint();
    if (notification != juce::dontSendNotification && onValueChange != nullptr)
        onValueChange (model.value());
}

void NuxVolume::setValue (int newValue, juce::NotificationType notification)
{
    changed (model.setValue (newValue), notification);
}

void NuxVolume::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    const auto& thumb = currentThumb();
    if (thumb.isValid())
        g.drawImage (thumb,
                     juce::Rectangle<float> (static_cast<float> (static_cast<int> (model.thumbX())),
                                             static_cast<float> (VolumeModel::thumbTop),
                                             static_cast<float> (VolumeModel::thumbWidth),
                                             static_cast<float> (VolumeModel::thumbHeight)),
                     juce::RectanglePlacement::centred);
}

void NuxVolume::mouseMove (const juce::MouseEvent& e)
{
    model.mouseMove (e.getPosition());
    repaint();
}

void NuxVolume::mouseDown (const juce::MouseEvent& e)
{
    model.mouseDown (e.getPosition());
    repaint();
}

void NuxVolume::mouseDrag (const juce::MouseEvent& e)
{
    changed (model.mouseDrag (e.getPosition().x), juce::sendNotificationSync);
    renderBackground(); // the bar follows the thumb even when the integer value stays the same
    repaint();
}

void NuxVolume::mouseUp (const juce::MouseEvent& e)
{
    model.mouseUp (e.getPosition());
    repaint();
}

void NuxVolume::mouseDoubleClick (const juce::MouseEvent&)
{
    changed (model.doubleClick(), juce::sendNotificationSync);
    renderBackground();
    repaint();
}
} // namespace qtn
