#include "device/PortConnection.h"
#include "support/FakeMidiPorts.h"
#include "support/FakeTransport.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using qtn::Bytes;

TEST_CASE ("fake_records_sent_bytes")
{
    qtn::test::FakeTransport transport;
    qtn::Transport& base = transport;

    CHECK (base.send ({0xF0, 0x01, 0xF7}));
    CHECK (base.send ({0xF0, 0x02, 0x03, 0xF7}));

    CHECK (transport.sent == std::vector<Bytes>{{0xF0, 0x01, 0xF7}, {0xF0, 0x02, 0x03, 0xF7}});
}

TEST_CASE ("fake_does_not_record_while_disconnected")
{
    qtn::test::FakeTransport transport;
    transport.setConnected (false);

    CHECK_FALSE (transport.send ({0xF0, 0x01, 0xF7}));
    CHECK (transport.sent.empty());
}

TEST_CASE ("fake_delivers_to_onReceive")
{
    qtn::test::FakeTransport transport;
    std::vector<Bytes> received;
    transport.onReceive = [&] (const Bytes& b)
    { received.push_back (b); };

    transport.deliver ({0xF0, 0x43, 0xF7});

    CHECK (received == std::vector<Bytes>{{0xF0, 0x43, 0xF7}});
}

TEST_CASE ("fake_set_connected_fires_connection_changed_on_change_only")
{
    qtn::test::FakeTransport transport; // starts connected
    std::vector<bool> events;
    transport.onConnectionChanged = [&] (bool c)
    { events.push_back (c); };

    transport.setConnected (true);
    transport.setConnected (false);
    transport.setConnected (false);
    transport.setConnected (true);

    CHECK (events == std::vector<bool>{false, true});
    CHECK (transport.isConnected());
}

TEST_CASE ("reconnect_fires_connection_changed_each_cycle")
{
    qtn::test::FakeMidiPorts ports;
    qtn::PortConnection connection (ports, qtn::isMg300PortName);
    std::vector<bool> events;
    connection.onConnectionChanged = [&] (bool c)
    { events.push_back (c); };

    for (int cycle = 0; cycle < 2; ++cycle)
    {
        ports.plug ({"NUX MG-300", "in-" + std::to_string (cycle)},
                    {"NUX MG-300", "out-" + std::to_string (cycle)});
        connection.rescan();
        CHECK (connection.isConnected());

        ports.unplugAll();
        connection.rescan();
        CHECK_FALSE (connection.isConnected());
    }

    CHECK (events == std::vector<bool>{true, false, true, false});
}
