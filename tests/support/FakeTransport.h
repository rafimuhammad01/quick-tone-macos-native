#pragma once

#include "device/MidiBytes.h"
#include "device/Transport.h"

#include <vector>

namespace qtn::test
{
// In-memory Transport for tests. Starts connected (pass false to start
// disconnected). send() records the bytes only while connected and only if they are one
// sysex or one short channel message, like the real transport.
class FakeTransport final : public Transport
{
  public:
    explicit FakeTransport (bool startConnected = true) : connected (startConnected) {}

    std::vector<Bytes> sent;

    bool isConnected() const override { return connected; }

    bool send (const Bytes& message) override
    {
        if (! connected || ! messageForm (message).has_value())
            return false;

        sent.push_back (message);
        return true;
    }

    // Simulates bytes arriving from the device.
    void deliver (const Bytes& message)
    {
        if (onReceive)
            onReceive (message);
    }

    // Simulates plug/unplug; fires onConnectionChanged only on a change.
    void setConnected (bool nowConnected)
    {
        if (nowConnected == connected)
            return;

        connected = nowConnected;

        if (onConnectionChanged)
            onConnectionChanged (connected);
    }

  private:
    bool connected;
};
} // namespace qtn::test
