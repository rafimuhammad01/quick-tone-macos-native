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

TEST_CASE ("fake_records_cc_bytes_exactly")
{
    qtn::test::FakeTransport transport;
    qtn::Transport& base = transport;

    CHECK (base.send ({0xB0, 0x3C, 0x05}));
    CHECK (base.send ({0xF0, 0x01, 0xF7}));
    CHECK (base.send ({0xBF, 0x0B, 0x63}));

    CHECK (transport.sent == std::vector<Bytes>{{0xB0, 0x3C, 0x05}, {0xF0, 0x01, 0xF7}, {0xBF, 0x0B, 0x63}});
}

TEST_CASE ("fake_rejects_malformed_bytes")
{
    // Same contract as the real transport: one complete sysex or one short channel message.
    qtn::test::FakeTransport transport;

    CHECK_FALSE (transport.send ({0xB0, 0x3C}));
    CHECK_FALSE (transport.send ({0xF8}));
    CHECK_FALSE (transport.send ({}));
    CHECK (transport.sent.empty());
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
    qtn::PortConnection connection (ports, qtn::isPedalPortName);
    std::vector<bool> events;
    connection.onConnectionChanged = [&] (bool c)
    { events.push_back (c); };

    for (int cycle = 0; cycle < 2; ++cycle)
    {
        ports.plug ({"NUX NME-300", "in-" + std::to_string (cycle)},
                    {"NUX NME-300", "out-" + std::to_string (cycle)});
        connection.rescan();
        CHECK (connection.isConnected());

        ports.unplugAll();
        connection.rescan();
        CHECK_FALSE (connection.isConnected());
    }

    CHECK (events == std::vector<bool>{true, false, true, false});
}
