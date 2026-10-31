#include "device/Connector.h"

#include <utility>

namespace qtn
{
namespace
{
juce::String describe (SendResult result)
{
    switch (result)
    {
    case SendResult::Sent:
        return "sent";
    case SendResult::DryRun:
        return "dry run";
    case SendResult::BlockedByPolicy:
        return "blocked by policy";
    case SendResult::NotAllowlisted:
        return "not allowlisted";
    case SendResult::Disconnected:
        return "disconnected";
    case SendResult::BackupMissing:
        return "backup missing";
    case SendResult::Malformed:
        return "malformed";
    }
    return "unknown";
}
} // namespace

Connector::Connector (DeviceSession& s, BackupStore& b, DeviceState& d, Clock c)
    : session (s), store (b), state (d), clock (std::move (c))
{
}

void Connector::start()
{
    session.markBackupDone (false);
    state.clear();

    done = 0;
    slot = 0;
    info.reset();
    globals.clear();
    presets.clear();

    step = Step::version;
    send (proto::getVersion());
}

bool Connector::isRunning() const noexcept { return step != Step::idle; }

void Connector::send (const Command& cmd)
{
    pendingName = cmd.name;
    sentAtMs = clock().toMilliseconds();

    // Through the gate only. Anything but Sent means no reply will come: fail now, never retry.
    if (const auto result = session.submit (cmd); result != SendResult::Sent)
        fail (juce::String (cmd.name) + " was not sent: " + describe (result));
}

void Connector::fail (const juce::String& why)
{
    step = Step::idle;
    if (onFailed)
        onFailed (why);
}

void Connector::replied()
{
    ++done;
    if (onProgress)
        onProgress (done, totalRequests);
}

bool Connector::accept (const Bytes& raw)
{
    const auto kind = proto::classify (raw);

    switch (step)
    {
    case Step::idle:
        return false;
    case Step::version:
        info = parseVersionReply (raw);
        return info.has_value();
    case Step::globals:
        if (kind != proto::ReplyKind::GlobalParams)
            return false;
        globals = raw;
        return true;
    case Step::presets:
        // UNVERIFIED (R3b §4): the original does not compare raw[8] with the request; we do, so a
        // reply for another slot is ignored (and then times out) instead of stored in the wrong slot.
        return kind == proto::ReplyKind::Preset && proto::presetSlot (raw) == slot;
    case Step::editBuffer:
        return kind == proto::ReplyKind::EditBuffer;
    }
    return false;
}

void Connector::handleMessage (const Bytes& raw)
{
    // A preset saved on the pedal mid-read may change a slot already read, so the backup would
    // not be one snapshot: fail once (no backup, no resend).
    if (isRunning() && proto::classify (raw) == proto::ReplyKind::PresetSavedOnPedal)
        return fail ("A preset was saved on the pedal during the read; reconnect to read again");

    if (! accept (raw))
        return;

    switch (step)
    {
    case Step::idle:
        return;
    case Step::version:
        replied();
        step = Step::globals;
        return send (proto::getGlobalParameters());
    case Step::globals:
        replied();
        step = Step::presets;
        return send (*proto::getOnePreset (slot));
    case Step::presets:
    {
        auto data = proto::presetData (raw);
        if (! data)
            return fail ("Preset " + juce::String (slot) + " reply has invalid data");

        presets.push_back (std::move (*data));
        replied();
        if (++slot < proto::presetCount)
            return send (*proto::getOnePreset (slot));

        step = Step::editBuffer;
        return send (proto::getCurrentPreset());
    }
    case Step::editBuffer:
    {
        const auto data = proto::presetData (raw);
        if (! data)
            return fail ("Edit buffer reply has invalid data");

        // raw[8] is the current preset index (R3b §3a); anything outside 0-71 is not a slot.
        const auto current = *proto::presetSlot (raw);
        if (current < 0 || current >= proto::presetCount)
            return fail ("Edit buffer reply has invalid current slot " + juce::String (current));

        replied();
        return finish (*data, current);
    }
    }
}

void Connector::finish (const Bytes& editData, int currentSlot)
{
    step = Step::idle;

    std::vector<PresetBlob> blobs;
    blobs.reserve (presets.size() + 2);
    for (size_t i = 0; i < presets.size(); ++i)
    {
        // presetData always returns presetSize bytes, so decode cannot fail here.
        const auto preset = Preset::decode (presets[i]);
        const auto index = static_cast<int> (i);
        state.setPreset (index, *preset);
        blobs.push_back ({index, preset->name().value_or (""), presets[i]});
    }
    state.setGlobalParams (globals);
    state.setEditBuffer (*Preset::decode (editData), currentSlot);

    blobs.push_back ({globalParamsBackupSlot, "global-parameters", globals});
    blobs.push_back ({editBufferBackupSlot, "edit-buffer", editData});

    const auto result = store.save (blobs, clock());
    session.markBackupDone (result.ok);

    if (! result.ok)
    {
        if (onFailed)
            onFailed ("Backup failed: " + result.error);
        return;
    }

    if (onConnected)
        onConnected (*info, result.folder);
}

void Connector::handleConnectionChanged (bool connected)
{
    if (connected)
        return start();

    session.markBackupDone (false);
    if (isRunning())
        fail ("Pedal disconnected while waiting for " + pendingName);
}

void Connector::tick()
{
    if (! isRunning())
        return;

    if (clock().toMilliseconds() - sentAtMs >= timeoutMs)
        fail ("No reply to " + pendingName + " within " + juce::String (timeoutMs) + " ms");
}
} // namespace qtn
