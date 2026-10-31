#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace qtn
{
/** Settings "Knob Control": Straight = 0, Round = 1, stored in UserData[0x41] and copied into
    NuxKnob+0x260 (decomp:NuxInstrumentPanel/showElement@100245f50.c:L429; R5 NuxSettingPanel). */
enum class KnobDragMode
{
    straight = 0,
    round = 1
};

/** Value, drag and filmstrip logic of the original NuxKnob, without any JUCE component.

    Ranges are integer device units with a step of 1 (NuxKnob+0x1b0 = 1.0). */
class KnobModel
{
  public:
    static constexpr int framesPerRow = 12;               // NuxKnob+0x298 (decomp:NuxKnob/NuxKnob@10008aa40.c:L45)
    static constexpr int frameCount = 120;                // 360 deg / 3; frame 120 wraps to 0 (paint@10008a0b0.c:L83)
    static constexpr double straightDragPerPixel = 0.005; // decomp:NuxKnob/mouseDrag@1000897b0.c:L302
    static constexpr float deadZoneSquared = 25.0f;       // round mode, mouseDrag@1000897b0.c:L63

    void setRange (int minimum, int maximum) noexcept;
    [[nodiscard]] int minimum() const noexcept { return min; }
    [[nodiscard]] int maximum() const noexcept { return max; }

    /** Sweep in degrees, each clamped to 0..360 (showElement@100245f50.c:L431-L437). */
    void setAngles (float startDegrees, float endDegrees) noexcept;
    void setDragMode (KnobDragMode mode) noexcept { dragMode = mode; }
    void setDefaultValue (int value) noexcept { defaultValue = value; }

    [[nodiscard]] int value() const noexcept { return current; }

    /** Snaps to whole units (floor(v + 0.5)) and clamps. Returns true if the value changed. */
    bool setValue (double newValue) noexcept;

    [[nodiscard]] double proportion() const noexcept;
    [[nodiscard]] int frameIndex() const noexcept;
    [[nodiscard]] static juce::Rectangle<int> frameSourceArea (int frame, int stripWidth) noexcept;

    void mouseDown (juce::Point<float> position) noexcept;
    /** Returns true if the value changed. */
    bool mouseDrag (juce::Point<float> position, juce::Point<int> componentSize, bool draggedSinceMouseDown) noexcept;
    bool mouseWheel (float deltaY) noexcept;
    bool doubleClick() noexcept;

  private:
    [[nodiscard]] double startRadians() const noexcept;
    [[nodiscard]] double endRadians() const noexcept;
    [[nodiscard]] double valueFromProportion (double proportion) const noexcept;
    [[nodiscard]] double roundDragAngle (juce::Point<float> position, juce::Point<int> size, bool dragged) const noexcept;

    int min = 0;
    int max = 100; // decomp:NuxKnob/NuxKnob@10008aa40.c:L34
    int current = 0;
    int defaultValue = 0; // +0xd8, 0 in the ctor
    float startDegrees = 0.0f;
    float endDegrees = 360.0f;                   // +0x268/+0x26c (NuxKnob@10008aa40.c:L49)
    KnobDragMode dragMode = KnobDragMode::round; // +0x260 = 1 in the ctor (L41)

    juce::Point<float> downPosition;
    double proportionAtDown = 0.0;
    double lastAngle = 0.0;
    bool skipNextDrag = false;
};

/** Filmstrip knob (NuxKnob, draw type 2 as created by showElement). */
class NuxKnob : public juce::Component
{
  public:
    NuxKnob();

    std::function<void (int)> onValueChange;

    /** Any notification other than dontSendNotification calls onValueChange synchronously, and
        only when the value changed. */
    void setValue (int newValue, juce::NotificationType notification = juce::sendNotificationSync);
    [[nodiscard]] int getValue() const noexcept { return model.value(); }

    void setRange (int minimum, int maximum);
    void setAngles (float startDegrees, float endDegrees);
    void setDragMode (KnobDragMode mode) noexcept { model.setDragMode (mode); }
    void setDefaultValue (int value) noexcept { model.setDefaultValue (value); }

    /** A 12-column grid of square frames (e.g. Knob_1.png, 3000x2500). Sliced once here. */
    void setFilmstrip (const juce::Image& strip);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  private:
    void changed (bool valueChanged, juce::NotificationType notification);

    KnobModel model;
    std::vector<juce::Image> frames;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NuxKnob)
};
} // namespace qtn
