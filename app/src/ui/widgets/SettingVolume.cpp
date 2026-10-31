#include "ui/widgets/SettingVolume.h"

#include "ui/widgets/IRFader.h"

#include <algorithm>

namespace qtn
{
SettingVolume::SettingVolume (AssetLibrary& assets)
    : FaderComponent (assets, geometry(), {IRFader::trackImage, IRFader::thumbNormalImage, IRFader::thumbHoverImage, IRFader::thumbPressedImage})
{
}

FaderGeometry SettingVolume::geometry()
{
    FaderGeometry g;
    g.minValue = 0;
    g.maxValue = 24;
    g.size = {280, 18};
    g.thumb = {0, 0, 19, 18};
    g.minThumbX = 0.0f;
    g.maxThumbX = 261.0f; // width - thumb width
    g.snapThumbToValue = true;
    // y 4.5 and height 9: UNVERIFIED, as for IRFader.
    g.fill = {5.0f, 4.5f, 0.0f, 9.0f};
    return g;
}

juce::String settingVolumeText (int value)
{
    const auto proportion = std::clamp (static_cast<float> (value) / 24.0f, 0.0f, 1.0f);
    return juce::String (static_cast<int> (proportion * 24.0f - 12.0f)) + "dB";
}
} // namespace qtn
