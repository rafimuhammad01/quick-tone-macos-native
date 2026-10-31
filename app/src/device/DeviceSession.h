#pragma once

#include "device/Allowlist.h"
#include "device/Transport.h"
#include "proto/Command.h"

#include <juce_core/juce_core.h>

#include <deque>
#include <functional>

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
    Malformed // bytes are not one sysex or one channel message, or the bytes are not a known read
              // although the command is labelled Read/Handshake
};

/** The safety gate: the only code that calls Transport::send. Every submitted command is
    checked (in this order) for well-formed bytes (one sysex, or one short channel message) that
    agree with its kind (a Read/Handshake label needs a known read frame: version request
    `00 00` or `70 19` / `70 07` / `70 13` at raw[6..7]), a connection, the allowlist, a finished
    backup, the write policy and dry-run mode, and is logged with its result (the newest
    maxLogEntries are kept). Short messages (CCs) go through the
    same checks as sysex, by their CommandKind. A failed send is never retried. */
class DeviceSession
{
  public:
    using Clock = std::function<juce::Time()>;

    /** `clock` stamps log entries; tests pass a fake one. */
    DeviceSession (Transport& transport, Allowlist allowlist, Clock clock = &juce::Time::getCurrentTime);

    static constexpr int maxScratchSlot = 35; // user slots 0-35

    /** `scratchSlot` is only used by WritePolicy::ScratchSlotOnly, and must then be 0-35;
        otherwise nothing changes and the result is false. */
    bool setPolicy (WritePolicy newPolicy, int scratchSlot = -1);
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

    static constexpr size_t maxLogEntries = 1000;

    const std::deque<LogEntry>& log() const noexcept;

  private:
    SendResult check (const Command& cmd) const;

    Transport& transport;
    Allowlist allowlist;
    Clock clock;
    WritePolicy currentPolicy{WritePolicy::ReadOnly};
    int scratchSlot = -1;
    bool dryRun = false;
    bool backupDone = false;
    std::deque<LogEntry> entries;

    JUCE_DECLARE_NON_COPYABLE (DeviceSession)
};
} // namespace qtn
