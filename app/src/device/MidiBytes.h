#pragma once

#include "Bytes.h"

#include <cstdint>
#include <optional>
#include <span>

namespace qtn
{
/** The two kinds of MIDI message this app sends and receives. */
enum class MessageForm
{
    SysEx, // F0, 7-bit data bytes, F7
    Short  // one channel message: status 0x80-0xEF plus its 1 or 2 data bytes (e.g. CC `Bn cc vv`)
};

/** Which form `raw` is, or empty if it is neither one complete sysex nor one complete channel
    message. Realtime/system-common bytes, running status and stray data bytes are rejected. */
[[nodiscard]] std::optional<MessageForm> messageForm (std::span<const uint8_t> raw) noexcept;
} // namespace qtn
