#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <optional>

namespace qtn
{
/** Index logic of NuxPlistButton: each click shows the next image, wrapping to the first. */
class PlistModel
{
  public:
    void setImageCount (int count) noexcept;
    [[nodiscard]] int index() const noexcept { return current; }

    /** Ignored (returns false) when index is not below the image count (setIndex@10008bca0.c). */
    bool setIndex (int newIndex) noexcept;

    /** The new index, or nothing when there are no images (mouseUp@10008b8d0.c). */
    std::optional<int> click() noexcept;

  private:
    int count = 0;
    int current = 0;
};

/** NuxPlistButton: a 50x50 button cycling through three images on each click. */
class NuxPlistButton : public juce::Component
{
  public:
    NuxPlistButton();

    std::function<void (int)> onValueChange; // the new image index

    void setValue (int newValue, juce::NotificationType notification = juce::sendNotificationSync);
    [[nodiscard]] int getValue() const noexcept { return model.index(); }

    /** setPlistImage(Image, Image, Image) of the original. */
    void setImages (const juce::Image& first, const juce::Image& second, const juce::Image& third);

    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override;

  private:
    PlistModel model;
    std::array<juce::Image, 3> images;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NuxPlistButton)
};
} // namespace qtn
