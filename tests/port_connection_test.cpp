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

TEST_CASE ("mg300_matcher_is_case_insensitive_substring")
{
    CHECK (qtn::isMg300PortName ("NUX MG-300"));
    CHECK (qtn::isMg300PortName ("nux mg-300 midi"));
    CHECK (qtn::isMg300PortName ("MG-300"));
    CHECK_FALSE (qtn::isMg300PortName ("MG-30"));
    CHECK_FALSE (qtn::isMg300PortName ("MG300"));
    CHECK_FALSE (qtn::isMg300PortName (""));
}

TEST_CASE ("starts_disconnected_without_rescan")
{
    FakeMidiPorts ports;
    ports.plug ({"MG-300", "in"}, {"MG-300", "out"});
    PortConnection connection (ports, qtn::isMg300PortName);

    CHECK_FALSE (connection.isConnected());
    CHECK (ports.openCalls.empty());
}

TEST_CASE ("opens_first_matching_input_and_output_by_identifier")
{
    FakeMidiPorts ports;
    ports.inputs = {{"IAC Bus", "iac-in"}, {"NUX MG-300", "mg-in-1"}, {"NUX MG-300 #2", "mg-in-2"}};
    ports.outputs = {{"IAC Bus", "iac-out"}, {"NUX MG-300", "mg-out-1"}, {"NUX MG-300 #2", "mg-out-2"}};
    PortConnection connection (ports, qtn::isMg300PortName);

    connection.rescan();

    REQUIRE (connection.isConnected());
    CHECK (ports.openCalls == std::vector<std::pair<std::string, std::string>>{{"mg-in-1", "mg-out-1"}});
}

TEST_CASE ("no_match_stays_disconnected_and_silent")
{
    FakeMidiPorts ports;
    ports.inputs = {{"IAC Bus", "iac-in"}};
    ports.outputs = {{"IAC Bus", "iac-out"}};
    PortConnection connection (ports, qtn::isMg300PortName);
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
    ports.inputs = {{"NUX MG-300", "mg-in"}};
    PortConnection connection (ports, qtn::isMg300PortName);

    connection.rescan();

    CHECK_FALSE (connection.isConnected());
    CHECK (ports.openCalls.empty());
}

TEST_CASE ("open_failure_stays_disconnected_and_silent")
{
    FakeMidiPorts ports;
    ports.plug ({"MG-300", "in"}, {"MG-300", "out"});
    ports.openSucceeds = false;
    PortConnection connection (ports, qtn::isMg300PortName);
    Recorder rec;
    rec.attach (connection);

    connection.rescan();

    CHECK_FALSE (connection.isConnected());
    CHECK (rec.events.empty());
}

TEST_CASE ("rescan_with_unchanged_ports_does_not_reopen")
{
    FakeMidiPorts ports;
    ports.plug ({"MG-300", "in"}, {"MG-300", "out"});
    PortConnection connection (ports, qtn::isMg300PortName);
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
    ports.plug ({"MG-300", "in"}, {"MG-300", "out"});
    PortConnection connection (ports, qtn::isMg300PortName);
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
    ports.plug ({"MG-300", "in-a"}, {"MG-300", "out-a"});
    PortConnection connection (ports, qtn::isMg300PortName);
    Recorder rec;
    rec.attach (connection);
    connection.rescan();

    // After sleep the pedal comes back under new identifiers in one change.
    ports.unplugAll();
    ports.plug ({"MG-300", "in-b"}, {"MG-300", "out-b"});
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
    ports.plug ({"MG-300", "in"}, {"MG-300", "out"});
    PortConnection connection (ports, qtn::isMg300PortName);
    connection.rescan();

    const Bytes sysex{0xF0, 0x43, 0x10, 0x7F, 0xF7};
    CHECK (connection.send (sysex));

    CHECK (ports.sentPayloads == std::vector<Bytes>{{0x43, 0x10, 0x7F}});
}

TEST_CASE ("send_while_disconnected_returns_false")
{
    FakeMidiPorts ports;
    PortConnection connection (ports, qtn::isMg300PortName);

    const Bytes sysex{0xF0, 0x43, 0xF7};
    CHECK_FALSE (connection.send (sysex));
    CHECK (ports.sentPayloads.empty());
}

TEST_CASE ("send_rejects_malformed_sysex")
{
    FakeMidiPorts ports;
    ports.plug ({"MG-300", "in"}, {"MG-300", "out"});
    PortConnection connection (ports, qtn::isMg300PortName);
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
    ports.plug ({"MG-300", "in"}, {"MG-300", "out"});
    PortConnection connection (ports, qtn::isMg300PortName);
    connection.rescan();

    const Bytes sysex{0xF0, 0xF7};
    CHECK (connection.send (sysex));
    CHECK (ports.sentPayloads == std::vector<Bytes>{Bytes{}});
}

TEST_CASE ("send_reports_port_failure")
{
    FakeMidiPorts ports;
    ports.plug ({"MG-300", "in"}, {"MG-300", "out"});
    PortConnection connection (ports, qtn::isMg300PortName);
    connection.rescan();
    ports.sendSucceeds = false;

    const Bytes sysex{0xF0, 0x43, 0xF7};
    CHECK_FALSE (connection.send (sysex));
}
