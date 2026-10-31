#include "ui/widgets/IRFader.h"

namespace qtn
{
IRFader::IRFader (AssetLibrary& assets)
    : FaderComponent (assets, geometry(), {trackImage, thumbNormalImage, thumbHoverImage, thumbPressedImage})
{
}

FaderGeometry IRFader::geometry()
{
    FaderGeometry g;
    g.minValue = 0;
    g.maxValue = 40;
    g.size = {280, 18};
    g.thumb = {0, 0, 20, 18};
    g.minThumbX = 0.0f;
    g.maxThumbX = 260.0f; // width - thumb width
    g.snapThumbToValue = true;
    // x 4.5 from decomp:NuxIRFaderComponent/paint@100064640.c:L75-78; y 4.5 and height 9 are
    // UNVERIFIED (arguments lost in the decomp): centred on the 18 px track, radius 4.5 = h / 2.
    g.fill = {4.5f, 4.5f, 0.0f, 9.0f};
    return g;
}

juce::String irLevelText (int value)
{
    return juce::String (value - 20) + "dB";
}
} // namespace qtn
