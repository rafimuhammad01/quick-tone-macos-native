#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace qtn
{
class AssetLibrary;

/** Logic of the title-bar NuxVolumeComponent (NME-300 path: range 0..99).

    Geometry from SFW:L443-592: component 84x33, thumb 15x16 at y 8, thumb x in 0..69. */
class VolumeModel
{
  public:
    static constexpr int width = 84;
    static constexpr int height = 33;
    static constexpr int thumbWidth = 15;
    static constexpr int thumbHeight = 16;
    static constexpr int thumbTop = 8;
    static constexpr float maximum = 99.0f; // setVolume@10007c650.c, device id 2

    enum class ThumbImage
    {
        normal, // data_633
        over,   // data_632
        down    // data_631
    };

    [[nodiscard]] int value() const noexcept { return current; }
    [[nodiscard]] float thumbX() const noexcept { return thumbLeft; }
    [[nodiscard]] ThumbImage thumbImage() const noexcept { return image; }

    /** Clamps to 0..99. Returns true if the value changed. */
    bool setValue (int newValue) noexcept;

    void mouseMove (juce::Point<int> position) noexcept;
    void mouseDown (juce::Point<int> position) noexcept;
    bool mouseDrag (int x) noexcept;
    void mouseUp (juce::Point<int> position) noexcept;
    bool doubleClick() noexcept;

    /** Tooltip: the value over 0..100 mapped to -12..+12 dB, "%.2fdB"
        (decomp:NuxVolumeComponent/updateCurrentVolume@10007c7d0.c; ranges SFW:L455-468). */
    [[nodiscard]] juce::String tipText() const;

    /** The gradient level bar (asm@0x10007c5ee: Rectangle (3, 13, thumbX, 5)). */
    [[nodiscard]] juce::Rectangle<float> barArea() const noexcept;

  private:
    [[nodiscard]] bool onThumb (juce::Point<int> position) const noexcept;
    [[nodiscard]] static constexpr float travel() noexcept { return static_cast<float> (width - thumbWidth); }
    void placeThumb (float value) noexcept;
    bool takeValueFromThumb() noexcept;

    int current = 0;
    float thumbLeft = 0.0f;
    ThumbImage image = ThumbImage::normal;
    bool dragging = false;
};

/** NuxVolumeComponent: master volume (CC 0x40 in the original) in the title bar. */
class NuxVolume : public juce::Component
{
  public:
    static constexpr int trackImageIndex = 630;       // SFW:L496
    static constexpr int thumbNormalImageIndex = 633; // +0xd0, SFW:L509
    static constexpr int thumbOverImageIndex = 632;   // +0xd8, SFW:L522
    static constexpr int thumbDownImageIndex = 631;   // +0xe0, SFW:L535
    static constexpr float barCorner = 4.5f;          // const @0x1006d7fc8
    static constexpr juce::uint32 barTopColour = 0xffc8f5b1;
    static constexpr juce::uint32 barBottomColour = 0xff52e6cb; // gradient y 8 -> 20 (paint@10007c530.c)

    explicit NuxVolume (AssetLibrary& assets);

    std::function<void (int)> onValueChange;

    void setValue (int newValue, juce::NotificationType notification = juce::sendNotificationSync);
    [[nodiscard]] int getValue() const noexcept { return model.value(); }
    [[nodiscard]] juce::String getTipText() const { return model.tipText(); }

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

  private:
    void changed (bool valueChanged, juce::NotificationType notification);
    void renderBackground();
    [[nodiscard]] const juce::Image& currentThumb() const noexcept;

    VolumeModel model;
    juce::Image track;
    juce::Image thumbNormal;
    juce::Image thumbOver;
    juce::Image thumbDown;
    juce::Image background; // track + level bar at 2x, re-rendered on value change, not in paint()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NuxVolume)
};
} // namespace qtn
