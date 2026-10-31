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
    ScratchSlotOnly, // + SavePreset / RenamePreset to the scratch slot (never SwapPreset)
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
    Malformed // bytes are not one sysex or one channel message, or they disagree with the label
              // (see DeviceSession)
};

/** The safety gate: the only code that calls Transport::send. Every submitted command is
    checked (in this order) for well-formed bytes (one sysex, or one short channel message) that
    agree with its kind, a connection, the allowlist, a finished backup, the write policy and
    dry-run mode, and is logged with its result (the newest maxLogEntries are kept). Short
    messages (CCs) go through the same checks as sysex, by their CommandKind. A failed send is
    never retried.

    Bytes agree with the kind when (else the result is Malformed):
    - Read/Handshake: the bytes are exactly one known 10-byte read: `F0 00 11 22 66 00 00 00 00
      F7` (version), or `... 70 19 00 F7`, `... 70 13 00 F7`, `... 70 07 <slot 0-71> F7`.
    - SavePreset / RenamePreset / SwapPreset: the bytes are that command's own frame, of its exact
      length (`0E` 113, `23` 21, `2C` 11 bytes), and its slot bytes are user slots 0-35 naming
      `targetSlot`: raw[8] for save and rename; for a swap raw[9] (drop target) == targetSlot and
      raw[8] (source) is another user slot.
    - A `0E` / `23` / `2C` frame never carries any other kind, except `0E` as a BulkWrite (to any
      user slot).
    A slot mismatch is Malformed, not BlockedByPolicy: the label and the bytes disagree, so no
    policy can make the command correct, and it is refused before the connection is even
    checked. Under ScratchSlotOnly a save/rename passes only for targetSlot == the scratch slot,
    and so only when its bytes write the scratch slot; a swap is always blocked there. */
class DeviceSession
{
  public:
    using Clock = std::function<juce::Time()>;

    /** `clock` stamps log entries; tests pass a fake one. */
    DeviceSession (Transport& transport, Allowlist allowlist, Clock clock = &juce::Time::getCurrentTime);

    static constexpr int maxScratchSlot = 35; // user slots 0-35

    /** `scratchSlot` is only used by WritePolicy::ScratchSlotOnly, and must then be 0-35;
        otherwise nothing changes and the result is false. */
    [[nodiscard]] bool setPolicy (WritePolicy newPolicy, int scratchSlot = -1);
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
