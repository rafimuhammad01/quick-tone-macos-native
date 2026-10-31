#pragma once

#include "ui/widgets/FaderComponent.h"

namespace qtn
{
/** NuxPatchLevelComponent: the P.L editor of the instrument panel (element type 0x60 in
    NuxInstrumentPanel::showElement). Value 0..100; double-click resets to 50.

    Constants (the ctor is inlined in decomp:NuxInstrumentPanel/showElement@100245f50.c):
    - range 0..100, interval 1, skew 1 (L1650-1651)
    - thumb 58x63 at (47,122) (L1677), thumb x 47..374 (L1865-1866: 0x43bb8000 = 374)
    - component 484x280 (L1779-1780); images: background data_467, thumb data_468 for every state
      (L1781-1830)
    - "LEVEL" label: font 20 "Regular", white, centred, at (w/2 - 50, 210, 100, 30)
      (L1687-1775; decomp:NuxPatchLevelComponent/resized@10006ce90.c)
    - fill: colour 0xff52e174 at x 63, y 145, width thumbX - thumbW/2
      (decomp:NuxPatchLevelComponent/paint@10006cd50.c:L54-58)
    - double-click: setValue (50, true) (decomp:NuxPatchLevelComponent/mouseDoubleClick@10006cd30.c)
*/
class PatchLevel : public FaderComponent
{
  public:
    static constexpr int backgroundImage = 467;
    static constexpr int thumbImage = 468;
    static constexpr int defaultValue = 50;
    static constexpr juce::uint32 fillColour = 0xff52e174;

    explicit PatchLevel (AssetLibrary& assets);

    [[nodiscard]] static FaderGeometry geometry();

    /** What a double-click does: value 50, with notification. */
    void resetToDefault();

    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

  private:
    juce::Label levelLabel;
};
} // namespace qtn
