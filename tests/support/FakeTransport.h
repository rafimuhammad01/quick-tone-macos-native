#pragma once

#include "device/Transport.h"

#include <vector>

namespace qtn::test
{
// In-memory Transport for tests. Starts connected (pass false to start
// disconnected). send() records the bytes only while connected.
class FakeTransport final : public Transport
{
  public:
    explicit FakeTransport (bool startConnected = true) : connected (startConnected) {}

    std::vector<Bytes> sent;

    bool isConnected() const override { return connected; }

    bool send (const Bytes& sysex) override
    {
        if (! connected)
            return false;

        sent.push_back (sysex);
        return true;
    }

    // Simulates bytes arriving from the device.
    void deliver (const Bytes& sysex)
    {
        if (onReceive)
            onReceive (sysex);
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
