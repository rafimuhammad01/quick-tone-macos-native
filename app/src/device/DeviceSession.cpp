#include "device/DeviceSession.h"

#include "device/MidiBytes.h"

#include <utility>

namespace qtn
{
namespace
{
bool isReadKind (CommandKind kind) noexcept
{
    return kind == CommandKind::Handshake || kind == CommandKind::Read;
}

bool policyAllows (WritePolicy policy, int scratchSlot, const Command& cmd) noexcept
{
    if (isReadKind (cmd.kind))
        return true;

    switch (policy)
    {
    case WritePolicy::ReadOnly:
        return false;
    case WritePolicy::LiveEditsOnly:
        return cmd.kind == CommandKind::LiveEdit;
    case WritePolicy::ScratchSlotOnly:
        if (cmd.kind == CommandKind::LiveEdit)
            return true;
        return (cmd.kind == CommandKind::SavePreset || cmd.kind == CommandKind::RenamePreset) && scratchSlot >= 0 && cmd.targetSlot == scratchSlot;
    case WritePolicy::Full:
        return true;
    }

    return false;
}
} // namespace

DeviceSession::DeviceSession (Transport& t, Allowlist a, Clock c)
    : transport (t), allowlist (std::move (a)), clock (std::move (c))
{
}

void DeviceSession::setPolicy (WritePolicy newPolicy, int newScratchSlot)
{
    currentPolicy = newPolicy;
    scratchSlot = newScratchSlot;
}

WritePolicy DeviceSession::policy() const noexcept { return currentPolicy; }

void DeviceSession::setDryRun (bool enabled) noexcept { dryRun = enabled; }

void DeviceSession::markBackupDone (bool done) noexcept { backupDone = done; }

SendResult DeviceSession::check (const Command& cmd) const
{
    // A short message (e.g. a CC) always changes pedal state, so it may never use the
    // Read/Handshake exemption from the backup and policy checks.
    const auto form = messageForm (cmd.bytes);
    if (! form.has_value() || (*form == MessageForm::Short && isReadKind (cmd.kind)))
        return SendResult::Malformed;

    if (! transport.isConnected())
        return SendResult::Disconnected;

    if (! allowlist.contains (cmd.name))
        return SendResult::NotAllowlisted;

    if (! isReadKind (cmd.kind) && ! backupDone)
        return SendResult::BackupMissing;

    if (! policyAllows (currentPolicy, scratchSlot, cmd))
        return SendResult::BlockedByPolicy;

    if (dryRun)
        return SendResult::DryRun;

    return SendResult::Sent;
}

SendResult DeviceSession::submit (const Command& cmd)
{
    auto result = check (cmd);

    // A failed send (the link dropped between the check and the send) is reported, never retried.
    if (result == SendResult::Sent && ! transport.send (cmd.bytes))
        result = SendResult::Disconnected;

    entries.push_back ({clock(), cmd, result});
    return result;
}

const std::vector<DeviceSession::LogEntry>& DeviceSession::log() const noexcept { return entries; }
} // namespace qtn
