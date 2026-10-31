#include "proto/Codec.h"

#include <catch2/catch_test_macros.hpp>

#include <random>

namespace
{
using qtn::Bytes;
using qtn::CommandKind;
using namespace qtn::proto;

/** A message of `length` bytes: `F0 00 11 22 66 00 <type> <cmd> 00... F7`. */
Bytes message (size_t length, uint8_t cmd, uint8_t type = 0x70)
{
    Bytes raw (length, 0x00);
    const Bytes head{0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, type, cmd};
    std::copy (head.begin(), head.end(), raw.begin());
    raw.back() = 0xF7;
    return raw;
}

/** 113-byte preset message: raw[7] = cmd, raw[8] = slot, raw[9 + i] = i % 100. */
Bytes presetMessage (uint8_t cmd, uint8_t slot)
{
    auto raw = message (113, cmd);
    raw[8] = slot;
    for (size_t i = 0; i < 103; ++i)
        raw[9 + i] = static_cast<uint8_t> (i % 100);
    return raw;
}
} // namespace

// ---- Frames ----

TEST_CASE ("version_request_bytes")
{
    // R3b §2 worked example / §4 step 1
    CHECK (versionRequestFrame() == Bytes{0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x00, 0x00, 0x00, 0xF7});
}

TEST_CASE ("tempo_120_frame_bytes")
{
    // R3b §2 worked example: tempo 120 BPM
    CHECK (frame (0x17, {0x00, 0x07, 0x08}) == Bytes{0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x70, 0x17, 0x00, 0x07, 0x08, 0xF7});
}

TEST_CASE ("save_frame_is_113_bytes")
{
    // R3b §2 worked example: write preset to slot 5 = 113 bytes (frame() only; no 0E builder yet)
    Bytes args (1 + presetSize, 0x00);
    args[0] = 0x05;
    const auto raw = frame (0x0E, args);
    REQUIRE (raw.size() == 113);
    CHECK (raw[6] == 0x70);
    CHECK (raw[7] == 0x0E);
    CHECK (raw[8] == 0x05);
    CHECK (raw.back() == 0xF7);
}

TEST_CASE ("frame_rejects_high_bit_args")
{
    CHECK (frame (0x17, {0x00, 0x80}).empty());
    CHECK (frame (0x80, {}).empty());
}

// ---- Parse ----

TEST_CASE ("parse_read_preset_5")
{
    // R3b §2 worked example: read preset 5
    const auto f = parse ({0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x70, 0x07, 0x05, 0xF7});
    REQUIRE (f.has_value());
    CHECK (f->cmd == 0x07);
    CHECK (f->args == Bytes{0x05});
}

TEST_CASE ("parse_version_request")
{
    // R3b §2: message type 00 for the version request
    const auto f = parse (versionRequestFrame());
    REQUIRE (f.has_value());
    CHECK (f->cmd == 0x00);
    CHECK (f->args == Bytes{0x00});
}

TEST_CASE ("parse_rejects_bad_header")
{
    const Bytes good{0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x70, 0x07, 0x05, 0xF7};
    REQUIRE (parse (good).has_value());

    auto wrongId = good;
    wrongId[3] = 0x23;
    CHECK_FALSE (parse (wrongId).has_value());

    auto wrongType = good;
    wrongType[6] = 0x71;
    CHECK_FALSE (parse (wrongType).has_value());

    const Bytes noF0 (good.begin() + 1, good.end());
    CHECK_FALSE (parse (noF0).has_value());

    const Bytes noF7 (good.begin(), good.end() - 1);
    CHECK_FALSE (parse (noF7).has_value());

    CHECK_FALSE (parse ({0xF0, 0xF7}).has_value());
    CHECK_FALSE (parse ({}).has_value());
}

TEST_CASE ("parse_rejects_high_bit_data")
{
    CHECK_FALSE (parse ({0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x70, 0x07, 0x85, 0xF7}).has_value());
    CHECK_FALSE (parse ({0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x70, 0xF7, 0x05, 0xF7}).has_value());
}

// ---- Classify ----

TEST_CASE ("classify_by_length_and_byte7")
{
    // R3b §3a reply checks
    CHECK (classify (message (48, 0x00, 0x10)) == ReplyKind::Version);
    CHECK_FALSE (classify (message (48, 0x00, 0x70)).has_value()); // version needs raw[6] == 10
    CHECK (classify (message (17, 0x18)) == ReplyKind::GlobalParams);
    CHECK_FALSE (classify (message (17, 0x19)).has_value());
    CHECK_FALSE (classify (message (18, 0x18)).has_value());
    CHECK (classify (message (113, 0x06)) == ReplyKind::Preset);
    CHECK (classify (message (113, 0x12)) == ReplyKind::EditBuffer);
    CHECK_FALSE (classify (message (113, 0x07)).has_value());
    CHECK_FALSE (classify (message (112, 0x06)).has_value());
}

TEST_CASE ("classify_unsolicited_kinds")
{
    // R3b §3b handleIncomingMidiMessage, dispatched by length
    CHECK (classify ({0xB0, 0x3C, 0x05}) == ReplyKind::ControlChange);
    CHECK (classify (message (12, 0x17)) == ReplyKind::Tempo);
    CHECK (classify (message (19, 0x00)) == ReplyKind::ModuleChange);
    CHECK (classify (message (21, 0x00)) == ReplyKind::AmpCabChange);
    CHECK (classify (message (113, 0x0E)) == ReplyKind::PresetSavedOnPedal);
    CHECK (classify (message (128, 0x58)) == ReplyKind::IrData);

    auto done = message (128, 0x50);
    done[8] = done[9] = done[10] = 0x7F;
    CHECK (classify (done) == ReplyKind::IrDownloadDone);
    CHECK_FALSE (classify (message (128, 0x50)).has_value()); // 50 with another address is a start frame
}

TEST_CASE ("classify_rejects_non_sysex")
{
    auto noF7 = message (113, 0x06);
    noF7.back() = 0x00;
    CHECK_FALSE (classify (noF7).has_value());
    auto noF0 = message (17, 0x18);
    noF0[0] = 0x00;
    CHECK_FALSE (classify (noF0).has_value());
    CHECK_FALSE (classify ({0x90, 0x3C, 0x05}).has_value()); // note on, not CC
    CHECK_FALSE (classify ({}).has_value());
}

// ---- Read-side request builders ----

TEST_CASE ("get_version_command")
{
    // R3b §4 step 1
    const auto c = getVersion();
    CHECK (c.name == "getVersion");
    CHECK (c.kind == CommandKind::Handshake);
    CHECK (c.targetSlot == -1);
    CHECK (c.bytes == Bytes{0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x00, 0x00, 0x00, 0xF7});
}

TEST_CASE ("get_global_parameters_command")
{
    // R3b §4 step 2
    const auto c = getGlobalParameters();
    CHECK (c.name == "getGlobleParameter");
    CHECK (c.kind == CommandKind::Read);
    CHECK (c.bytes == Bytes{0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x70, 0x19, 0x00, 0xF7});
}

TEST_CASE ("read_preset_5_command")
{
    // R3b §2 worked example / §4 step 3
    const auto c = getOnePreset (5);
    REQUIRE (c.has_value());
    CHECK (c->name == "getOnePreset");
    CHECK (c->kind == CommandKind::Read);
    CHECK (c->bytes == Bytes{0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x70, 0x07, 0x05, 0xF7});
}

TEST_CASE ("read_preset_rejects_slot_out_of_range")
{
    CHECK (getOnePreset (0).has_value());
    CHECK (getOnePreset (71).has_value());
    CHECK (getOnePreset (71)->bytes[8] == 71);
    CHECK_FALSE (getOnePreset (-1).has_value());
    CHECK_FALSE (getOnePreset (72).has_value());
}

TEST_CASE ("get_current_preset_command")
{
    // R3b §4 step 4
    const auto c = getCurrentPreset();
    CHECK (c.name == "getCurrentPreset");
    CHECK (c.kind == CommandKind::Read);
    CHECK (c.bytes == Bytes{0xF0, 0x00, 0x11, 0x22, 0x66, 0x00, 0x70, 0x13, 0x00, 0xF7});
}

// ---- Preset extraction ----

TEST_CASE ("preset_reply_yields_103_bytes_and_slot")
{
    // R3b §3a: len 113, raw[7] == 06, raw[8] = slot, raw[9..111] = preset
    const auto raw = presetMessage (0x06, 5);
    const auto data = presetData (raw);
    REQUIRE (data.has_value());
    REQUIRE (data->size() == 103);
    CHECK (data->front() == 0);
    CHECK ((*data)[99] == 99);
    CHECK (data->back() == 2);
    CHECK (presetSlot (raw) == 5);
}

TEST_CASE ("edit_buffer_and_pedal_save_yield_preset_data")
{
    // R3b §3a (13 reply) and §3b (113-byte 12 / 0E)
    CHECK (presetData (presetMessage (0x12, 3)).has_value());
    CHECK (presetSlot (presetMessage (0x12, 3)) == 3);
    CHECK (presetData (presetMessage (0x0E, 7)).has_value());
    CHECK (presetSlot (presetMessage (0x0E, 7)) == 7);
}

TEST_CASE ("preset_data_rejects_other_messages")
{
    CHECK_FALSE (presetData (message (17, 0x18)).has_value());
    CHECK_FALSE (presetData (presetMessage (0x07, 5)).has_value());
    CHECK_FALSE (presetSlot (message (17, 0x18)).has_value());

    auto highBit = presetMessage (0x06, 5);
    highBit[50] = 0x80;
    CHECK_FALSE (presetData (highBit).has_value());
}

// ---- IR packing (R3b §3d) ----

TEST_CASE ("pack7_one_group_msb_bit6_is_first_byte")
{
    // R3b §3d: data byte j of group k = raw[25+7k+j] | ((raw[11+k] >> (6-j)) & 1) << 7
    const Bytes data{0x81, 0x02, 0xFF, 0x00, 0x7F, 0x80, 0x10};
    CHECK (pack7 (data) == Bytes{0x52, 0x01, 0x02, 0x7F, 0x00, 0x7F, 0x00, 0x10});
    CHECK (unpack7 (Bytes{0x52, 0x01, 0x02, 0x7F, 0x00, 0x7F, 0x00, 0x10}) == data);
}

TEST_CASE ("pack7_puts_all_msb_bytes_before_data")
{
    // R3b §3d: raw[11..24] = 14 MSB bytes, raw[25..122] = 98 data bytes
    Bytes data (14, 0x00);
    data[0] = 0x80;  // group 0, first byte -> MSB byte 0 bit 6
    data[13] = 0xFF; // group 1, last byte -> MSB byte 1 bit 0
    Bytes expected (16, 0x00);
    expected[0] = 0x40;
    expected[1] = 0x01;
    expected[15] = 0x7F;
    CHECK (pack7 (data) == expected);
}

TEST_CASE ("pack7_full_ir_frame_is_112_bytes")
{
    // R3b §3d: 98 data bytes -> 14 + 98
    const Bytes data (98, 0xAA);
    const auto packed = pack7 (data);
    REQUIRE (packed.has_value());
    CHECK (packed->size() == 112);
}

TEST_CASE ("pack_unpack_rejects_bad_sizes")
{
    CHECK_FALSE (pack7 (Bytes (6, 0)).has_value());
    CHECK_FALSE (unpack7 (Bytes (7, 0)).has_value());
    CHECK_FALSE (unpack7 (Bytes{0x80, 0, 0, 0, 0, 0, 0, 0}).has_value());
    CHECK (pack7 (Bytes{}) == Bytes{});
}

TEST_CASE ("pack_unpack_roundtrip_random")
{
    std::mt19937 rng (12345);
    std::uniform_int_distribution<int> byte (0, 255);
    for (int round = 0; round < 50; ++round)
    {
        Bytes data (98);
        for (auto& b : data)
            b = static_cast<uint8_t> (byte (rng));
        const auto packed = pack7 (data);
        REQUIRE (packed.has_value());
        for (const auto b : *packed)
            REQUIRE (b < 0x80);
        CHECK (unpack7 (*packed) == data);
    }
}

TEST_CASE ("sum14_masks_to_14_bits")
{
    // R3b §3d: checksum = sum of the 98 decoded bytes & 0x3FFF, sent as (c0<<7)|c1
    const Bytes data (98, 0xFF); // 24990 = 0x619E
    CHECK (sum14 (data) == 0x219E);
    CHECK (split14 (0x219E) == std::array<uint8_t, 2>{0x43, 0x1E});
    CHECK (join7 (std::array<uint8_t, 2>{0x43, 0x1E}) == 0x219Eu);
}

TEST_CASE ("sum28_masks_to_28_bits")
{
    // R3b §3d: trailer raw[11..14] = 28-bit total of all decoded bytes
    CHECK (sum28 (Bytes (8192, 0xFF)) == 0x1FE000u);
    CHECK (split28 (0x1FE000) == std::array<uint8_t, 4>{0x00, 0x7F, 0x40, 0x00});
    const Bytes huge (1'052'689, 0xFF); // 268435695 = 0x10000000 + 239
    CHECK (sum28 (huge) == 239u);
}

TEST_CASE ("address21_split_and_join")
{
    // R3b §3d: address (a8<<14)|(a9<<7)|a10, step 98; trailer address 7F 7F 7F
    CHECK (split21 (98) == std::array<uint8_t, 3>{0x00, 0x00, 0x62});
    CHECK (split21 (196) == std::array<uint8_t, 3>{0x00, 0x01, 0x44});
    CHECK (join7 (std::array<uint8_t, 3>{0x7F, 0x7F, 0x7F}) == 0x1FFFFFu);
    CHECK (join7 (std::array<uint8_t, 3>{0x00, 0x01, 0x44}) == 196u);
}

TEST_CASE ("splits_reject_values_that_do_not_fit")
{
    CHECK_FALSE (split14 (0x4000).has_value());
    CHECK_FALSE (split21 (0x200000).has_value());
    CHECK_FALSE (split28 (0x10000000).has_value());
    CHECK_FALSE (join7 (std::array<uint8_t, 2>{0x80, 0x00}).has_value());
}

// ---- Control Change (R3b §3c) ----

TEST_CASE ("cc_bytes")
{
    CHECK (controlChange (1, 11, 99) == Bytes{0xB0, 0x0B, 0x63});
    CHECK (controlChange (16, 127, 127) == Bytes{0xBF, 0x7F, 0x7F});
    CHECK (controlChange (1, 0, 0) == Bytes{0xB0, 0x00, 0x00});
}

TEST_CASE ("cc_rejects_out_of_range")
{
    CHECK_FALSE (controlChange (0, 11, 0).has_value());
    CHECK_FALSE (controlChange (17, 11, 0).has_value());
    CHECK_FALSE (controlChange (1, -1, 0).has_value());
    CHECK_FALSE (controlChange (1, 128, 0).has_value());
    CHECK_FALSE (controlChange (1, 11, -1).has_value());
    CHECK_FALSE (controlChange (1, 11, 128).has_value());
}

TEST_CASE ("select_preset_is_cc60")
{
    // R3b §3c: CC 3C (60) = preset index, channel 1; R4: flat index 0-71
    CHECK (selectPreset (5) == Bytes{0xB0, 0x3C, 0x05});
    CHECK (selectPreset (71) == Bytes{0xB0, 0x3C, 0x47});
    CHECK_FALSE (selectPreset (-1).has_value());
    CHECK_FALSE (selectPreset (72).has_value());
}

// ---- Control Change commands (R7ab §2d) ----

TEST_CASE ("select_preset_command_is_live_edit_cc60")
{
    // R7ab §2a item 5: preset select is LiveEdit, `B0 3C <slot>`
    const auto c = selectPresetCommand (35);
    REQUIRE (c.has_value());
    CHECK (c->name == "selectPreset");
    CHECK (c->kind == CommandKind::LiveEdit);
    CHECK (c->targetSlot == -1);
    CHECK (c->bytes == Bytes{0xB0, 0x3C, 0x23});
    CHECK (selectPresetCommand (0)->bytes == Bytes{0xB0, 0x3C, 0x00});
    CHECK (selectPresetCommand (71)->bytes == Bytes{0xB0, 0x3C, 0x47});
    CHECK_FALSE (selectPresetCommand (-1).has_value());
    CHECK_FALSE (selectPresetCommand (72).has_value());
}

TEST_CASE ("param_change_command_is_live_edit_cc")
{
    // R7ab §2a items 12-18, 23: params 11-58, mic 59, module select 66, IR level 71,
    // patch level 74, module type CC = module index 0-10.
    const auto c = paramChangeCommand (11, 99);
    REQUIRE (c.has_value());
    CHECK (c->name == "paramChange");
    CHECK (c->kind == CommandKind::LiveEdit);
    CHECK (c->targetSlot == -1);
    CHECK (c->bytes == Bytes{0xB0, 0x0B, 0x63});

    for (const int cc : {0, 9, 10, 11, 58, 59, 66, 71, 74})
    {
        const auto ok = paramChangeCommand (cc, 0);
        REQUIRE (ok.has_value());
        CHECK (ok->bytes == Bytes{0xB0, static_cast<uint8_t> (cc), 0x00});
    }
    CHECK (paramChangeCommand (74, 127)->bytes == Bytes{0xB0, 0x4A, 0x7F});
}

TEST_CASE ("param_change_command_rejects_other_ccs_and_values")
{
    // 60 = preset select, 64/67-69 = globals, 70 = IR delete? (R7ab §2a); the rest are unknown.
    for (const int cc : {-1, 60, 61, 64, 65, 67, 68, 69, 70, 72, 73, 75, 127, 128})
        CHECK_FALSE (paramChangeCommand (cc, 0).has_value());

    CHECK_FALSE (paramChangeCommand (11, -1).has_value());
    CHECK_FALSE (paramChangeCommand (11, 128).has_value());
}

TEST_CASE ("global_change_command_is_system_write_cc")
{
    // R7ab §2a items 20-22: master volume 0x40, routing 0x43, USB rec 0x44, USB play 0x45
    const auto c = globalChangeCommand (0x40, 50);
    REQUIRE (c.has_value());
    CHECK (c->name == "globalChange");
    CHECK (c->kind == CommandKind::SystemWrite);
    CHECK (c->targetSlot == -1);
    CHECK (c->bytes == Bytes{0xB0, 0x40, 0x32});

    for (const int cc : {0x43, 0x44, 0x45})
        CHECK (globalChangeCommand (cc, 3)->bytes == Bytes{0xB0, static_cast<uint8_t> (cc), 0x03});
}

TEST_CASE ("global_change_command_rejects_other_ccs_and_values")
{
    for (const int cc : {-1, 0x0B, 0x3C, 0x41, 0x42, 0x46, 0x47, 0x4A, 128})
        CHECK_FALSE (globalChangeCommand (cc, 0).has_value());

    CHECK_FALSE (globalChangeCommand (0x40, -1).has_value());
    CHECK_FALSE (globalChangeCommand (0x40, 128).has_value());
}
