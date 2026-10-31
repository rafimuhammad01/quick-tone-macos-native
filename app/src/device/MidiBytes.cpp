#include "device/MidiBytes.h"

#include <algorithm>

namespace qtn
{
namespace
{
constexpr uint8_t sysexStart = 0xF0;
constexpr uint8_t sysexEnd = 0xF7;

bool allSevenBit (std::span<const uint8_t> bytes) noexcept
{
    return std::all_of (bytes.begin(), bytes.end(), [] (uint8_t b)
                        { return b < 0x80; });
}

// Total length of a channel message with this status byte (MIDI 1.0: program change Cn and
// channel pressure Dn carry one data byte, the other channel messages two); 0 if not a channel status.
size_t channelMessageLength (uint8_t status) noexcept
{
    if (status < 0x80 || status >= 0xF0)
        return 0;

    const auto type = status & 0xF0;
    return (type == 0xC0 || type == 0xD0) ? 2 : 3;
}
} // namespace

std::optional<MessageForm> messageForm (std::span<const uint8_t> raw) noexcept
{
    if (raw.empty())
        return std::nullopt;

    if (raw.front() == sysexStart)
    {
        if (raw.size() >= 2 && raw.back() == sysexEnd && allSevenBit (raw.subspan (1, raw.size() - 2)))
            return MessageForm::SysEx;

        return std::nullopt;
    }

    const auto length = channelMessageLength (raw.front());

    if (length != 0 && raw.size() == length && allSevenBit (raw.subspan (1)))
        return MessageForm::Short;

    return std::nullopt;
}
} // namespace qtn
