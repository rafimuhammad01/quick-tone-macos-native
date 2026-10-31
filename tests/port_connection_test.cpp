#include "device/PortConnection.h"
#include "support/FakeMidiPorts.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using qtn::Bytes;
using qtn::PortConnection;
using qtn::test::FakeMidiPorts;

namespace
{
struct Recorder
{
    std::vector<bool> events;

    void attach (PortConnection& c)
    {
        c.onConnectionChanged = [this] (bool connected)
        { events.push_back (connected); };
    }
};
} // namespace

TEST_CASE ("pedal_matcher_accepts_names_containing_nux_nme_300")
{
    CHECK (qtn::isPedalPortName ("NUX NME-300"));
    CHECK (qtn::isPedalPortName ("NUX NME-300 MIDI 1"));
    CHECK (qtn::isPedalPortName ("Something NUX NME-300"));
}

TEST_CASE ("pedal_matcher_rejects_other_models_and_wrong_case")
{
    CHECK_FALSE (qtn::isPedalPortName ("nux nme-300"));
    CHECK_FALSE (qtn::isPedalPortName ("NUX MG-300"));
    CHECK_FALSE (qtn::isPedalPortName ("NUX MG-300V2"));
    CHECK_FALSE (qtn::isPedalPortName ("NUX MG-30"));
    CHECK_FALSE (qtn::isPedalPortName ("NME-300"));
    CHECK_FALSE (qtn::isPedalPortName (""));
}

TEST_CASE ("starts_disconnected_without_rescan")
{
    FakeMidiPorts ports;
    ports.plug ({"NUX NME-300", "in"}, {"NUX NME-300", "out"});
    PortConnection connection (ports, qtn::isPedalPortName);

    CHECK_FALSE (connection.isConnected());
    CHECK (ports.openCalls.empty());
}

TEST_CASE ("opens_first_matching_input_and_output_by_identifier")
{
    FakeMidiPorts ports;
    ports.inputs = {{"IAC Bus", "iac-in"}, {"NUX NME-300", "mg-in-1"}, {"NUX NME-300 #2", "mg-in-2"}};
    ports.outputs = {{"IAC Bus", "iac-out"}, {"NUX NME-300", "mg-out-1"}, {"NUX NME-300 #2", "mg-out-2"}};
    PortConnection connection (ports, qtn::isPedalPortName);

    connection.rescan();

    REQUIRE (connection.isConnected());
    CHECK (ports.openCalls == std::vector<std::pair<std::string, std::string>>{{"mg-in-1", "mg-out-1"}});
}

TEST_CASE ("no_match_stays_disconnected_and_silent")
{
    FakeMidiPorts ports;
    ports.inputs = {{"IAC Bus", "iac-in"}};
    ports.outputs = {{"IAC Bus", "iac-out"}};
    PortConnection connection (ports, qtn::isPedalPortName);
    Recorder rec;
    rec.attach (connection);

    connection.rescan();

    CHECK_FALSE (connection.isConnected());
    CHECK (ports.openCalls.empty());
    CHECK (rec.events.empty());
}

TEST_CASE ("input_without_output_does_not_connect")
{
    FakeMidiPorts ports;
    ports.inputs = {{"NUX NME-300", "mg-in"}};
    PortConnection connection (ports, qtn::isPedalPortName);

    connection.rescan();

    CHECK_FALSE (connection.isConnected());
    CHECK (ports.openCalls.empty());
}

TEST_CASE ("open_failure_stays_disconnected_and_silent")
{
    FakeMidiPorts ports;
    ports.plug ({"NUX NME-300", "in"}, {"NUX NME-300", "out"});
    ports.openSucceeds = false;
    PortConnection connection (ports, qtn::isPedalPortName);
    Recorder rec;
    rec.attach (connection);

    connection.rescan();

    CHECK_FALSE (connection.isConnected());
    CHECK (rec.events.empty());
}

TEST_CASE ("rescan_with_unchanged_ports_does_not_reopen")
{
    FakeMidiPorts ports;
    ports.plug ({"NUX NME-300", "in"}, {"NUX NME-300", "out"});
    PortConnection connection (ports, qtn::isPedalPortName);
    Recorder rec;
    rec.attach (connection);

    connection.rescan();
    ports.inputs.push_back ({"Other", "other-in"}); // unrelated device appears
    connection.rescan();

    CHECK (ports.openCalls.size() == 1);
    CHECK (ports.closeCount == 0);
    CHECK (rec.events == std::vector<bool>{true});
}

TEST_CASE ("losing_only_the_output_closes_both")
{
    FakeMidiPorts ports;
    ports.plug ({"NUX NME-300", "in"}, {"NUX NME-300", "out"});
    PortConnection connection (ports, qtn::isPedalPortName);
    Recorder rec;
    rec.attach (connection);
    connection.rescan();

    ports.outputs.clear();
    connection.rescan();

    CHECK_FALSE (connection.isConnected());
    CHECK (ports.closeCount == 1);
    CHECK (rec.events == std::vector<bool>{true, false});
}

TEST_CASE ("reenumerated_identifiers_close_then_reopen")
{
    FakeMidiPorts ports;
    ports.plug ({"NUX NME-300", "in-a"}, {"NUX NME-300", "out-a"});
    PortConnection connection (ports, qtn::isPedalPortName);
    Recorder rec;
    rec.attach (connection);
    connection.rescan();

    // After sleep the pedal comes back under new identifiers in one change.
    ports.unplugAll();
    ports.plug ({"NUX NME-300", "in-b"}, {"NUX NME-300", "out-b"});
    connection.rescan();

    CHECK (connection.isConnected());
    CHECK (ports.closeCount == 1);
    REQUIRE (ports.openCalls.size() == 2);
    CHECK (ports.openCalls.back() == std::pair<std::string, std::string>{"in-b", "out-b"});
    CHECK (rec.events == std::vector<bool>{true, false, true});
}

TEST_CASE ("send_strips_framing_and_forwards_payload")
{
    FakeMidiPorts ports;
    ports.plug ({"NUX NME-300", "in"}, {"NUX NME-300", "out"});
    PortConnection connection (ports, qtn::isPedalPortName);
    connection.rescan();

    const Bytes sysex{0xF0, 0x43, 0x10, 0x7F, 0xF7};
    CHECK (connection.send (sysex));

    CHECK (ports.sentPayloads == std::vector<Bytes>{{0x43, 0x10, 0x7F}});
}

TEST_CASE ("send_while_disconnected_returns_false")
{
    FakeMidiPorts ports;
    PortConnection connection (ports, qtn::isPedalPortName);

    const Bytes sysex{0xF0, 0x43, 0xF7};
    CHECK_FALSE (connection.send (sysex));
    CHECK (ports.sentPayloads.empty());
}

TEST_CASE ("send_rejects_malformed_sysex")
{
    FakeMidiPorts ports;
    ports.plug ({"NUX NME-300", "in"}, {"NUX NME-300", "out"});
    PortConnection connection (ports, qtn::isPedalPortName);
    connection.rescan();

    const std::vector<Bytes> bad{
        {},
        {0xF0},
        {0x43, 0x10, 0xF7},       // no F0
        {0xF0, 0x43, 0x10},       // no F7
        {0xF0, 0x43, 0x80, 0xF7}, // status byte inside the payload
        {0xF0, 0xF7, 0x01, 0xF7}, // early F7
    };
    for (const auto& b : bad)
        CHECK_FALSE (connection.send (b));

    CHECK (ports.sentPayloads.empty());
}

TEST_CASE ("send_accepts_empty_payload")
{
    FakeMidiPorts ports;
    ports.plug ({"NUX NME-300", "in"}, {"NUX NME-300", "out"});
    PortConnection connection (ports, qtn::isPedalPortName);
    connection.rescan();

    const Bytes sysex{0xF0, 0xF7};
    CHECK (connection.send (sysex));
    CHECK (ports.sentPayloads == std::vector<Bytes>{Bytes{}});
}

TEST_CASE ("send_reports_port_failure")
{
    FakeMidiPorts ports;
    ports.plug ({"NUX NME-300", "in"}, {"NUX NME-300", "out"});
    PortConnection connection (ports, qtn::isPedalPortName);
    connection.rescan();
    ports.sendSucceeds = false;

    const Bytes sysex{0xF0, 0x43, 0xF7};
    CHECK_FALSE (connection.send (sysex));
}

TEST_CASE ("send_forwards_short_message_bytes_exactly")
{
    FakeMidiPorts ports;
    ports.plug ({"NUX NME-300", "in"}, {"NUX NME-300", "out"});
    PortConnection connection (ports, qtn::isPedalPortName);
    connection.rescan();

    CHECK (connection.send (Bytes{0xB0, 0x3C, 0x05}));
    CHECK (connection.send (Bytes{0xC0, 0x05}));

    CHECK (ports.sentShortMessages == std::vector<Bytes>{{0xB0, 0x3C, 0x05}, {0xC0, 0x05}});
    CHECK (ports.sentPayloads.empty());
}

TEST_CASE ("send_rejects_malformed_short_message")
{
    FakeMidiPorts ports;
    ports.plug ({"NUX NME-300", "in"}, {"NUX NME-300", "out"});
    PortConnection connection (ports, qtn::isPedalPortName);
    connection.rescan();

    const std::vector<Bytes> bad{{0xB0}, {0xB0, 0x3C}, {0xB0, 0x3C, 0x05, 0x00}, {0xB0, 0x3C, 0x80}, {0xF8}, {0x3C, 0x05}};
    for (const auto& b : bad)
        CHECK_FALSE (connection.send (b));

    CHECK (ports.sentShortMessages.empty());
}

TEST_CASE ("short_message_while_disconnected_returns_false")
{
    FakeMidiPorts ports;
    PortConnection connection (ports, qtn::isPedalPortName);

    CHECK_FALSE (connection.send (Bytes{0xB0, 0x3C, 0x05}));
    CHECK (ports.sentShortMessages.empty());
}

TEST_CASE ("short_message_reports_port_failure")
{
    FakeMidiPorts ports;
    ports.plug ({"NUX NME-300", "in"}, {"NUX NME-300", "out"});
    PortConnection connection (ports, qtn::isPedalPortName);
    connection.rescan();
    ports.sendSucceeds = false;

    CHECK_FALSE (connection.send (Bytes{0xB0, 0x3C, 0x05}));
}
