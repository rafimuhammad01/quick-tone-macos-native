#include "device/DeviceSession.h"

#include "device/MidiBytes.h"
#include "proto/Codec.h"

#include <algorithm>
#include <array>
#include <utility>

namespace qtn
{
namespace
{
bool isReadKind (CommandKind kind) noexcept
{
    return kind == CommandKind::Handshake || kind == CommandKind::Read;
}

/** A read frame: raw[6] (message type) and raw[7] (command) of an NME-300 frame (R3b §2), and
    the only label it may carry. */
struct ReadFrame
{
    uint8_t type;
    uint8_t cmd;
    CommandKind kind;
};

/** The only frames that may carry a Read/Handshake label (R3b §4). Add a new read here. */
constexpr std::array<ReadFrame, 4> readFrames{{
    {0x00, 0x00, CommandKind::Handshake}, // version request (getVersion)
    {0x70, 0x19, CommandKind::Read},      // global parameters (getGlobleParameter)
    {0x70, 0x07, CommandKind::Read},      // one stored preset (getOnePreset)
    {0x70, 0x13, CommandKind::Read},      // edit buffer (getCurrentPreset)
}};

/** The bytes agree with the label: a Read/Handshake label needs a well-formed frame listed in
    readFrames with that kind. A short message (e.g. a CC) always changes pedal state, so it can
    never be a read. Other kinds are not checked here: a read labelled as a write only meets
    stricter checks. */
bool labelMatchesBytes (const Command& cmd)
{
    if (! isReadKind (cmd.kind))
        return true;

    if (! proto::parse (cmd.bytes).has_value())
        return false;

    return std::any_of (readFrames.begin(), readFrames.end(), [&] (const ReadFrame& f)
                        { return cmd.bytes[6] == f.type && cmd.bytes[7] == f.cmd && cmd.kind == f.kind; });
}

bool isValidScratchSlot (int slot) noexcept { return slot >= 0 && slot <= DeviceSession::maxScratchSlot; }

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

bool DeviceSession::setPolicy (WritePolicy newPolicy, int newScratchSlot)
{
    if (newPolicy == WritePolicy::ScratchSlotOnly && ! isValidScratchSlot (newScratchSlot))
        return false;

    currentPolicy = newPolicy;
    scratchSlot = newScratchSlot;
    return true;
}

WritePolicy DeviceSession::policy() const noexcept { return currentPolicy; }

void DeviceSession::setDryRun (bool enabled) noexcept { dryRun = enabled; }

void DeviceSession::markBackupDone (bool done) noexcept { backupDone = done; }

SendResult DeviceSession::check (const Command& cmd) const
{
    // The label is not trusted: only bytes that are a known read may use the Read/Handshake
    // exemption from the backup and policy checks.
    if (! messageForm (cmd.bytes).has_value() || ! labelMatchesBytes (cmd))
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

    while (entries.size() >= maxLogEntries)
        entries.pop_front();
    entries.push_back ({clock(), cmd, result});
    return result;
}

const std::deque<DeviceSession::LogEntry>& DeviceSession::log() const noexcept { return entries; }
} // namespace qtn
