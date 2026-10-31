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
    BackupMissing,
    Malformed // bytes are not one sysex or one channel message, or a short message is labelled Read/Handshake
};

/** The safety gate: the only code that calls Transport::send. Every submitted command is
    checked (in this order) for well-formed bytes (one sysex, or one short channel message whose
    kind is not Read/Handshake), a connection, the allowlist, a finished backup, the write
    policy and dry-run mode, and is logged with its result. Short messages (CCs) go through the
    same checks as sysex, by their CommandKind. A failed send is never retried. */
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
