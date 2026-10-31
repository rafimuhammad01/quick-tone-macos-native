#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace qtn
{
/** NuxToggle (draw type 2): an off and an on image stretched over the component; a click toggles
    the state (decomp:NuxToggle/paintButton@10008f160.c; showElement@100245f50.c:L1205-L1223). */
class NuxToggle : public juce::Component
{
  public:
    NuxToggle();

    std::function<void (int)> onValueChange; // 0 = off, 1 = on

    void setValue (int newValue, juce::NotificationType notification = juce::sendNotificationSync);
    [[nodiscard]] int getValue() const noexcept { return on ? 1 : 0; }

    void setImages (const juce::Image& offImage, const juce::Image& onImage);

    /** 0 = off image (+0x1b8), 1 = on image (+0x1c0). */
    [[nodiscard]] static int imageSlotFor (bool isOn) noexcept { return isOn ? 1 : 0; }
    /** 0.5 when disabled, else 1 (paintButton@10008f160.c). */
    [[nodiscard]] static float opacityFor (bool enabled) noexcept { return enabled ? 1.0f : 0.5f; }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void enablementChanged() override { repaint(); }

  private:
    juce::Image images[2];
    bool on = false;
    bool pressed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NuxToggle)
};
} // namespace qtn
