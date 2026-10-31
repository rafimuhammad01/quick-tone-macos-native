#pragma once

#include "app/AppController.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace qtn
{
/** The M1 status window: shows AppController::status() and offers "Reveal backup folder" and
    "Compare with QuickTone export...". Thin adapter: every text comes from the StatusModel.
    Message thread only. Has no way to build or submit a command. */
class StatusComponent final : public juce::Component
{
  public:
    explicit StatusComponent (AppController& controller);
    ~StatusComponent() override;

    /** Copies the controller's current status into the widgets. */
    void refresh();

    void paint (juce::Graphics& g) override;
    void resized() override;

  private:
    void revealBackup();
    void chooseExport();

    AppController& controller;

    juce::Label modeLabel;
    juce::Label connectionLabel;
    juce::Label deviceLabel;
    juce::Label progressLabel;
    juce::Label resultLabel;
    juce::Label compareLabel;
    juce::TextButton revealButton{"Reveal backup folder"};
    juce::TextButton compareButton{"Compare with QuickTone export..."};
    juce::TextEditor names;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusComponent)
};
} // namespace qtn
