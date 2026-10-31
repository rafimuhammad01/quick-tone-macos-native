#include "device/PortConnection.h"

#include "device/MidiBytes.h"

#include <algorithm>

namespace qtn
{
namespace
{
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
} // namespace

bool isPedalPortName (std::string_view name)
{
    // Original QuickTone opens a port whose name contains "NUX NME-300"
    // (juce::String::contains: case-sensitive substring); decomp
    // juce__NUX_MIDI_IO/checkDeivceIfChanged@1005f3200 L128, L620.
    return name.find ("NUX NME-300") != std::string_view::npos;
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

bool PortConnection::send (std::span<const uint8_t> message)
{
    if (! connected)
        return false;

    const auto form = messageForm (message);

    if (form == MessageForm::SysEx)
        return ports.sendSysexPayload (message.subspan (1, message.size() - 2));

    if (form == MessageForm::Short)
        return ports.sendShortMessage (message);

    return false;
}
} // namespace qtn
