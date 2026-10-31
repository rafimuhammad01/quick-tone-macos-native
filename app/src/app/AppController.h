#pragma once

#include "backup/BackupStore.h"
#include "device/Allowlist.h"
#include "device/Connector.h"
#include "device/DeviceInfo.h"
#include "device/DeviceSession.h"
#include "device/Transport.h"
#include "model/DeviceState.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>
#include <vector>

namespace qtn
{
/** What the status window shows. Computed by AppController; the window only copies it. */
struct StatusModel
{
    juce::String mode;                      // always "READ-ONLY" in this build
    juce::String connection;                // "Connected" / "Not connected"
    juce::String device;                    // "Firmware <fw>, name <name>" once the version reply came
    juce::String progress;                  // "<n>/75" while connected, else empty
    juce::String result;                    // "Backup saved: <folder>", a failure text, or empty
    std::vector<juce::String> presetNames;  // 72 lines "NN  name" after a successful read, else empty
    std::optional<juce::File> backupFolder; // this connection's backup
    juce::String compare;                   // result of the last compareWith()
};

/** The app's read-only connect flow, without JUCE UI or timers: owns the DeviceSession (policy
    fixed to ReadOnly), BackupStore, DeviceState and Connector, and wires the transport's
    callbacks to the Connector. The app drives tick() from a juce::Timer.

    Single-threaded: call everything on the message thread (Transport invokes its callbacks
    there). The transport must outlive the controller. */
class AppController
{
  public:
    using Clock = std::function<juce::Time()>;

    AppController (Transport& transport, Allowlist allowlist, juce::File backupRoot, Clock clock = &juce::Time::getCurrentTime);
    ~AppController();

    /** Starts the connect sequence if the pedal is already connected. Call once, after setting
        onStatusChanged. */
    void begin();

    /** Checks the Connector's reply timeout. */
    void tick();

    [[nodiscard]] StatusModel status() const;

    /** The newest complete backup on disk (from this or an earlier run). */
    [[nodiscard]] std::optional<juce::File> latestBackup() const;

    /** Compares latestBackup() with a QuickTone export; the text lands in status().compare. */
    void compareWith (const juce::File& nuxpresetFile);

    /** The gate every outgoing command passes. Its policy is ReadOnly for the whole run. */
    [[nodiscard]] DeviceSession& session() noexcept;

    std::function<void()> onStatusChanged;

  private:
    void handleConnectionChanged (bool connected);
    void changed() const;

    Transport& transport;
    DeviceSession deviceSession;
    BackupStore store;
    DeviceState state;
    Clock clock;
    Connector connector;

    juce::String device;
    juce::String progress;
    juce::String result;
    juce::String compare;
    std::optional<juce::File> backupFolder;

    JUCE_DECLARE_NON_COPYABLE (AppController)
};
} // namespace qtn
