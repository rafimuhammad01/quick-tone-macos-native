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
    save/rename writes to (-1 when the command has no slot). `sysex` includes F0 ... F7. */
struct Command
{
    std::string name;
    CommandKind kind{CommandKind::Read};
    int targetSlot = -1;
    Bytes sysex;
};
} // namespace qtn
