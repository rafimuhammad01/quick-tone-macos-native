#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace qtn
{
enum class ButtonImage
{
    normal,
    over,
    down
};

/** juce::ImageButton::getCurrentImage with its fallbacks: down (or toggled) -> down image, else
    over, else normal; over -> over image, else normal. */
[[nodiscard]] ButtonImage selectButtonImage (bool isDown, bool toggled, bool isOver, bool hasOverImage,
                                             bool hasDownImage) noexcept;

/** NuxImageButton: the original subclasses juce::ImageButton and adds a tips string shown on hover
    (decomp:NuxImageButton/mouseEnter@100089350.c, setTips@100089610.c). Used on the NME-300 path
    for the side panel's USER / FACTORY buttons (R5 NuxSidePanel). */
class NuxImageButton : public juce::Component
{
  public:
    NuxImageButton();

    std::function<void()> onClick;
    std::function<void (int)> onValueChange; // toggle state, 0 / 1

    void setValue (int newValue, juce::NotificationType notification = juce::sendNotificationSync);
    [[nodiscard]] int getValue() const noexcept { return toggled ? 1 : 0; }
    void setClickingTogglesState (bool shouldToggle) noexcept { clickTogglesState = shouldToggle; }

    void setImages (const juce::Image& normal, const juce::Image& over, const juce::Image& down);
    void setTips (juce::String text) { tips = std::move (text); }
    [[nodiscard]] const juce::String& getTips() const noexcept { return tips; }

    [[nodiscard]] ButtonImage currentImage() const noexcept;

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

  private:
    juce::Image normalImage;
    juce::Image overImage;
    juce::Image downImage;
    juce::String tips;
    bool toggled = false;
    bool clickTogglesState = false;
    bool over = false;
    bool down = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NuxImageButton)
};
} // namespace qtn
