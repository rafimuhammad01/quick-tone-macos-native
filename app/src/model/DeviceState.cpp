#include "model/DeviceState.h"

#include <utility>

namespace qtn
{
namespace
{
bool isValidSlot (int slot) noexcept { return slot >= 0 && slot < DeviceState::presetCount; }
} // namespace

std::optional<Preset> DeviceState::preset (int slot) const
{
    if (! isValidSlot (slot))
        return std::nullopt;
    return presets[static_cast<size_t> (slot)];
}

void DeviceState::setPreset (int slot, const Preset& newPreset)
{
    if (! isValidSlot (slot))
        return;
    presets[static_cast<size_t> (slot)] = newPreset;
    changed();
}

const Bytes& DeviceState::globalParams() const noexcept { return globals; }

void DeviceState::setGlobalParams (Bytes reply)
{
    globals = std::move (reply);
    changed();
}

const std::optional<Preset>& DeviceState::editBuffer() const noexcept { return edit; }

void DeviceState::setEditBuffer (const Preset& newPreset, int currentSlot)
{
    edit = newPreset;
    current = currentSlot;
    changed();
}

int DeviceState::currentSlot() const noexcept { return current; }

void DeviceState::clear()
{
    presets = {};
    globals.clear();
    edit.reset();
    current = -1;
    changed();
}

void DeviceState::changed() const
{
    if (onChange)
        onChange();
}
} // namespace qtn
