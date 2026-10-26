#pragma once

#include "device/Allowlist.h"
#include "device/Transport.h"
#include "proto/Command.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

namespace qtn
{
/** How much the session lets through to the pedal. */
enum class WritePolicy
{
    ReadOnly,        // Handshake, Read
    LiveEditsOnly,   // + LiveEdit
    ScratchSlotOnly, // + SavePreset / RenamePreset to the scratch slot
    Full             // everything
};

enum class SendResult
{
    Sent,
    DryRun,
    BlockedByPolicy,
    NotAllowlisted,
    Disconnected,
    BackupMissing
};

/** The safety gate: the only code that calls Transport::send. Every submitted command is
    checked (in this order) for a connection, the allowlist, a finished backup, the write
    policy and dry-run mode, and is logged with its result. A failed send is never retried. */
class DeviceSession
{
  public:
    using Clock = std::function<juce::Time()>;

    /** `clock` stamps log entries; tests pass a fake one. */
    DeviceSession (Transport& transport, Allowlist allowlist, Clock clock = &juce::Time::getCurrentTime);

    /** `scratchSlot` is only used by WritePolicy::ScratchSlotOnly. */
    void setPolicy (WritePolicy newPolicy, int scratchSlot = -1);
    WritePolicy policy() const noexcept;

    void setDryRun (bool enabled) noexcept;

    /** Set by the auto-backup (D2). Until it is true only Handshake and Read pass. */
    void markBackupDone (bool done) noexcept;

    [[nodiscard]] SendResult submit (const Command& cmd);

    struct LogEntry
    {
        juce::Time at;
        Command cmd;
        SendResult result;
    };

    const std::vector<LogEntry>& log() const noexcept;

  private:
    SendResult check (const Command& cmd) const;

    Transport& transport;
    Allowlist allowlist;
    Clock clock;
    WritePolicy currentPolicy{WritePolicy::ReadOnly};
    int scratchSlot = -1;
    bool dryRun = false;
    bool backupDone = false;
    std::vector<LogEntry> entries;

    JUCE_DECLARE_NON_COPYABLE (DeviceSession)
};
} // namespace qtn
