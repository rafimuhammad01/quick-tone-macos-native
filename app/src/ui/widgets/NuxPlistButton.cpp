#include "ui/widgets/NuxPlistButton.h"

#include <algorithm>

namespace qtn
{
// ---- PlistModel --------------------------------------------------------------------------------

void PlistModel::setImageCount (int newCount) noexcept
{
    count = std::max (0, newCount);
    if (current >= count)
        current = 0;
}

bool PlistModel::setIndex (int newIndex) noexcept
{
    if (newIndex < 0 || newIndex >= count) // decomp:NuxPlistButton/setIndex@10008bca0.c
        return false;
    current = newIndex;
    return true;
}

std::optional<int> PlistModel::click() noexcept
{
    // index + 1, wrapping to 0 when it reaches the count (decomp:NuxPlistButton/mouseUp@10008b8d0.c).
    if (count == 0)
        return std::nullopt;
    current = current + 1 >= count ? 0 : current + 1;
    return current;
}

// ---- NuxPlistButton ----------------------------------------------------------------------------

NuxPlistButton::NuxPlistButton()
{
    setSize (50, 50); // 0x32 x 0x32 (decomp:NuxPlistButton/NuxPlistButton@10008bd80.c)
}

void NuxPlistButton::setValue (int newValue, juce::NotificationType notification)
{
    if (newValue == model.index() || ! model.setIndex (newValue))
        return;
    repaint();
    if (notification != juce::dontSendNotification && onValueChange != nullptr)
        onValueChange (model.index());
}

void NuxPlistButton::setImages (const juce::Image& first, const juce::Image& second, const juce::Image& third)
{
    images = {first, second, third};
    model.setImageCount (static_cast<int> (images.size()));
    repaint();
}

void NuxPlistButton::paint (juce::Graphics& g)
{
    // Child ImageComponent filling the bounds, default (centred) placement.
    const auto& image = images[static_cast<size_t> (model.index())];
    if (image.isValid())
        g.drawImage (image, getLocalBounds().toFloat(), juce::RectanglePlacement::centred);
}

void NuxPlistButton::mouseUp (const juce::MouseEvent&)
{
    // The original cycles on any mouse-up (no hit test, no enabled check) and always notifies.
    if (const auto next = model.click())
    {
        repaint();
        if (onValueChange != nullptr)
            onValueChange (*next);
    }
}
} // namespace qtn
