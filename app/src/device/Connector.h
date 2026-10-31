#pragma once

#include "Bytes.h"
#include "backup/BackupStore.h"
#include "device/DeviceInfo.h"
#include "device/DeviceSession.h"
#include "model/DeviceState.h"
#include "proto/Codec.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <optional>
#include <vector>

namespace qtn
{
/** Runs the NME-300 connect sequence (R3b §4) and the auto-backup:
    version -> `19` global parameters -> `07 i` for i = 0..71 -> `13` edit buffer.

    - One request at a time, each through DeviceSession::submit. A request the session does not
      send fails the sequence at once.
    - A reply is matched by total length + raw[7] (proto::classify) and, for `07`, by raw[8] ==
      the requested slot. Anything else is ignored.
    - A request without a reply within `timeoutMs` fails the sequence once. Nothing is ever resent
      (the original resends forever, R3b §7).
    - On success the presets, the global-parameters reply and the edit buffer go into DeviceState
      and a BackupStore backup; then DeviceSession::markBackupDone(result.ok). Nothing is saved for
      an incomplete sequence.

    Single-threaded logic, no timers: the owner forwards Transport::onReceive to handleMessage,
    Transport::onConnectionChanged to handleConnectionChanged, and calls tick() periodically
    (from a juce::Timer in the app; that adapter is not written yet). */
class Connector
{
  public:
    using Clock = std::function<juce::Time()>;

    static constexpr int timeoutMs = 1000;                       // R3b §4
    static constexpr int totalRequests = 3 + proto::presetCount; // 75
    static constexpr int globalParamsBackupSlot = 100;           // backup "slot" of the 17-byte reply
    static constexpr int editBufferBackupSlot = 101;             // backup "slot" of the edit buffer

    Connector (DeviceSession& session, BackupStore& store, DeviceState& state, Clock clock);

    /** Starts (or restarts) the sequence: clears backup-done and the device state first. */
    void start();

    /** Feed every incoming message here. Messages that are not the awaited reply are ignored. */
    void handleMessage (const Bytes& raw);

    /** connected: start(); disconnected: abort a running sequence (onFailed once) and clear
        backup-done. */
    void handleConnectionChanged (bool connected);

    /** Checks the reply timeout against the clock. */
    void tick();

    [[nodiscard]] bool isRunning() const noexcept;

    std::function<void (const DeviceInfo&)> onConnected;
    std::function<void (const juce::String&)> onFailed;
    std::function<void (int done, int total)> onProgress;

  private:
    enum class Step
    {
        idle,
        version,
        globals,
        presets,
        editBuffer
    };

    void send (const Command& cmd);
    void fail (const juce::String& why);
    void replied();
    void finish (const Bytes& editData, int currentSlot);
    [[nodiscard]] bool accept (const Bytes& raw);

    DeviceSession& session;
    BackupStore& store;
    DeviceState& state;
    Clock clock;

    Step step{Step::idle};
    juce::String pendingName;
    juce::int64 sentAtMs = 0;
    int done = 0;
    int slot = 0;
    std::optional<DeviceInfo> info;
    Bytes globals;
    std::vector<Bytes> presets;

    JUCE_DECLARE_NON_COPYABLE (Connector)
};
} // namespace qtn
