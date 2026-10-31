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

// Offsets count from F0: raw[6] message type, raw[7] command, raw[8...] arguments (R3b §2).
constexpr size_t typeIndex = 6;
constexpr size_t cmdIndex = 7;
constexpr size_t firstArgIndex = 8;
constexpr uint8_t typeCommand = 0x70;

/** Every read is `F0 00 11 22 66 00 <type> <cmd> <arg> F7` (R3b §4, R7ab §2a items 1-4). */
constexpr size_t readFrameLength = 10;

/** ReadFrame::arg value meaning "any stored preset slot 0-71". */
constexpr int anyPresetSlot = -1;

/** A read frame: its type, command and single argument byte, and the only label it may carry. */
struct ReadFrame
{
    uint8_t type;
    uint8_t cmd;
    int arg;
    CommandKind kind;
};

/** The only frames that may carry a Read/Handshake label (R3b §4). Add a new read here. */
constexpr std::array<ReadFrame, 4> readFrames{{
    {0x00, 0x00, 0x00, CommandKind::Handshake},            // version request (getVersion)
    {typeCommand, 0x19, 0x00, CommandKind::Read},          // global parameters (getGlobleParameter)
    {typeCommand, 0x07, anyPresetSlot, CommandKind::Read}, // one stored preset (getOnePreset)
    {typeCommand, 0x13, 0x00, CommandKind::Read},          // edit buffer (getCurrentPreset)
}};

bool argMatches (const ReadFrame& f, uint8_t arg) noexcept
{
    return f.arg == anyPresetSlot ? arg < proto::presetCount : arg == f.arg;
}

/** The bytes agree with the label: a Read/Handshake label needs exactly one of readFrames
    (length, type, command and argument) listed with that kind. A short message (e.g. a CC)
    always changes pedal state, so it can never be a read. Other kinds are not checked here: a
    read labelled as a write only meets stricter checks. */
bool readLabelMatchesBytes (const Command& cmd)
{
    if (! isReadKind (cmd.kind))
        return true;

    const auto& raw = cmd.bytes;
    if (raw.size() != readFrameLength || ! proto::parse (raw).has_value())
        return false;

    return std::any_of (readFrames.begin(), readFrames.end(), [&] (const ReadFrame& f)
                        { return raw[typeIndex] == f.type && raw[cmdIndex] == f.cmd && argMatches (f, raw[firstArgIndex]) && cmd.kind == f.kind; });
}

bool isUserSlot (int slot) noexcept { return slot >= 0 && slot <= DeviceSession::maxScratchSlot; }

/** A write frame whose bytes name the preset slot(s) it changes, and the label it carries. */
struct SlotWriteFrame
{
    uint8_t cmd;
    size_t length;
    CommandKind kind;
};

/** Layouts: R7ab §2a items 6 / 10, R7cd §4 item 5. */
constexpr std::array<SlotWriteFrame, 3> slotWriteFrames{{
    {0x0E, 113, CommandKind::SavePreset},  // `P 70 0E <slot> <103 preset bytes> F7`
    {0x23, 21, CommandKind::RenamePreset}, // `P 70 23 <slot> <len> <10 codes> F7` (stored IR name)
    {0x2C, 11, CommandKind::SwapPreset},   // `P 70 2C <source> <target> F7`
}};

bool isSlotWriteKind (CommandKind kind) noexcept
{
    return std::any_of (slotWriteFrames.begin(), slotWriteFrames.end(), [kind] (const SlotWriteFrame& f)
                        { return f.kind == kind; });
}

/** The slotWriteFrames entry for the command byte of an NME-300 frame, if any. */
const SlotWriteFrame* findSlotWrite (const Bytes& raw)
{
    if (! proto::parse (raw).has_value())
        return nullptr;

    const auto it = std::find_if (slotWriteFrames.begin(), slotWriteFrames.end(), [&] (const SlotWriteFrame& f)
                                  { return raw[cmdIndex] == f.cmd; });
    return it != slotWriteFrames.end() ? &*it : nullptr;
}

/** The slot bytes agree with the label. A save / rename / swap label needs its own frame, of its
    exact length, whose slot bytes are user slots (0-35: QuickTone never writes 36-71, R7ab §1)
    and name `cmd.targetSlot`:
    - save `0E`, rename `23`: raw[8] == targetSlot;
    - swap `2C`: raw[9] (the drop target, which becomes the current preset) == targetSlot, and
      raw[8] (the dragged source) is another user slot.
    Such a frame may not carry any other label, except that a BulkWrite (import-all, R7ab item 9)
    may carry `0E` frames to any user slot. */
bool slotLabelMatchesBytes (const Command& cmd)
{
    const auto* frame = findSlotWrite (cmd.bytes);
    if (frame == nullptr)
        return ! isSlotWriteKind (cmd.kind);

    const bool bulkSave = cmd.kind == CommandKind::BulkWrite && frame->kind == CommandKind::SavePreset;
    if (cmd.kind != frame->kind && ! bulkSave)
        return false;

    const auto& raw = cmd.bytes;
    if (raw.size() != frame->length || raw[typeIndex] != typeCommand)
        return false;

    const int slot = raw[firstArgIndex];
    if (frame->kind != CommandKind::SwapPreset)
        return isUserSlot (slot) && (bulkSave || slot == cmd.targetSlot);

    const int target = raw[firstArgIndex + 1];
    return isUserSlot (slot) && isUserSlot (target) && slot != target && target == cmd.targetSlot;
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
        // A swap rewrites two slots, so it is never confined to the scratch slot.
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
    if (newPolicy == WritePolicy::ScratchSlotOnly && ! isUserSlot (newScratchSlot))
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
    // exemption from the backup and policy checks, and a save / rename / swap must write the
    // slot its label (and so the scratch-slot policy) names.
    if (! messageForm (cmd.bytes).has_value() || ! readLabelMatchesBytes (cmd) || ! slotLabelMatchesBytes (cmd))
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
