#include "ui/widgets/FaderComponent.h"

namespace qtn
{
namespace
{
constexpr int fillBarScale = 2; // rendered at 2x so it stays sharp on Retina displays
} // namespace

FaderComponent::FaderComponent (AssetLibrary& assets, const FaderGeometry& geometry, const FaderImages& images)
    : logic (geometry),
      background (assets.image (images.background)),
      thumbNormal (assets.image (images.thumbNormal)),
      thumbHover (assets.image (images.thumbHover)),
      thumbPressed (assets.image (images.thumbPressed))
{
    setSize (geometry.size.x, geometry.size.y);
}

void FaderComponent::setValue (int newValue, juce::NotificationType notification)
{
    if (! logic.setValue (newValue))
        return;
    repaint();
    notify (notification);
}

void FaderComponent::notify (juce::NotificationType notification)
{
    if (notification == juce::dontSendNotification || onValueChange == nullptr)
        return;

    if (notification == juce::sendNotificationAsync)
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<FaderComponent> (this), value = logic.value()]
                                         {
                                             if (safe != nullptr && safe->onValueChange != nullptr)
                                                 safe->onValueChange (value); });
        return;
    }
    onValueChange (logic.value());
}

void FaderComponent::resized()
{
    renderFillBar();
}

void FaderComponent::renderFillBar()
{
    // The fill always starts at the same x and ends under the thumb, so one full-width bar is
    // drawn once here and paint() shows its left part.
    const auto& geometry = logic.geometry();
    const auto bar = geometry.fill.withWidth (static_cast<float> (getWidth()) - geometry.fill.getX());
    if (bar.isEmpty())
    {
        fillBar = {};
        return;
    }

    fillBar = juce::Image (juce::Image::ARGB, getWidth() * fillBarScale, getHeight() * fillBarScale, true);
    juce::Graphics g (fillBar);
    g.addTransform (juce::AffineTransform::scale (static_cast<float> (fillBarScale)));
    if (geometry.fillColour != 0)
        g.setColour (juce::Colour (geometry.fillColour));
    else
        g.setGradientFill (juce::ColourGradient (juce::Colour (fillTopColour), 0.0f, 8.0f, juce::Colour (fillBottomColour), 0.0f, 20.0f, false));
    g.fillRoundedRectangle (bar, bar.getHeight() / 2.0f);
}

const juce::Image& FaderComponent::thumbImage() const noexcept
{
    switch (logic.thumbState())
    {
    case ThumbState::hover:
        return thumbHover;
    case ThumbState::pressed:
        return thumbPressed;
    case ThumbState::normal:
        break;
    }
    return thumbNormal;
}

void FaderComponent::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

    if (fillBar.isValid())
    {
        const auto fill = logic.fillBounds().getSmallestIntegerContainer().getIntersection (getLocalBounds());
        if (! fill.isEmpty())
            g.drawImage (fillBar, fill.getX(), fill.getY(), fill.getWidth(), fill.getHeight(), fill.getX() * fillBarScale, fill.getY() * fillBarScale, fill.getWidth() * fillBarScale, fill.getHeight() * fillBarScale);
    }

    g.drawImage (thumbImage(), logic.thumbBounds().toFloat(), juce::RectanglePlacement::centred);
}

void FaderComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto before = logic.thumbState();
    logic.mouseMove (e.getPosition());
    if (logic.thumbState() != before)
        repaint (logic.thumbBounds());
}

void FaderComponent::mouseExit (const juce::MouseEvent&)
{
    logic.mouseExit();
    repaint (logic.thumbBounds());
}

void FaderComponent::mouseDown (const juce::MouseEvent& e)
{
    logic.mouseDown (e.getPosition());
    repaint (logic.thumbBounds());
}

void FaderComponent::mouseDrag (const juce::MouseEvent& e)
{
    const auto changed = logic.mouseDrag (e.getPosition());
    repaint();
    if (changed)
        notify (juce::sendNotificationSync);
}

void FaderComponent::mouseUp (const juce::MouseEvent& e)
{
    logic.mouseUp (e.getPosition());
    repaint (logic.thumbBounds());
}
} // namespace qtn
