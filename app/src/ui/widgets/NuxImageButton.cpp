#include "ui/widgets/NuxImageButton.h"

namespace qtn
{
ButtonImage selectButtonImage (bool isDown, bool toggled, bool isOver, bool hasOverImage, bool hasDownImage) noexcept
{
    // juce::ImageButton::getCurrentImage / getOverImage / getDownImage (JUCE 8 juce_ImageButton.cpp).
    const auto overOrNormal = hasOverImage ? ButtonImage::over : ButtonImage::normal;
    if (isDown || toggled)
        return hasDownImage ? ButtonImage::down : overOrNormal;
    if (isOver)
        return overOrNormal;
    return ButtonImage::normal;
}

NuxImageButton::NuxImageButton() = default;

void NuxImageButton::setValue (int newValue, juce::NotificationType notification)
{
    const bool newState = newValue != 0;
    if (newState == toggled)
        return;
    toggled = newState;
    repaint();
    if (notification != juce::dontSendNotification && onValueChange != nullptr)
        onValueChange (getValue());
}

void NuxImageButton::setImages (const juce::Image& normal, const juce::Image& overImg, const juce::Image& downImg)
{
    normalImage = normal;
    overImage = overImg;
    downImage = downImg;
    repaint();
}

ButtonImage NuxImageButton::currentImage() const noexcept
{
    return selectButtonImage (down, toggled, over, overImage.isValid(), downImage.isValid());
}

void NuxImageButton::paint (juce::Graphics& g)
{
    const auto which = currentImage();
    const auto& image = which == ButtonImage::down ? downImage : which == ButtonImage::over ? overImage
                                                                                            : normalImage;
    if (image.isValid())
        g.drawImage (image, getLocalBounds().toFloat(), juce::RectanglePlacement::centred);
}

void NuxImageButton::mouseEnter (const juce::MouseEvent&)
{
    // The original also shows the translated tips text here (CommonCode::showGlobalTips); the tips
    // bubble belongs to the caller, which reads getTips().
    over = true;
    repaint();
}

void NuxImageButton::mouseExit (const juce::MouseEvent&)
{
    over = false;
    repaint();
}

void NuxImageButton::mouseDown (const juce::MouseEvent&)
{
    down = isEnabled();
    repaint();
}

void NuxImageButton::mouseUp (const juce::MouseEvent& e)
{
    const bool click = down && isEnabled() && getLocalBounds().contains (e.getPosition());
    down = false;
    repaint();
    if (! click)
        return;
    if (clickTogglesState)
        setValue (toggled ? 0 : 1, juce::sendNotificationSync);
    if (onClick != nullptr)
        onClick();
}
} // namespace qtn
