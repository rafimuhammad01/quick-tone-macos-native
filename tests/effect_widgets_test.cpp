// T7a widgets used on the NME-300 path: HarmonyLED, PatchLevel, SettingVolume, IRFader, and the
// shared horizontal-fader logic. Constants are cited in the widget headers.
#include "ui/laf/AssetLibrary.h"
#include "ui/widgets/FaderLogic.h"
#include "ui/widgets/HarmonyLED.h"
#include "ui/widgets/IRFader.h"
#include "ui/widgets/PatchLevel.h"
#include "ui/widgets/SettingVolume.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>
#include <vector>

namespace
{
/** A library whose manifest is empty: every image is the 16x16 placeholder. Enough for the
    behaviour tests, which never look at pixels. */
qtn::AssetLibrary emptyLibrary()
{
    auto parsed = qtn::AssetManifest::parse ("index,filename,size\n");
    REQUIRE (parsed.manifest.has_value());
    return qtn::AssetLibrary (std::move (*parsed.manifest), juce::File());
}

bool realAssetsAvailable()
{
    return juce::String (QTN_ASSETS_DIR).isNotEmpty() && juce::File (QTN_ASSET_MANIFEST).existsAsFile();
}

std::unique_ptr<qtn::AssetLibrary> realLibrary()
{
    auto parsed = qtn::AssetManifest::parse (juce::File (QTN_ASSET_MANIFEST).loadFileAsString());
    INFO (parsed.error);
    REQUIRE (parsed.manifest.has_value());
    return std::make_unique<qtn::AssetLibrary> (std::move (*parsed.manifest),
                                                juce::File (QTN_ASSETS_DIR).getChildFile ("named"));
}

bool isPlaceholder (const juce::Image& image)
{
    return image.getWidth() == 16 && image.getHeight() == 16 && image.getPixelAt (0, 0) == juce::Colours::magenta;
}

/** Records every onValueChange call. */
struct Recorder
{
    std::vector<int> values;
    std::function<void (int)> callback()
    {
        return [this] (int v)
        { values.push_back (v); };
    }
};

const qtn::FaderGeometry irGeometry = qtn::IRFader::geometry();
const qtn::FaderGeometry patchGeometry = qtn::PatchLevel::geometry();
} // namespace

// ---- fader logic -----------------------------------------------------------------------------

TEST_CASE ("ir_fader_geometry_follows_decomp")
{
    // NuxIRFaderComponent ctor: range 0..40, 280x18, thumb 20x18, thumb x from 0 to 280-20.
    CHECK (irGeometry.minValue == 0);
    CHECK (irGeometry.maxValue == 40);
    CHECK (irGeometry.size == juce::Point<int> (280, 18));
    CHECK (irGeometry.thumb == juce::Rectangle<int> (0, 0, 20, 18));
    CHECK (irGeometry.minThumbX == 0.0f);
    CHECK (irGeometry.maxThumbX == 260.0f);
}

TEST_CASE ("fader_thumb_x_for_value_is_linear_and_truncated")
{
    CHECK (qtn::FaderLogic::thumbXForValue (irGeometry, 0) == 0);
    CHECK (qtn::FaderLogic::thumbXForValue (irGeometry, 20) == 130);
    CHECK (qtn::FaderLogic::thumbXForValue (irGeometry, 40) == 260);
    CHECK (qtn::FaderLogic::thumbXForValue (irGeometry, 1) == 6); // 6.5 truncated

    // Patch level: x = (374 - 47) * v / 100 + 47, truncated.
    CHECK (qtn::FaderLogic::thumbXForValue (patchGeometry, 0) == 47);
    CHECK (qtn::FaderLogic::thumbXForValue (patchGeometry, 50) == 210);
    CHECK (qtn::FaderLogic::thumbXForValue (patchGeometry, 100) == 374);
}

TEST_CASE ("fader_value_for_thumb_x_truncates_and_clamps")
{
    CHECK (qtn::FaderLogic::valueForThumbX (irGeometry, 0.0f) == 0);
    CHECK (qtn::FaderLogic::valueForThumbX (irGeometry, 129.0f) == 19); // 19.85 truncated
    CHECK (qtn::FaderLogic::valueForThumbX (irGeometry, 130.0f) == 20);
    CHECK (qtn::FaderLogic::valueForThumbX (irGeometry, 260.0f) == 40);
    CHECK (qtn::FaderLogic::valueForThumbX (irGeometry, -50.0f) == 0);
    CHECK (qtn::FaderLogic::valueForThumbX (irGeometry, 999.0f) == 40);

    CHECK (qtn::FaderLogic::valueForThumbX (patchGeometry, 47.0f) == 0);
    CHECK (qtn::FaderLogic::valueForThumbX (patchGeometry, 374.0f) == 100);
}

TEST_CASE ("fader_set_value_clamps_and_reports_change")
{
    qtn::FaderLogic fader (irGeometry);
    CHECK (fader.value() == 0);

    CHECK (fader.setValue (25));
    CHECK (fader.value() == 25);
    CHECK (fader.thumbBounds().getX() == 162); // 260 * 25 / 40 = 162.5

    CHECK_FALSE (fader.setValue (25));
    CHECK (fader.setValue (41));
    CHECK (fader.value() == 40);
    CHECK (fader.setValue (-3));
    CHECK (fader.value() == 0);
}

TEST_CASE ("fader_drag_starts_only_on_thumb")
{
    qtn::FaderLogic fader (irGeometry);
    fader.setValue (20); // thumb at x 130..150

    fader.mouseDown ({100, 5}); // on the track, not the thumb
    CHECK_FALSE (fader.mouseDrag ({200, 5}));
    CHECK (fader.value() == 20);

    fader.mouseDown ({135, 5});
    CHECK (fader.thumbState() == qtn::ThumbState::pressed);
    CHECK (fader.mouseDrag ({200, 5})); // thumb x = 200 - 10 = 190 -> 40 * 190 / 260 = 29.2
    CHECK (fader.value() == 29);
}

TEST_CASE ("fader_drag_clamps_to_track_ends")
{
    qtn::FaderLogic fader (irGeometry);
    fader.mouseDown ({5, 5});
    CHECK (fader.mouseDrag ({1000, 5}));
    CHECK (fader.value() == 40);
    CHECK (fader.thumbBounds().getX() == 260);
    CHECK (fader.mouseDrag ({-1000, 5}));
    CHECK (fader.value() == 0);
    CHECK (fader.thumbBounds().getX() == 0);
}

TEST_CASE ("fader_drag_reports_only_value_changes")
{
    qtn::FaderLogic fader (irGeometry);
    fader.mouseDown ({5, 5});
    CHECK (fader.mouseDrag ({20, 5}));       // x 10 -> 1
    CHECK_FALSE (fader.mouseDrag ({21, 5})); // x 11 -> 1.69 -> 1
    CHECK (fader.value() == 1);
}

TEST_CASE ("ir_fader_thumb_snaps_to_value_while_dragging")
{
    qtn::FaderLogic fader (irGeometry);
    fader.mouseDown ({5, 5});
    fader.mouseDrag ({21, 5}); // x 11 -> value 1 -> thumb x 6
    CHECK (fader.thumbBounds().getX() == 6);
}

TEST_CASE ("patch_level_thumb_follows_mouse_while_dragging")
{
    // NuxPatchLevelComponent::mouseDrag sets the thumb at the mouse and does not snap it.
    qtn::FaderLogic fader (patchGeometry);
    fader.mouseDown ({50, 130});
    CHECK (fader.mouseDrag ({100, 130})); // x = 100 - 29 = 71 -> 100 * 24 / 327 = 7.3
    CHECK (fader.value() == 7);
    CHECK (fader.thumbBounds().getX() == 71);
}

TEST_CASE ("fader_thumb_state_follows_hover_and_release")
{
    qtn::FaderLogic fader (irGeometry);
    CHECK (fader.thumbState() == qtn::ThumbState::normal);
    fader.mouseMove ({5, 5});
    CHECK (fader.thumbState() == qtn::ThumbState::hover);
    fader.mouseMove ({100, 5});
    CHECK (fader.thumbState() == qtn::ThumbState::normal);
    fader.mouseDown ({5, 5});
    CHECK (fader.thumbState() == qtn::ThumbState::pressed);
    fader.mouseUp ({5, 5});
    CHECK (fader.thumbState() == qtn::ThumbState::hover);
    fader.mouseExit();
    CHECK (fader.thumbState() == qtn::ThumbState::normal);
}

TEST_CASE ("fader_fill_bar_ends_under_the_thumb")
{
    qtn::FaderLogic ir (irGeometry);
    ir.setValue (20);
    CHECK (ir.fillBounds() == juce::Rectangle<float> (4.5f, 4.5f, 130.0f, 9.0f));

    qtn::FaderLogic volume (qtn::SettingVolume::geometry());
    volume.setValue (12);
    CHECK (volume.fillBounds().getX() == 5.0f);
    CHECK (volume.fillBounds().getWidth() == static_cast<float> (volume.thumbBounds().getX()));

    qtn::FaderLogic patch (patchGeometry);
    patch.setValue (50); // thumb x 210, fill width 210 - 58 / 2
    CHECK (patch.fillBounds() == juce::Rectangle<float> (63.0f, 145.0f, 181.0f, 9.0f));
}

// ---- texts ------------------------------------------------------------------------------------

TEST_CASE ("ir_level_text_is_value_minus_20_db")
{
    // NuxCabinetPopupMenu::irFaderValueChanged: "%ddB" with value - 20.
    CHECK (qtn::irLevelText (0) == "-20dB");
    CHECK (qtn::irLevelText (20) == "0dB");
    CHECK (qtn::irLevelText (40) == "20dB");
}

TEST_CASE ("setting_volume_text_is_value_minus_12_db")
{
    // NuxSettingPanel::volumeValueChanged: "%ddB" with clamp(v / 24) * 24 - 12.
    CHECK (qtn::settingVolumeText (0) == "-12dB");
    CHECK (qtn::settingVolumeText (12) == "0dB");
    CHECK (qtn::settingVolumeText (24) == "12dB");
    CHECK (qtn::settingVolumeText (30) == "12dB");
}

TEST_CASE ("setting_volume_geometry_follows_decomp")
{
    const auto g = qtn::SettingVolume::geometry();
    CHECK (g.minValue == 0);
    CHECK (g.maxValue == 24);
    CHECK (g.size == juce::Point<int> (280, 18));
    CHECK (g.thumb == juce::Rectangle<int> (0, 0, 19, 18));
    CHECK (g.maxThumbX == 261.0f);
}

TEST_CASE ("patch_level_geometry_follows_decomp")
{
    CHECK (patchGeometry.minValue == 0);
    CHECK (patchGeometry.maxValue == 100);
    CHECK (patchGeometry.size == juce::Point<int> (484, 280));
    CHECK (patchGeometry.thumb == juce::Rectangle<int> (47, 122, 58, 63));
    CHECK (patchGeometry.minThumbX == 47.0f);
    CHECK (patchGeometry.maxThumbX == 374.0f);
}

// ---- harmony LED logic ------------------------------------------------------------------------

TEST_CASE ("harmony_led_steps_within_0_to_11")
{
    qtn::HarmonyLedLogic led;
    CHECK (led.index() == 0);
    CHECK_FALSE (led.stepDown());
    CHECK (led.stepUp());
    CHECK (led.index() == 1);

    led.setIndex (11);
    CHECK_FALSE (led.stepUp());
    CHECK (led.index() == 11);
    CHECK (led.stepDown());
    CHECK (led.index() == 10);
}

TEST_CASE ("harmony_led_set_index_clamps")
{
    qtn::HarmonyLedLogic led;
    CHECK (led.setIndex (99));
    CHECK (led.index() == 11);
    CHECK (led.setIndex (-4));
    CHECK (led.index() == 0);
    CHECK_FALSE (led.setIndex (0));
}

TEST_CASE ("harmony_led_glyph_images_are_414_to_425")
{
    // NuxHarmonyLEDComponent(2) (the NME-300 instrument panel's type) loads data_414..425.
    CHECK (qtn::HarmonyLED::glyphImageIndex (0) == 414);
    CHECK (qtn::HarmonyLED::glyphImageIndex (11) == 425);
}

// ---- components: clamping and notifications --------------------------------------------------

TEST_CASE ("ir_fader_component_clamps_and_notifies")
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto library = emptyLibrary();
    qtn::IRFader fader (library);
    Recorder recorder;
    fader.onValueChange = recorder.callback();

    CHECK (fader.getWidth() == 280);
    CHECK (fader.getHeight() == 18);

    fader.setValue (55, juce::dontSendNotification);
    CHECK (fader.getValue() == 40);
    CHECK (recorder.values.empty());

    fader.setValue (10, juce::sendNotificationSync);
    CHECK (fader.getValue() == 10);
    CHECK (recorder.values == std::vector<int>{10});

    fader.setValue (10, juce::sendNotificationSync); // unchanged: no callback
    CHECK (recorder.values.size() == 1);
}

TEST_CASE ("setting_volume_component_clamps_and_notifies")
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto library = emptyLibrary();
    qtn::SettingVolume volume (library);
    Recorder recorder;
    volume.onValueChange = recorder.callback();

    volume.setValue (-1, juce::sendNotificationSync);
    CHECK (volume.getValue() == 0);
    CHECK (recorder.values.empty()); // already 0

    volume.setValue (30, juce::sendNotificationSync);
    CHECK (volume.getValue() == 24);
    CHECK (recorder.values == std::vector<int>{24});
}

TEST_CASE ("patch_level_component_clamps_and_notifies")
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto library = emptyLibrary();
    qtn::PatchLevel level (library);
    Recorder recorder;
    level.onValueChange = recorder.callback();

    CHECK (level.getWidth() == 484);
    CHECK (level.getHeight() == 280);

    level.setValue (120, juce::sendNotificationSync);
    CHECK (level.getValue() == 100);
    level.setValue (30, juce::dontSendNotification);
    CHECK (level.getValue() == 30);
    CHECK (recorder.values == std::vector<int>{100});
}

TEST_CASE ("patch_level_double_click_resets_to_50")
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto library = emptyLibrary();
    qtn::PatchLevel level (library);
    Recorder recorder;
    level.onValueChange = recorder.callback();

    level.resetToDefault();
    CHECK (level.getValue() == 50);
    CHECK (recorder.values == std::vector<int>{50});
}

TEST_CASE ("harmony_led_component_clamps_and_notifies")
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto library = emptyLibrary();
    qtn::HarmonyLED led (library);
    Recorder recorder;
    led.onValueChange = recorder.callback();

    CHECK (led.getWidth() == 51);
    CHECK (led.getHeight() == 79);

    led.setValue (12, juce::sendNotificationSync);
    CHECK (led.getValue() == 11);
    CHECK (recorder.values == std::vector<int>{11});

    led.setValue (3, juce::dontSendNotification);
    CHECK (led.getValue() == 3);
    CHECK (recorder.values.size() == 1);
}

TEST_CASE ("harmony_led_buttons_step_and_notify")
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto library = emptyLibrary();
    qtn::HarmonyLED led (library);
    Recorder recorder;
    led.onValueChange = recorder.callback();

    // Button::triggerClick posts a message; call the click handlers directly instead.
    led.downButton().onClick(); // at 0: no change, no callback
    led.upButton().onClick();
    led.upButton().onClick();
    led.downButton().onClick();
    CHECK (led.getValue() == 1);
    CHECK (recorder.values == std::vector<int>{1, 2, 1});
}

// ---- real assets ------------------------------------------------------------------------------

TEST_CASE ("effect_widget_images_resolve_from_real_assets")
{
    if (! realAssetsAvailable())
        SKIP ("QTN_ASSETS_DIR not set or missing");

    const auto library = realLibrary();
    std::vector<int> indices{qtn::IRFader::trackImage, qtn::IRFader::thumbNormalImage, qtn::IRFader::thumbHoverImage, qtn::IRFader::thumbPressedImage, qtn::PatchLevel::backgroundImage, qtn::PatchLevel::thumbImage, qtn::HarmonyLED::backgroundImage, qtn::HarmonyLED::downNormalImage, qtn::HarmonyLED::downPressedImage, qtn::HarmonyLED::upNormalImage, qtn::HarmonyLED::upPressedImage};
    for (int i = 0; i < 12; ++i)
        indices.push_back (qtn::HarmonyLED::glyphImageIndex (i));

    for (const auto index : indices)
    {
        INFO ("data_" << index);
        const auto image = library->image (index);
        CHECK (image.isValid());
        CHECK_FALSE (isPlaceholder (image));
    }
}
