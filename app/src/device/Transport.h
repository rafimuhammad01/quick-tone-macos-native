#pragma once

#include "Bytes.h"

#include <functional>

namespace qtn
{
// A bidirectional MIDI link to the pedal carrying sysex and short channel messages (CCs).
// Implementations invoke both callbacks on the message thread.
class Transport
{
  public:
    virtual ~Transport() = default;

    virtual bool isConnected() const = 0;

    // Sends one complete sysex message (F0 ... F7) or one short channel message such as a
    // Control Change (`Bn cc vv`), see qtn::messageForm. Returns false for anything else.
    // Project rule: only qtn::DeviceSession calls this.
    virtual bool send (const Bytes& message) = 0;

    // One message received from the device, as raw bytes: a complete sysex (F0 ... F7) or a
    // short channel message (e.g. an unsolicited CC when a knob is turned on the pedal).
    std::function<void (const Bytes&)> onReceive;

    // Fired on every connect and disconnect transition.
    std::function<void (bool connected)> onConnectionChanged;
};
} // namespace qtn
