#include "BinaryData.h"
#include "Version.h"
#include "app/AppController.h"
#include "app/StatusComponent.h"
#include "device/Allowlist.h"
#include "device/JuceMidiTransport.h"
#include "device/PortConnection.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace
{
constexpr int windowWidth = 720;
constexpr int windowHeight = 800;
constexpr int tickIntervalMs = 50;

/** Adapter: drives AppController::tick() (the Connector's reply timeout) from the message loop. */
class TickTimer final : public juce::Timer
{
  public:
    explicit TickTimer (qtn::AppController& c) : controller (c) {}
    void timerCallback() override { controller.tick(); }

  private:
    qtn::AppController& controller;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TickTimer)
};

class MainWindow final : public juce::DocumentWindow
{
  public:
    MainWindow (const juce::String& name, qtn::AppController& controller)
        : DocumentWindow (name,
                          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                          DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new qtn::StatusComponent (controller), false);
        setResizable (true, true);
        centreWithSize (windowWidth, windowHeight);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

juce::File backupRoot()
{
    // ~/Library/Application Support/QuickTone Native/backups
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support")
        .getChildFile ("QuickTone Native")
        .getChildFile ("backups");
}

qtn::Allowlist embeddedAllowlist()
{
    return qtn::Allowlist::fromJson (juce::String::createStringFromData (BinaryData::allowlist_json, BinaryData::allowlist_jsonSize));
}

class QuickToneNativeApplication final : public juce::JUCEApplication
{
  public:
    QuickToneNativeApplication() = default;

    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return qtn::versionString(); }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        transport = std::make_unique<qtn::JuceMidiTransport> ([] (const juce::String& portName)
                                                              { return qtn::isPedalPortName (portName.toStdString()); });
        controller = std::make_unique<qtn::AppController> (*transport, embeddedAllowlist(), backupRoot());
        mainWindow = std::make_unique<MainWindow> (getApplicationName() + " (read-only)", *controller);
        timer = std::make_unique<TickTimer> (*controller);

        controller->begin();
        timer->startTimer (tickIntervalMs);
    }

    void shutdown() override
    {
        // Reverse order: nothing may call into an object that is already gone.
        timer = nullptr;
        mainWindow = nullptr;
        controller = nullptr;
        transport = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

  private:
    std::unique_ptr<qtn::JuceMidiTransport> transport;
    std::unique_ptr<qtn::AppController> controller;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<TickTimer> timer;
};
} // namespace

START_JUCE_APPLICATION (QuickToneNativeApplication)
