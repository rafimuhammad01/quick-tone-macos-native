#include "model/Preset.h"

#include <catch2/catch_test_macros.hpp>

#include <numeric>

namespace
{
qtn::Bytes countingBytes (size_t size)
{
    qtn::Bytes bytes (size);
    std::iota (bytes.begin(), bytes.end(), uint8_t{});
    return bytes;
}
} // namespace

TEST_CASE ("decode_rejects_wrong_size")
{
    CHECK_FALSE (qtn::Preset::decode (qtn::Bytes (102)).has_value());
    CHECK_FALSE (qtn::Preset::decode (qtn::Bytes (104)).has_value());
    CHECK_FALSE (qtn::Preset::decode (qtn::Bytes{}).has_value());
    CHECK (qtn::Preset::decode (qtn::Bytes (103)).has_value());
}

TEST_CASE ("decode_reads_named_fields")
{
    const auto preset = qtn::Preset::decode (countingBytes (qtn::Preset::size));
    REQUIRE (preset.has_value());

    CHECK (preset->type (qtn::Module::cmp) == 0x01);
    CHECK (preset->type (qtn::Module::cab) == 0x09);
    CHECK (preset->type (qtn::Module::mic) == 0x0A);
    CHECK (preset->param (qtn::Module::cmp, 0) == 0x0C);
    CHECK (preset->param (qtn::Module::cmp, 5) == 0x11);
    CHECK (preset->param (qtn::Module::efx, 0) == 0x13);
    CHECK (preset->param (qtn::Module::rvb, 5) == 0x42);
    CHECK (preset->micPosition == 0x43);
    CHECK (preset->nameLength == 0x44);
    CHECK (preset->nameCodes.front() == 0x45);
    CHECK (preset->nameCodes.back() == 0x56);
    CHECK (preset->irNameLength == 0x57);
    CHECK (preset->irNameCodes.front() == 0x58);
    CHECK (preset->irNameCodes.back() == 0x61);
    CHECK (preset->irLevel == 0x62);
    CHECK (preset->tempoHigh == 0x64);
    CHECK (preset->tempoLow == 0x65);
    CHECK (preset->patchLevel == 0x66);
}

TEST_CASE ("encode_decode_roundtrip_preserves_unknown_bytes")
{
    const auto original = countingBytes (qtn::Preset::size);
    const auto preset = qtn::Preset::decode (original);
    REQUIRE (preset.has_value());
    CHECK (preset->encode() == original);
}

TEST_CASE ("encode_writes_named_fields_over_raw_bytes")
{
    auto preset = qtn::Preset::decode (countingBytes (qtn::Preset::size));
    REQUIRE (preset.has_value());

    preset->setType (qtn::Module::amp, 40);
    preset->setParam (qtn::Module::dly, 2, 77);
    preset->patchLevel = 99;
    const auto bytes = preset->encode();

    CHECK (bytes[0x03] == 40);
    CHECK (bytes[0x38] == 77);
    CHECK (bytes[0x66] == 99);
    CHECK (bytes[0x00] == 0x00); // unknown byte untouched
    CHECK (bytes[0x63] == 0x63); // unknown byte untouched
}

TEST_CASE ("tempo_combines_nibbles")
{
    auto preset = qtn::Preset::decode (qtn::Bytes (qtn::Preset::size));
    REQUIRE (preset.has_value());
    preset->tempoHigh = 7;
    preset->tempoLow = 8;
    CHECK (preset->tempoBpm() == 120);

    preset->setTempoBpm (240);
    CHECK (preset->tempoHigh == 15);
    CHECK (preset->tempoLow == 0);
}

TEST_CASE ("name_roundtrip_max_length")
{
    auto preset = qtn::Preset::decode (qtn::Bytes (qtn::Preset::size));
    REQUIRE (preset.has_value());

    const std::string longest = "Az09 ?!-':&@,.xyzQ";
    REQUIRE (longest.size() == 18);
    REQUIRE (preset->setName (longest));
    CHECK (preset->name() == longest);
    CHECK (preset->nameLength == 18);

    const auto again = qtn::Preset::decode (preset->encode());
    REQUIRE (again.has_value());
    CHECK (again->name() == longest);
}

TEST_CASE ("name_rejects_too_long")
{
    auto preset = qtn::Preset::decode (qtn::Bytes (qtn::Preset::size));
    REQUIRE (preset.has_value());
    CHECK_FALSE (preset->setName ("ABCDEFGHIJKLMNOPQRS"));
    CHECK (preset->nameLength == 0);
}

TEST_CASE ("name_rejects_unencodable_char")
{
    auto preset = qtn::Preset::decode (qtn::Bytes (qtn::Preset::size));
    REQUIRE (preset.has_value());
    REQUIRE (preset->setName ("Keep"));

    CHECK_FALSE (preset->setName ("Semi;colon"));
    CHECK_FALSE (preset->setName ("caf\xC3\xA9"));
    CHECK (preset->name() == "Keep");
}

TEST_CASE ("name_decode_fails_on_code_outside_table")
{
    auto preset = qtn::Preset::decode (qtn::Bytes (qtn::Preset::size));
    REQUIRE (preset.has_value());
    preset->nameLength = 2;
    preset->nameCodes[0] = 0;
    preset->nameCodes[1] = 72;
    CHECK_FALSE (preset->name().has_value());

    preset->nameCodes[1] = 71;
    CHECK (preset->name() == "A.");

    preset->nameLength = 19;
    CHECK_FALSE (preset->name().has_value());
}

TEST_CASE ("ir_name_roundtrip_max_length")
{
    auto preset = qtn::Preset::decode (qtn::Bytes (qtn::Preset::size));
    REQUIRE (preset.has_value());

    const std::string longest = "CT BRITV:+";
    REQUIRE (preset->setIrName (longest));
    CHECK (preset->irName() == longest);
    CHECK (preset->irNameLength == 10);
    CHECK (preset->irNameCodes[0] == 12); // C
    CHECK (preset->irNameCodes[2] == 37); // space

    CHECK_FALSE (preset->setIrName ("ABCDEFGHIJK"));
    CHECK_FALSE (preset->setIrName ("lower"));
    CHECK (preset->irName() == longest);
}

TEST_CASE ("ir_name_absent_when_length_over_ten")
{
    auto preset = qtn::Preset::decode (qtn::Bytes (qtn::Preset::size));
    REQUIRE (preset.has_value());
    preset->irNameLength = 16;
    CHECK_FALSE (preset->hasIrName());
    CHECK_FALSE (preset->irName().has_value());

    preset->irNameLength = 1;
    preset->irNameCodes[0] = 40; // outside the 40-symbol table
    CHECK (preset->hasIrName());
    CHECK_FALSE (preset->irName().has_value());
}
