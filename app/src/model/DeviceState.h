#pragma once

#include "Bytes.h"
#include "model/Preset.h"

#include <array>
#include <functional>
#include <optional>

namespace qtn
{
/** What the app knows about the connected pedal: the 72 stored presets, the global-parameters
    reply, the edit buffer and the current preset slot (R3b §5). Single-threaded (message
    thread). `onChange` fires once after every change. */
class DeviceState
{
  public:
    static constexpr int presetCount = 72;

    /** Empty for a slot outside 0-71 or a slot not read yet. */
    [[nodiscard]] std::optional<Preset> preset (int slot) const;
    /** Ignored for a slot outside 0-71. */
    void setPreset (int slot, const Preset& preset);

    /** The whole 17-byte global-parameters reply (R3b §3a); empty until read. */
    [[nodiscard]] const Bytes& globalParams() const noexcept;
    void setGlobalParams (Bytes reply);

    [[nodiscard]] const std::optional<Preset>& editBuffer() const noexcept;
    /** The edit buffer and the slot it came from (raw[8] of the `12` reply). */
    void setEditBuffer (const Preset& preset, int currentSlot);

    /** -1 until known. */
    [[nodiscard]] int currentSlot() const noexcept;

    /** Forgets everything (new connection). */
    void clear();

    std::function<void()> onChange;

  private:
    void changed() const;

    std::array<std::optional<Preset>, presetCount> presets{};
    Bytes globals;
    std::optional<Preset> edit;
    int current = -1;
};
} // namespace qtn
