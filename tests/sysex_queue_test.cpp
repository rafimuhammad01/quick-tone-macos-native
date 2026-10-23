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

TEST_CASE ("queue_ignores_non_sysex_messages")
{
    qtn::SysexQueue queue;
    const Bytes noteOn{0x90, 0x40, 0x7F};
    const Bytes clock{0xF8};
    const Bytes partial{0xF0, 0x01, 0x02};
    const Bytes empty{};

    CHECK_FALSE (queue.push (noteOn));
    CHECK_FALSE (queue.push (clock));
    CHECK_FALSE (queue.push (partial));
    CHECK_FALSE (queue.push (empty));

    CHECK (queue.drain().empty());
}
