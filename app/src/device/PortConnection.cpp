#include "device/PortConnection.h"

#include <algorithm>
#include <cctype>
#include <optional>

namespace qtn
{
namespace
{
constexpr uint8_t sysexStart = 0xF0;
constexpr uint8_t sysexEnd = 0xF7;

bool containsIgnoreCase (std::string_view haystack, std::string_view needle)
{
    const auto lower = [] (char c)
    { return static_cast<char> (std::tolower (static_cast<unsigned char> (c))); };

    const auto it = std::search (haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                                 [&] (char a, char b)
                                 { return lower (a) == lower (b); });
    return it != haystack.end();
}

const PortInfo* findFirstMatch (const std::vector<PortInfo>& ports,
                                const PortConnection::NameMatcher& matcher)
{
    const auto it = std::find_if (ports.begin(), ports.end(), [&] (const PortInfo& p)
                                  { return matcher (p.name); });
    return it != ports.end() ? &*it : nullptr;
}

bool containsId (const std::vector<PortInfo>& ports, const std::string& id)
{
    return std::any_of (ports.begin(), ports.end(), [&] (const PortInfo& p)
                        { return p.identifier == id; });
}

// Returns the payload between F0 and F7, or nothing if `sysex` is not one
// well-formed sysex message (all payload bytes must be 7-bit).
std::optional<std::span<const uint8_t>> sysexPayload (std::span<const uint8_t> sysex)
{
    if (sysex.size() < 2 || sysex.front() != sysexStart || sysex.back() != sysexEnd)
        return std::nullopt;

    const auto payload = sysex.subspan (1, sysex.size() - 2);

    if (std::any_of (payload.begin(), payload.end(), [] (uint8_t b)
                     { return b >= 0x80; }))
        return std::nullopt;

    return payload;
}
} // namespace

bool isMg300PortName (std::string_view name)
{
    return containsIgnoreCase (name, "MG-300");
}

PortConnection::PortConnection (MidiPorts& p, NameMatcher m) : ports (p), matcher (std::move (m)) {}

void PortConnection::rescan()
{
    const auto inputs = ports.availableInputs();
    const auto outputs = ports.availableOutputs();

    if (connected)
    {
        if (containsId (inputs, openInputId) && containsId (outputs, openOutputId))
            return;

        ports.close();
        connected = false;
        openInputId.clear();
        openOutputId.clear();

        if (onConnectionChanged)
            onConnectionChanged (false);
    }

    const auto* in = findFirstMatch (inputs, matcher);
    const auto* out = findFirstMatch (outputs, matcher);

    if (in == nullptr || out == nullptr)
        return;

    if (! ports.open (in->identifier, out->identifier))
        return;

    connected = true;
    openInputId = in->identifier;
    openOutputId = out->identifier;

    if (onConnectionChanged)
        onConnectionChanged (true);
}

bool PortConnection::isConnected() const noexcept
{
    return connected;
}

bool PortConnection::send (std::span<const uint8_t> sysex)
{
    if (! connected)
        return false;

    const auto payload = sysexPayload (sysex);

    if (! payload.has_value())
        return false;

    return ports.sendSysexPayload (*payload);
}
} // namespace qtn
