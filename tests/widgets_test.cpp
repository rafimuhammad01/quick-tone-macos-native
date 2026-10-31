// T6 base widgets. Constants come from the decomp of each original class (cited next to each
// check) and from research/R5-ui.md.

#include "ui/laf/AssetLibrary.h"
#include "ui/widgets/NuxFader.h"
#include "ui/widgets/NuxImageButton.h"
#include "ui/widgets/NuxKnob.h"
#include "ui/widgets/NuxPlistButton.h"
#include "ui/widgets/NuxToggle.h"
#include "ui/widgets/NuxVolume.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace
{
juce::MouseEvent mouseEvent (juce::Component& target, juce::Point<float> position, juce::Point<float> downPosition,
                             int clicks = 1, bool dragged = false)
{
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), position, juce::ModifierKeys(),
                             juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target,
                             juce::Time(), downPosition, juce::Time(), clicks, dragged);
}

juce::MouseEvent mouseAt (juce::Component& target, float x, float y)
{
    return mouseEvent (target, {x, y}, {x, y});
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

std::unique_ptr<qtn::AssetLibrary> emptyLibrary()
{
    auto parsed = qtn::AssetManifest::parse ("index,filename,size\n");
    REQUIRE (parsed.manifest.has_value());
    return std::make_unique<qtn::AssetLibrary> (std::move (*parsed.manifest),
                                                juce::File::getSpecialLocation (juce::File::tempDirectory));
}

juce::Image solidImage (int w, int h)
{
    juce::Image image (juce::Image::ARGB, w, h, true);
    image.clear (image.getBounds(), juce::Colours::white);
    return image;
}
} // namespace

// ---- NuxKnob ----------------------------------------------------------------------------------

TEST_CASE ("knob_clamps_to_range")
{
    qtn::KnobModel knob; // ctor range 0..100 (decomp:NuxKnob/NuxKnob@10008aa40.c:L34)
    CHECK (knob.minimum() == 0);
    CHECK (knob.maximum() == 100);

    knob.setValue (150);
    CHECK (knob.value() == 100);
    knob.setValue (-5);
    CHECK (knob.value() == 0);

    knob.setRange (10, 20);
    knob.setValue (5);
    CHECK (knob.value() == 10);
    knob.setValue (25);
    CHECK (knob.value() == 20);
    knob.setValue (14.5); // snapped like setValue's floor(v + 0.5)
    CHECK (knob.value() == 15);
}

TEST_CASE ("knob_drag_maps_like_original")
{
    // Straight mode: proportion += ((downY - y) + (x - downX)) * 0.005, from the value at mouse-down
    // (decomp:NuxKnob/mouseDrag@1000897b0.c:L302). The first drag event after mouse-down is skipped (L56).
    qtn::KnobModel knob;
    knob.setDragMode (qtn::KnobDragMode::straight);
    knob.setValue (50);
    const juce::Point<int> size{40, 40};

    knob.mouseDown ({20.0f, 20.0f});
    CHECK_FALSE (knob.mouseDrag ({20.0f, 0.0f}, size, true)); // first drag ignored
    CHECK (knob.value() == 50);

    CHECK (knob.mouseDrag ({20.0f, 0.0f}, size, true)); // 20 px up -> +0.1
    CHECK (knob.value() == 60);
    CHECK (knob.mouseDrag ({30.0f, 0.0f}, size, true)); // + 10 px right -> +0.15 total
    CHECK (knob.value() == 65);
    knob.mouseDrag ({20.0f, 400.0f}, size, true); // far down -> clamped
    CHECK (knob.value() == 0);
}

TEST_CASE ("knob_round_drag_follows_angle")
{
    // Round mode: angle = atan2(dx, -dy) around the centre, outside a 5 px dead zone, mapped from
    // start..end radians = (deg / 180 + 1) * pi (decomp:NuxKnob/mouseDrag@1000897b0.c:L63-L64;
    // decomp:NuxInstrumentPanel/showElement@100245f50.c:L436-L437).
    qtn::KnobModel knob;
    knob.setDragMode (qtn::KnobDragMode::round);
    knob.setValue (50);
    const juce::Point<int> size{40, 40};

    knob.mouseDown ({20.0f, 0.0f});
    knob.mouseDrag ({20.0f, 0.0f}, size, false); // skipped first event

    CHECK_FALSE (knob.mouseDrag ({22.0f, 22.0f}, size, true)); // inside the dead zone
    CHECK (knob.value() == 50);

    CHECK (knob.mouseDrag ({40.0f, 20.0f}, size, true)); // pointing right: 3/4 of the sweep
    CHECK (knob.value() == 75);
    CHECK (knob.mouseDrag ({0.0f, 20.0f}, size, true)); // pointing left: 1/4
    CHECK (knob.value() == 25);
}

TEST_CASE ("knob_filmstrip_frame_for_value")
{
    // frame = ceil(start/3) + ceil((end-start)/3 * p); 120 wraps to 0; 12 frames per row, square
    // frames of stripWidth/12 (decomp:NuxKnob/paint@10008a0b0.c:L79-L93).
    qtn::KnobModel knob;
    knob.setValue (0);
    CHECK (knob.frameIndex() == 0);
    knob.setValue (1);
    CHECK (knob.frameIndex() == 2);
    knob.setValue (50);
    CHECK (knob.frameIndex() == 60);
    knob.setValue (100);
    CHECK (knob.frameIndex() == 0);

    knob.setAngles (30.0f, 330.0f);
    knob.setValue (50);
    CHECK (knob.frameIndex() == 60);
    knob.setValue (0);
    CHECK (knob.frameIndex() == 10);

    CHECK (qtn::KnobModel::frameSourceArea (61, 3000) == juce::Rectangle<int> (250, 1250, 250, 250));
    CHECK (qtn::KnobModel::frameSourceArea (0, 2640) == juce::Rectangle<int> (0, 0, 220, 220));
}

TEST_CASE ("knob_wheel_steps_one_unit_and_double_click_resets")
{
    // Wheel: ceil(deltaY) when > 0 else floor(deltaY) (asm@0x10008a039); double-click: default (L13).
    qtn::KnobModel knob;
    knob.setValue (50);
    CHECK (knob.mouseWheel (0.3f));
    CHECK (knob.value() == 51);
    CHECK (knob.mouseWheel (-0.3f));
    CHECK (knob.value() == 50);
    knob.setValue (100);
    CHECK_FALSE (knob.mouseWheel (0.3f));
    CHECK (knob.value() == 100);

    knob.setDefaultValue (30);
    CHECK (knob.doubleClick());
    CHECK (knob.value() == 30);
}

TEST_CASE ("knob_component_notifies_on_drag")
{
    juce::ScopedJuceInitialiser_GUI gui;
    qtn::NuxKnob knob;
    knob.setSize (40, 40);
    knob.setDragMode (qtn::KnobDragMode::straight); // the ctor default is round (+0x260 = 1)
    knob.setValue (50, juce::dontSendNotification);
    Recorder recorder;
    knob.onValueChange = recorder.callback();

    knob.mouseDown (mouseAt (knob, 20, 20));
    knob.mouseDrag (mouseEvent (knob, {20, 10}, {20, 20}, 1, true)); // skipped
    knob.mouseDrag (mouseEvent (knob, {20, 0}, {20, 20}, 1, true));
    knob.mouseDrag (mouseEvent (knob, {20, 0}, {20, 20}, 1, true)); // no change, no repeat
    CHECK (recorder.values == std::vector<int>{60});
}

// ---- notification contract --------------------------------------------------------------------

TEST_CASE ("set_value_dont_send_does_not_notify")
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto assets = emptyLibrary();
    Recorder recorder;

    auto check = [&recorder] (auto& widget, int quietValue, int loudValue)
    {
        recorder.values.clear();
        widget.onValueChange = recorder.callback();
        widget.setValue (quietValue, juce::dontSendNotification);
        CHECK (widget.getValue() == quietValue);
        CHECK (recorder.values.empty());
        widget.setValue (loudValue, juce::sendNotificationSync);
        CHECK (widget.getValue() == loudValue);
        CHECK (recorder.values == std::vector<int>{loudValue});
    };

    SECTION ("knob")
    {
        qtn::NuxKnob w;
        check (w, 10, 20);
    }
    SECTION ("fader")
    {
        qtn::NuxFader w (*assets);
        check (w, 10, 20);
    }
    SECTION ("volume")
    {
        qtn::NuxVolume w (*assets);
        check (w, 10, 20);
    }
    SECTION ("toggle")
    {
        qtn::NuxToggle w;
        check (w, 1, 0);
    }
    SECTION ("plist")
    {
        qtn::NuxPlistButton w;
        w.setImages (solidImage (4, 4), solidImage (4, 4), solidImage (4, 4));
        check (w, 1, 2);
    }
    SECTION ("image button")
    {
        qtn::NuxImageButton w;
        check (w, 1, 0);
    }
}

// ---- NuxToggle --------------------------------------------------------------------------------

TEST_CASE ("toggle_emits_once_per_click")
{
    juce::ScopedJuceInitialiser_GUI gui;
    qtn::NuxToggle toggle;
    toggle.setSize (49, 113);
    Recorder recorder;
    toggle.onValueChange = recorder.callback();

    toggle.mouseDown (mouseAt (toggle, 10, 10));
    toggle.mouseUp (mouseAt (toggle, 10, 10));
    CHECK (recorder.values == std::vector<int>{1});

    toggle.mouseDown (mouseAt (toggle, 10, 10));
    toggle.mouseUp (mouseAt (toggle, 10, 10));
    CHECK (recorder.values == std::vector<int>{1, 0});

    // Released outside: no click, like juce::Button.
    toggle.mouseDown (mouseAt (toggle, 10, 10));
    toggle.mouseUp (mouseEvent (toggle, {200, 10}, {10, 10}));
    CHECK (recorder.values.size() == 2);

    // Disabled: no click.
    toggle.setEnabled (false);
    toggle.mouseDown (mouseAt (toggle, 10, 10));
    toggle.mouseUp (mouseAt (toggle, 10, 10));
    CHECK (recorder.values.size() == 2);
}

TEST_CASE ("toggle_image_for_state")
{
    // Image slot = toggle state (off image +0x1b8, on image +0x1c0); 0.5 opacity when disabled
    // (decomp:NuxToggle/paintButton@10008f160.c; decomp:NuxToggle/setImages@10008f660.c).
    CHECK (qtn::NuxToggle::imageSlotFor (false) == 0);
    CHECK (qtn::NuxToggle::imageSlotFor (true) == 1);
    CHECK (qtn::NuxToggle::opacityFor (true) == 1.0f);
    CHECK (qtn::NuxToggle::opacityFor (false) == 0.5f);
}

// ---- NuxPlistButton ---------------------------------------------------------------------------

TEST_CASE ("plist_click_cycles_images")
{
    // mouseUp: index + 1, wrapping to 0 at the image count; always notifies
    // (decomp:NuxPlistButton/mouseUp@10008b8d0.c). setIndex ignores out-of-range (setIndex@10008bca0.c).
    qtn::PlistModel plist;
    CHECK_FALSE (plist.click().has_value()); // no images: nothing happens
    plist.setImageCount (3);
    CHECK (plist.click() == 1);
    CHECK (plist.click() == 2);
    CHECK (plist.click() == 0);
    CHECK_FALSE (plist.setIndex (3));
    CHECK (plist.index() == 0);
    CHECK (plist.setIndex (2));
    CHECK (plist.index() == 2);

    juce::ScopedJuceInitialiser_GUI gui;
    qtn::NuxPlistButton button;
    CHECK (button.getWidth() == 50); // decomp:NuxPlistButton/NuxPlistButton@10008bd80.c (0x32)
    button.setImages (solidImage (4, 4), solidImage (4, 4), solidImage (4, 4));
    Recorder recorder;
    button.onValueChange = recorder.callback();
    button.mouseUp (mouseAt (button, 5, 5));
    button.mouseUp (mouseAt (button, 5, 5));
    button.mouseUp (mouseAt (button, 5, 5));
    CHECK (recorder.values == std::vector<int>{1, 2, 0});
}

// ---- NuxFader ---------------------------------------------------------------------------------

TEST_CASE ("fader_drag_maps_like_original")
{
    // Range 0..99, top = max; thumb y = (max - v - min)/(max - min) * (h - thumbH); drag only when
    // mouse-down hit the thumb; thumb y = clamp(y - thumbH/2) (decomp:NuxFaderComponent/
    // NuxFaderComponent@100088110.c; mouseDown@1000877f0.c; mouseDrag@100087840.c; setValue@100087a40.c).
    qtn::FaderModel fader;
    fader.setGeometry (120, 35, 16); // travel 104
    fader.setValue (99);
    CHECK (fader.thumbY() == 0.0f);
    fader.setValue (0);
    CHECK (fader.thumbY() == 104.0f);

    fader.setValue (99);
    fader.mouseDown ({10, 40}); // below the thumb: not armed
    CHECK_FALSE (fader.mouseDrag (60));
    CHECK (fader.value() == 99);

    fader.mouseDown ({10, 5});
    CHECK (fader.mouseDrag (60)); // thumb y 52 -> p 0.5 -> 99 - 49.5 -> (int) 49
    CHECK (fader.value() == 49);
    fader.mouseDrag (500);
    CHECK (fader.value() == 0);

    // Wheel over the thumb column: +-1, clamped (mouseWheelMove@100087990.c).
    CHECK (fader.mouseWheel ({5, 100}, 0.2f));
    CHECK (fader.value() == 1);
    CHECK_FALSE (fader.mouseWheel ({50, 100}, 0.2f)); // right of the thumb column
}

TEST_CASE ("fader_thumb_image_for_enabled_state")
{
    // setEnabled picks +0x190 (enabled) or +0x198 (disabled) (decomp:NuxFaderComponent/setEnabled@100087fb0.c).
    const auto normal = solidImage (56, 64);
    const auto disabled = solidImage (20, 20);
    CHECK (qtn::NuxFader::thumbImageFor (true, normal, disabled) == normal);
    CHECK (qtn::NuxFader::thumbImageFor (false, normal, disabled) == disabled);
    CHECK (qtn::NuxFader::thumbImageFor (false, normal, juce::Image()) == normal);
}

// ---- NuxVolume --------------------------------------------------------------------------------

TEST_CASE ("volume_drag_maps_like_original")
{
    // 84x33, thumb 15x16 at y 8, x range 0..69; value = (int)(99 * x / 69); double-click sets
    // 50 (convertFrom0to1(0.5) on 0..100) (SFW:L443-592; decomp:NuxVolumeComponent/*).
    qtn::VolumeModel volume;
    volume.setValue (99);
    CHECK (volume.thumbX() == 69.0f);

    volume.mouseDown ({30, 12}); // not on the thumb
    CHECK_FALSE (volume.mouseDrag (42));

    volume.mouseDown ({75, 12});
    CHECK (volume.mouseDrag (42)); // x 35 -> 50.2 -> 50
    CHECK (volume.value() == 50);
    CHECK (volume.mouseDrag (-40));
    CHECK (volume.value() == 0);

    // Double-click: thumb x = 50/99*69 in float, read back as (int)(99 * x/69) = (int)49.999996.
    CHECK (volume.doubleClick());
    CHECK (volume.value() == 49);

    // Tip: value over 0..100 mapped to -12..+12 dB, "%.2fdB" (updateCurrentVolume@10007c7d0.c).
    CHECK (volume.tipText() == "-0.24dB");
    volume.setValue (50);
    CHECK (volume.tipText() == "0.00dB");
    volume.setValue (99);
    CHECK (volume.tipText() == "11.76dB");

    // Level bar: Rectangle (3, 13, thumbX, 5), corner 4.5 (asm@0x10007c5ee, consts @0x1006d6030/40, @0x1006d7fc8).
    CHECK (volume.barArea() == juce::Rectangle<float> (3.0f, 13.0f, 69.0f, 5.0f));
}

TEST_CASE ("volume_thumb_image_follows_mouse")
{
    // mouseMove/mouseUp: over the thumb -> +0xd8 (data_632) else +0xd0 (data_633); mouseDown on
    // the thumb -> +0xe0 (data_631) (decomp:NuxVolumeComponent/mouseMove@10007c210.c, mouseDown@10007c2e0.c).
    qtn::VolumeModel volume;
    volume.setValue (0);
    CHECK (volume.thumbImage() == qtn::VolumeModel::ThumbImage::normal);
    volume.mouseMove ({5, 10});
    CHECK (volume.thumbImage() == qtn::VolumeModel::ThumbImage::over);
    volume.mouseDown ({5, 10});
    CHECK (volume.thumbImage() == qtn::VolumeModel::ThumbImage::down);
    volume.mouseUp ({50, 10});
    CHECK (volume.thumbImage() == qtn::VolumeModel::ThumbImage::normal);
}

// ---- NuxImageButton ---------------------------------------------------------------------------

TEST_CASE ("image_button_image_for_state")
{
    // juce::ImageButton::getCurrentImage: down or toggled -> down (else over, else normal);
    // over -> over (else normal) (NuxImageButton derives from juce::ImageButton, MW.lp:L395-430).
    using qtn::ButtonImage;
    CHECK (qtn::selectButtonImage (false, false, false, true, true) == ButtonImage::normal);
    CHECK (qtn::selectButtonImage (false, false, true, true, true) == ButtonImage::over);
    CHECK (qtn::selectButtonImage (true, false, true, true, true) == ButtonImage::down);
    CHECK (qtn::selectButtonImage (false, true, false, true, true) == ButtonImage::down);
    CHECK (qtn::selectButtonImage (true, false, false, true, false) == ButtonImage::over);
    CHECK (qtn::selectButtonImage (true, false, false, false, false) == ButtonImage::normal);
    CHECK (qtn::selectButtonImage (false, false, true, false, true) == ButtonImage::normal);
}

TEST_CASE ("image_button_emits_once_per_click")
{
    juce::ScopedJuceInitialiser_GUI gui;
    qtn::NuxImageButton button;
    button.setSize (80, 22);
    button.setImages (solidImage (160, 44), solidImage (160, 44), solidImage (160, 44));
    button.setClickingTogglesState (true);
    Recorder recorder;
    button.onValueChange = recorder.callback();
    int clicks = 0;
    button.onClick = [&clicks]
    { ++clicks; };

    button.mouseDown (mouseAt (button, 5, 5));
    CHECK (button.currentImage() == qtn::ButtonImage::down);
    button.mouseUp (mouseAt (button, 5, 5));
    CHECK (clicks == 1);
    CHECK (recorder.values == std::vector<int>{1});

    button.mouseDown (mouseAt (button, 5, 5));
    button.mouseUp (mouseEvent (button, {300, 5}, {5, 5}));
    CHECK (clicks == 1);
}

// ---- real extracted assets (QTN_ASSETS_DIR), skipped when not configured -----------------------

namespace
{
bool isPlaceholder (const juce::Image& image)
{
    return image.getWidth() == 16 && image.getHeight() == 16 && image.getPixelAt (0, 0) == juce::Colours::magenta;
}
} // namespace

TEST_CASE ("widget_images_resolve_from_real_assets")
{
    if (juce::String (QTN_ASSETS_DIR).isEmpty() || ! juce::File (QTN_ASSET_MANIFEST).existsAsFile())
        SKIP ("QTN_ASSETS_DIR not configured");

    auto parsed = qtn::AssetManifest::parse (juce::File (QTN_ASSET_MANIFEST).loadFileAsString());
    REQUIRE (parsed.manifest.has_value());
    qtn::AssetLibrary assets (std::move (*parsed.manifest), juce::File (QTN_ASSETS_DIR).getChildFile ("named"));

    // Fader thumb, volume track/thumbs (fixed in the original code).
    std::vector<int> indices{qtn::NuxFader::defaultThumbImageIndex, qtn::NuxVolume::trackImageIndex,
                             qtn::NuxVolume::thumbNormalImageIndex, qtn::NuxVolume::thumbOverImageIndex,
                             qtn::NuxVolume::thumbDownImageIndex};
    // R5: WAH toggle data_392/393; side-panel USER/FACTORY image buttons data_1191-1194.
    for (int i : {392, 393, 1191, 1192, 1193, 1194})
        indices.push_back (i);
    for (int index : indices)
    {
        INFO ("data_" << index);
        CHECK_FALSE (isPlaceholder (assets.image (index)));
    }

    // Config-driven images: knob filmstrip (12 x 10 square frames) and plist frames.
    const auto strip = assets.image ("Knob_1.png");
    REQUIRE_FALSE (isPlaceholder (strip));
    const int frame = strip.getWidth() / qtn::KnobModel::framesPerRow;
    CHECK (strip.getWidth() % qtn::KnobModel::framesPerRow == 0);
    CHECK (strip.getHeight() == frame * (qtn::KnobModel::frameCount / qtn::KnobModel::framesPerRow));
    for (auto name : {"_Plist_Button_Frame_1.png", "_Plist_Button_Frame_2.png", "_Plist_Button_Frame_3.png"})
    {
        INFO (name);
        CHECK_FALSE (isPlaceholder (assets.image (juce::String (name))));
    }
}
