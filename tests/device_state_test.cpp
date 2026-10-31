#include "model/DeviceState.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
qtn::Preset presetFilledWith (uint8_t value)
{
    return *qtn::Preset::decode (qtn::Bytes (qtn::Preset::size, value));
}
} // namespace

TEST_CASE ("device_state_starts_empty")
{
    const qtn::DeviceState state;
    CHECK_FALSE (state.preset (0).has_value());
    CHECK_FALSE (state.preset (71).has_value());
    CHECK_FALSE (state.editBuffer().has_value());
    CHECK (state.globalParams().empty());
    CHECK (state.currentSlot() == -1);
}

TEST_CASE ("device_state_set_preset_stores_and_notifies")
{
    qtn::DeviceState state;
    int changes = 0;
    state.onChange = [&]
    { ++changes; };

    state.setPreset (71, presetFilledWith (0x11));
    REQUIRE (state.preset (71).has_value());
    CHECK (state.preset (71)->encode() == qtn::Bytes (qtn::Preset::size, 0x11));
    CHECK (changes == 1);
}

TEST_CASE ("device_state_ignores_slot_out_of_range")
{
    qtn::DeviceState state;
    int changes = 0;
    state.onChange = [&]
    { ++changes; };

    state.setPreset (-1, presetFilledWith (1));
    state.setPreset (72, presetFilledWith (1));
    CHECK_FALSE (state.preset (-1).has_value());
    CHECK_FALSE (state.preset (72).has_value());
    CHECK (changes == 0);
}

TEST_CASE ("device_state_edit_buffer_sets_current_slot")
{
    qtn::DeviceState state;
    state.setEditBuffer (presetFilledWith (2), 9);
    REQUIRE (state.editBuffer().has_value());
    CHECK (state.editBuffer()->encode() == qtn::Bytes (qtn::Preset::size, 2));
    CHECK (state.currentSlot() == 9);
}

TEST_CASE ("device_state_clear_resets_and_notifies")
{
    qtn::DeviceState state;
    state.setPreset (0, presetFilledWith (1));
    state.setGlobalParams ({0xF0, 0x01, 0xF7});
    state.setEditBuffer (presetFilledWith (2), 3);
    int changes = 0;
    state.onChange = [&]
    { ++changes; };

    state.clear();
    CHECK_FALSE (state.preset (0).has_value());
    CHECK (state.globalParams().empty());
    CHECK_FALSE (state.editBuffer().has_value());
    CHECK (state.currentSlot() == -1);
    CHECK (changes == 1);
}

TEST_CASE ("device_state_edit_buffer_slot_out_of_range_is_unknown")
{
    qtn::DeviceState state;
    state.setEditBuffer (presetFilledWith (2), 71);
    CHECK (state.currentSlot() == 71);

    state.setEditBuffer (presetFilledWith (3), 72);
    CHECK (state.editBuffer().has_value());
    CHECK (state.currentSlot() == -1);

    state.setEditBuffer (presetFilledWith (3), -5);
    CHECK (state.currentSlot() == -1);
}
