#pragma once

#include "ui/widgets/FaderComponent.h"

namespace qtn
{
/** NuxSettingVolumeComponent: USB recording / playback level in the settings panel (R5
    NuxSettingPanel +0x308/+0x310, CC 0x44/0x45). Value 0..24, shown as value - 12 dB.

    Constants: decomp:NuxSettingVolumeComponent/NuxSettingVolumeComponent@100077220.c
    - range 0..24, interval 1, skew 1 (0x41c0000000000000, 0x3f8000003f800000)
    - thumb 19x18 at (0,0), component 280x18, thumb x 0..width-19
    - images: track data_1187, thumb normal data_1189, hover data_1190, pressed data_1188
    Fill bar x = 5 (decomp:NuxSettingVolumeComponent/paint@100076d90.c:L45-46).
*/
class SettingVolume : public FaderComponent
{
  public:
    explicit SettingVolume (AssetLibrary& assets);

    [[nodiscard]] static FaderGeometry geometry();
};

/** "%ddB" of (int) (clamp01 (value / 24) * 24 - 12)
    (decomp:NuxSettingPanel/volumeValueChanged@100278310.c:L59-77). */
[[nodiscard]] juce::String settingVolumeText (int value);
} // namespace qtn
