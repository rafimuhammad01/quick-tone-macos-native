#pragma once

#include "ui/widgets/FaderComponent.h"

namespace qtn
{
/** NuxIRFaderComponent: IR level fader of the cabinet popup (R5 NuxCabinetPopupMenu, bounds
    (609,687,280,18); CC 0x47). Value 0..40, shown as value - 20 dB.

    Constants: decomp:NuxIRFaderComponent/NuxIRFaderComponent@100064d10.c
    - range 0..40, interval 1, skew 1 (L23-24: 0x4220000000000000, 0x3f8000003f800000)
    - thumb 20x18 at (0,0) (L52), component 280x18 (L133-134), thumb x 0..width-20 (L135-139)
    - images: track data_1187 (L53), thumb normal data_1189 (L66), hover data_1190 (L79),
      pressed data_1188 (L92); state switching in mouseDown/mouseMove/mouseUp
    The NME-300 popup never sets the centred fill mode (+0x1b0 = 1 is set only by
    NuxCabinetParameterPanelMG300MK2), so the fill runs from the left end to the thumb.
*/
class IRFader : public FaderComponent
{
  public:
    static constexpr int trackImage = 1187;
    static constexpr int thumbNormalImage = 1189;
    static constexpr int thumbHoverImage = 1190;
    static constexpr int thumbPressedImage = 1188;

    explicit IRFader (AssetLibrary& assets);

    [[nodiscard]] static FaderGeometry geometry();
};

/** "%ddB" of value - 20 (decomp:NuxCabinetPopupMenu/irFaderValueChanged@100234040.c:L35-37). */
[[nodiscard]] juce::String irLevelText (int value);
} // namespace qtn
