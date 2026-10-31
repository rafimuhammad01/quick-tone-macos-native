#pragma once

#include "Bytes.h"
#include "FakeTransport.h"
#include "model/Preset.h"
#include "proto/Codec.h"

#include <cstdint>
#include <string>

namespace qtn::test
{
/** Synthetic pedal replies to the connect sequence (R3b §3a, §4), built by hand. */
namespace replies
{
inline Bytes header (uint8_t type, uint8_t cmd) { return {0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, type, cmd}; }

inline Bytes version (const std::string& firmware, const std::string& name)
{
    auto raw = header (0x10, 0x00);
    raw.pop_back(); // raw[7] is the first firmware character
    for (size_t i = 0; i < 32; ++i)
        raw.push_back (i < firmware.size() ? static_cast<uint8_t> (firmware[i]) : 0x00);
    for (size_t i = 0; i < 8; ++i)
        raw.push_back (i < name.size() ? static_cast<uint8_t> (name[i]) : 0x00);
    raw.push_back (0xF7);
    return raw;
}

inline Bytes globals()
{
    auto raw = header (0x70, 0x18);
    for (const uint8_t b : {0x00, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06})
        raw.push_back (b);
    raw.push_back (0xF7);
    return raw;
}

inline Bytes presetMessage (uint8_t cmd, int slot, const Bytes& data)
{
    auto raw = header (0x70, cmd);
    raw.push_back (static_cast<uint8_t> (slot));
    raw.insert (raw.end(), data.begin(), data.end());
    raw.push_back (0xF7);
    return raw;
}

/** A 103-byte preset whose only content is `name`. */
inline Bytes namedPreset (const std::string& name)
{
    auto preset = *Preset::decode (Bytes (proto::presetSize, 0x00));
    [[maybe_unused]] const bool ok = preset.setName (name);
    return preset.encode();
}

/** The name the replies give to `slot`. */
inline std::string slotName (int slot) { return "Tone " + std::to_string (slot); }

/** The valid reply to one of the four connect requests. */
inline Bytes replyTo (const Bytes& request)
{
    if (request[6] == 0x00)
        return version ("V1.0.6", "MG-300");
    if (request[7] == 0x19)
        return globals();
    if (request[7] == 0x07)
        return presetMessage (0x06, request[8], namedPreset (slotName (request[8])));
    return presetMessage (0x12, 3, namedPreset ("Edit"));
}

/** Answers the newest request until the transport sends nothing new. */
inline void answerAll (FakeTransport& transport)
{
    while (! transport.sent.empty())
    {
        const auto before = transport.sent.size();
        transport.deliver (replyTo (transport.sent.back()));
        if (transport.sent.size() == before)
            return;
    }
}
} // namespace replies
} // namespace qtn::test
