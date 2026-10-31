#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace qtn
{
class AssetLibrary;

/** Vertical fader logic of the original NuxFaderComponent: the top is the maximum. */
class FaderModel
{
  public:
    void setRange (int minimum, int maximum) noexcept;
    void setGeometry (int height, int thumbWidth, int thumbHeight) noexcept;

    [[nodiscard]] int value() const noexcept { return static_cast<int> (current); }
    [[nodiscard]] float thumbY() const noexcept { return thumbTop; }

    /** Clamps to the range. Returns true if the integer value changed. */
    bool setValue (float newValue) noexcept;

    void mouseDown (juce::Point<int> position) noexcept;
    bool mouseDrag (int y) noexcept;
    bool mouseWheel (juce::Point<int> position, float deltaY) noexcept;

  private:
    [[nodiscard]] float travel() const noexcept { return static_cast<float> (height - thumbHeight); }
    void updateThumb() noexcept;

    float min = 0.0f;
    float max = 99.0f; // NormalisableRange<float> at +0xd0 (NuxFaderComponent@100088110.c)
    float current = 0.0f;
    int height = 120;
    int thumbWidth = 35;
    int thumbHeight = 16; // ctor: thumb 35x16, component 35x120
    float thumbTop = 0.0f;
    bool dragging = false;
};

/** NuxFaderComponent: a thumb image moving along a vertical track. */
class NuxFader : public juce::Component
{
  public:
    static constexpr int defaultThumbImageIndex = 509; // _Fader_1_Normal (NuxFaderComponent@100088110.c)

    explicit NuxFader (AssetLibrary& assets);

    std::function<void (int)> onValueChange;

    void setValue (int newValue, juce::NotificationType notification = juce::sendNotificationSync);
    [[nodiscard]] int getValue() const noexcept { return model.value(); }
    void setRange (int minimum, int maximum);

    /** Size w x h; the thumb is drawn at half the image size (images are @2x)
        (decomp:NuxFaderComponent/resetDisplay@100087d70.c). */
    void resetDisplay (int width, int height, const juce::Image& thumb, const juce::Image& disabledThumb);

    [[nodiscard]] static const juce::Image& thumbImageFor (bool enabled, const juce::Image& normal,
                                                           const juce::Image& disabled) noexcept;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void enablementChanged() override { repaint(); }

  private:
    void changed (bool valueChanged, juce::NotificationType notification);

    FaderModel model;
    juce::Image thumbImage;
    juce::Image disabledThumbImage;
    int thumbWidth = 35;
    int thumbHeight = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NuxFader)
};
} // namespace qtn
