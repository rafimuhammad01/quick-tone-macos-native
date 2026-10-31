#include "ui/widgets/PatchLevel.h"

namespace qtn
{
PatchLevel::PatchLevel (AssetLibrary& assets)
    : FaderComponent (assets, geometry(), {backgroundImage, thumbImage, thumbImage, thumbImage})
{
    levelLabel.setText (juce::translate ("LEVEL"), juce::dontSendNotification);
    levelLabel.setFont (juce::Font (juce::FontOptions (20.0f).withStyle ("Regular")));
    levelLabel.setJustificationType (juce::Justification::centred);
    levelLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    levelLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    levelLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (levelLabel);
    resized();
}

FaderGeometry PatchLevel::geometry()
{
    FaderGeometry g;
    g.minValue = 0;
    g.maxValue = 100;
    g.size = {484, 280};
    g.thumb = {47, 122, 58, 63};
    g.minThumbX = 47.0f;
    g.maxThumbX = 374.0f;
    g.snapThumbToValue = false;
    // x 63, y 145 from decomp:NuxPatchLevelComponent/paint@10006cd50.c (0x43110000427c0000);
    // height 9 is UNVERIFIED (argument lost; matches the groove of data_467 at y 145..154).
    g.fill = {63.0f, 145.0f, 0.0f, 9.0f};
    g.fillWidthOffset = -29.0f; // - thumb width / 2
    g.fillColour = fillColour;
    return g;
}

void PatchLevel::resetToDefault()
{
    setValue (defaultValue, juce::sendNotificationSync);
}

void PatchLevel::resized()
{
    FaderComponent::resized();
    levelLabel.setBounds (juce::roundToInt (getWidth() * 0.5f) - 50, 210, 100, 30);
}

void PatchLevel::mouseDoubleClick (const juce::MouseEvent&)
{
    resetToDefault();
}
} // namespace qtn
