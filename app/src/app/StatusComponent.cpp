#include "app/StatusComponent.h"

namespace qtn
{
namespace
{
constexpr int margin = 16;
constexpr int rowHeight = 26;
constexpr int buttonWidth = 260;
} // namespace

StatusComponent::StatusComponent (AppController& c) : controller (c)
{
    modeLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    modeLabel.setColour (juce::Label::textColourId, juce::Colours::orange);

    for (auto* label : {&modeLabel, &connectionLabel, &deviceLabel, &progressLabel, &resultLabel, &compareLabel})
        addAndMakeVisible (*label);

    names.setMultiLine (true);
    names.setReadOnly (true);
    names.setScrollbarsShown (true);
    names.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    addAndMakeVisible (names);

    revealButton.onClick = [this]
    { revealBackup(); };
    compareButton.onClick = [this]
    { chooseExport(); };
    addAndMakeVisible (revealButton);
    addAndMakeVisible (compareButton);

    controller.onStatusChanged = [this]
    { refresh(); };
    refresh();
}

StatusComponent::~StatusComponent() { controller.onStatusChanged = nullptr; }

void StatusComponent::refresh()
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto status = controller.status();

    modeLabel.setText (status.mode, juce::dontSendNotification);
    connectionLabel.setText ("Pedal: " + status.connection, juce::dontSendNotification);
    deviceLabel.setText (status.device, juce::dontSendNotification);
    progressLabel.setText (status.progress.isEmpty() ? juce::String() : "Read " + status.progress, juce::dontSendNotification);
    resultLabel.setText (status.result, juce::dontSendNotification);
    compareLabel.setText (status.compare, juce::dontSendNotification);
    names.setText (juce::StringArray (status.presetNames.data(), static_cast<int> (status.presetNames.size())).joinIntoString ("\n"), false);

    const bool hasBackup = controller.latestBackup().has_value();
    revealButton.setEnabled (hasBackup);
    compareButton.setEnabled (hasBackup);
}

void StatusComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void StatusComponent::resized()
{
    auto area = getLocalBounds().reduced (margin);

    modeLabel.setBounds (area.removeFromTop (rowHeight + 6));
    for (auto* label : {&connectionLabel, &deviceLabel, &progressLabel, &resultLabel})
        label->setBounds (area.removeFromTop (rowHeight));

    auto buttons = area.removeFromTop (rowHeight + 8).reduced (0, 4);
    revealButton.setBounds (buttons.removeFromLeft (buttonWidth));
    buttons.removeFromLeft (margin);
    compareButton.setBounds (buttons.removeFromLeft (buttonWidth));

    compareLabel.setBounds (area.removeFromTop (rowHeight));
    area.removeFromTop (margin / 2);
    names.setBounds (area);
}

void StatusComponent::revealBackup()
{
    if (const auto folder = controller.latestBackup())
        folder->revealToUser();
}

void StatusComponent::chooseExport()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a QuickTone all-presets export",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   "*.nuxpreset");

    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (flags,
                          [safe = juce::Component::SafePointer<StatusComponent> (this)] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (safe == nullptr || file == juce::File())
                                  return;
                              safe->controller.compareWith (file);
                          });
}
} // namespace qtn
