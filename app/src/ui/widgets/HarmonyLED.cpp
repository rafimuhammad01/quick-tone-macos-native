#include "ui/widgets/HarmonyLED.h"

#include <algorithm>

namespace qtn
{
bool HarmonyLedLogic::setIndex (int newIndex) noexcept
{
    const auto clamped = std::clamp (newIndex, minIndex, maxIndex);
    if (clamped == current)
        return false;
    current = clamped;
    return true;
}

bool HarmonyLedLogic::stepUp() noexcept
{
    return current < maxIndex && setIndex (current + 1);
}

bool HarmonyLedLogic::stepDown() noexcept
{
    return current > minIndex && setIndex (current - 1);
}

namespace
{
void setButtonImages (juce::ImageButton& button, const juce::Image& normal, const juce::Image& pressed)
{
    // setImages (normal = over = data_452/454, down = data_451/453), no overlay colours.
    button.setImages (false, true, true, normal, 1.0f, {}, normal, 1.0f, {}, pressed, 1.0f, {});
}
} // namespace

HarmonyLED::HarmonyLED (AssetLibrary& assets)
    : background (assets.image (backgroundImage))
{
    for (int i = 0; i < static_cast<int> (glyphs.size()); ++i)
        glyphs[static_cast<size_t> (i)] = assets.image (glyphImageIndex (i));

    setButtonImages (down, assets.image (downNormalImage), assets.image (downPressedImage));
    setButtonImages (up, assets.image (upNormalImage), assets.image (upPressedImage));
    down.onClick = [this]
    {
        if (logic.stepDown())
            changed (juce::sendNotificationSync);
    };
    up.onClick = [this]
    {
        if (logic.stepUp())
            changed (juce::sendNotificationSync);
    };
    addAndMakeVisible (down);
    addAndMakeVisible (up);
    down.setBounds (0, 61, 26, 18);
    up.setBounds (27, 61, 26, 18);
    setSize (51, 79);
}

void HarmonyLED::setValue (int newValue, juce::NotificationType notification)
{
    if (logic.setIndex (newValue))
        changed (notification);
}

void HarmonyLED::changed (juce::NotificationType notification)
{
    repaint (5, 15, 42, 32);
    if (notification == juce::dontSendNotification || onValueChange == nullptr)
        return;

    if (notification == juce::sendNotificationAsync)
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<HarmonyLED> (this), value = logic.index()]
                                         {
                                             if (safe != nullptr && safe->onValueChange != nullptr)
                                                 safe->onValueChange (value); });
        return;
    }
    onValueChange (logic.index());
}

void HarmonyLED::paint (juce::Graphics& g)
{
    // Background and glyph were ImageComponents (default placement: centred, fitted).
    g.drawImage (background, juce::Rectangle<float> (0.0f, 0.0f, 51.0f, 61.0f), juce::RectanglePlacement::centred);
    g.drawImage (glyphs[static_cast<size_t> (logic.index())], juce::Rectangle<float> (5.0f, 15.0f, 42.0f, 32.0f), juce::RectanglePlacement::centred);
}
} // namespace qtn
