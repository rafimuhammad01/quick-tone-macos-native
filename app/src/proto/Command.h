#pragma once

#include "Bytes.h"

#include <string>

namespace qtn
{
/** What a command does to the pedal; DeviceSession decides from this whether it may be sent. */
enum class CommandKind
{
    Handshake,
    Read,
    LiveEdit,
    SavePreset,
    RenamePreset,
    SwapPreset,
    BulkWrite,
    IRWrite,
    IRDelete,
    SystemWrite
};

/** One outgoing message. `name` is the allowlist key; `targetSlot` is the preset slot a
    save/rename writes to (-1 when the command has no slot). `bytes` is either one complete
    sysex (F0 ... F7) or one short channel message such as a Control Change (`Bn cc vv`);
    see qtn::messageForm. */
struct Command
{
    std::string name;
    CommandKind kind{CommandKind::Read};
    int targetSlot = -1;
    Bytes bytes;
};
} // namespace qtn
