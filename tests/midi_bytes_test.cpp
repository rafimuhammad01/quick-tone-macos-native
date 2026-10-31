#include "device/MidiBytes.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using qtn::Bytes;
using qtn::MessageForm;
using qtn::messageForm;

TEST_CASE ("message_form_sysex")
{
    CHECK (messageForm (Bytes{0xF0, 0x43, 0x10, 0xF7}) == MessageForm::SysEx);
    CHECK (messageForm (Bytes{0xF0, 0xF7}) == MessageForm::SysEx);
}

TEST_CASE ("message_form_short_channel_messages")
{
    CHECK (messageForm (Bytes{0xB0, 0x3C, 0x05}) == MessageForm::Short); // CC 60 on channel 1
    CHECK (messageForm (Bytes{0xBF, 0x7F, 0x7F}) == MessageForm::Short); // CC on channel 16
    CHECK (messageForm (Bytes{0x80, 0x40, 0x00}) == MessageForm::Short); // note off
    CHECK (messageForm (Bytes{0xEF, 0x00, 0x40}) == MessageForm::Short); // pitch bend
    CHECK (messageForm (Bytes{0xC0, 0x05}) == MessageForm::Short);       // program change: 2 bytes
    CHECK (messageForm (Bytes{0xD3, 0x7F}) == MessageForm::Short);       // channel pressure: 2 bytes
}

TEST_CASE ("malformed_short_message_rejected")
{
    const std::vector<Bytes> bad{
        {},
        {0xB0},                   // status only
        {0xB0, 0x3C},             // CC missing its value
        {0xB0, 0x3C, 0x05, 0x00}, // too long
        {0xC0, 0x05, 0x00},       // program change is 2 bytes
        {0xB0, 0x80, 0x05},       // data byte with the high bit set
        {0xB0, 0x3C, 0x80},
        {0x3C, 0x05, 0x00},       // no status byte (running status not allowed)
        {0xF8},                   // realtime
        {0xF2, 0x00, 0x00},       // system common
        {0xF0, 0x43, 0x10},       // sysex without F7
        {0xF0, 0x43, 0x80, 0xF7}, // sysex with a status byte inside
        {0x43, 0x10, 0xF7},
    };

    for (const auto& b : bad)
        CHECK_FALSE (messageForm (b).has_value());
}
