#include "device/SysexQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using qtn::Bytes;

TEST_CASE ("queue_drains_complete_sysex_in_order")
{
    qtn::SysexQueue queue;
    const Bytes a{0xF0, 0x01, 0xF7};
    const Bytes b{0xF0, 0x02, 0x03, 0xF7};

    CHECK (queue.push (a));
    CHECK (queue.push (b));

    CHECK (queue.drain() == std::vector<Bytes>{a, b});
    CHECK (queue.drain().empty());
}

TEST_CASE ("queue_delivers_incoming_cc_as_raw_bytes")
{
    // R3b §3b: the pedal sends unsolicited 3-byte CCs when a knob is turned.
    qtn::SysexQueue queue;
    const Bytes cc{0xB0, 0x0B, 0x40};
    const Bytes sysex{0xF0, 0x01, 0xF7};

    CHECK (queue.push (cc));
    CHECK (queue.push (sysex));

    CHECK (queue.drain() == std::vector<Bytes>{cc, sysex});
}

TEST_CASE ("queue_ignores_realtime_and_malformed_messages")
{
    qtn::SysexQueue queue;
    const Bytes shortCc{0xB0, 0x0B};
    const Bytes clock{0xF8};
    const Bytes partial{0xF0, 0x01, 0x02};
    const Bytes empty{};

    CHECK_FALSE (queue.push (shortCc));
    CHECK_FALSE (queue.push (clock));
    CHECK_FALSE (queue.push (partial));
    CHECK_FALSE (queue.push (empty));

    CHECK (queue.drain().empty());
}
