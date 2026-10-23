#pragma once

#include "Bytes.h"

#include <functional>

namespace qtn
{
// A bidirectional sysex link to the pedal.
// Implementations invoke both callbacks on the message thread.
class Transport
{
  public:
    virtual ~Transport() = default;

    virtual bool isConnected() const = 0;

    // Sends one complete sysex message, including F0 ... F7.
    // Project rule: only qtn::DeviceSession calls this.
    virtual bool send (const Bytes& sysex) = 0;

    // One complete sysex message (F0 ... F7) received from the device.
    std::function<void (const Bytes&)> onReceive;

    // Fired on every connect and disconnect transition.
    std::function<void (bool connected)> onConnectionChanged;
};
} // namespace qtn
