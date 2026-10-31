#include "ui/widgets/NuxToggle.h"

namespace qtn
{
NuxToggle::NuxToggle() = default;

void NuxToggle::setValue (int newValue, juce::NotificationType notification)
{
    const bool newState = newValue != 0;
    if (newState == on)
        return;
    on = newState;
    repaint();
    if (notification != juce::dontSendNotification && onValueChange != nullptr)
        onValueChange (getValue());
}

void NuxToggle::setImages (const juce::Image& offImage, const juce::Image& onImage)
{
    images[0] = offImage;
    images[1] = onImage;
    repaint();
}

void NuxToggle::paint (juce::Graphics& g)
{
    // The state's image scaled to the bounds, half transparent when disabled
    // (decomp:NuxToggle/paintButton@10008f160.c, draw type 2).
    const auto& image = images[imageSlotFor (on)];
    if (! image.isValid())
        return;
    g.setOpacity (opacityFor (isEnabled()));
    g.drawImage (image, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
}

void NuxToggle::mouseDown (const juce::MouseEvent&)
{
    pressed = isEnabled();
}

void NuxToggle::mouseUp (const juce::MouseEvent& e)
{
    // juce::Button semantics (the original is a Button with clicking-toggles-state,
    // showElement@100245f50.c:L1210): click on release inside, when enabled.
    const bool click = pressed && isEnabled() && getLocalBounds().contains (e.getPosition());
    pressed = false;
    if (click)
        setValue (on ? 0 : 1, juce::sendNotificationSync);
}
} // namespace qtn
