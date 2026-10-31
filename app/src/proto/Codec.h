#pragma once

#include "Bytes.h"
#include "proto/Command.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

/** Byte-level codec for the NUX NME-300 (first-gen MG-300) SysEx protocol: the base
    `NuxMidiTransmission` path documented in research/R3b-nme300-framing.md. Pure functions:
    no sending, no timers. Offsets count from F0 = 0. */
namespace qtn::proto
{
inline constexpr int presetCount = 72;        // R3b §5
inline constexpr int presetSize = 103;        // R3b §5
inline constexpr int presetReplyLength = 113; // R3b §3a

/** `F0 00 11 22 66 00 70 <cmd> <args...> F7` (R3b §2). Args must be 7-bit. */
[[nodiscard]] Bytes frame (uint8_t cmd, const Bytes& args);

/** The one frame with message type 00 instead of 70: `F0 00 11 22 66 00 00 00 00 F7` (R3b §2). */
[[nodiscard]] Bytes versionRequestFrame();

struct Frame
{
    uint8_t cmd = 0;
    Bytes args;
};

/** Accepts `F0 00 11 22 66 00 <type> <cmd> <args...> F7` with type 00/10/70 and all data
    bytes < 0x80. Returns raw[7] as cmd and raw[8..n-2] as args. */
[[nodiscard]] std::optional<Frame> parse (const Bytes& raw);

/** What an incoming message is, by total length + raw[7], as the original dispatches it
    (R3b §3a check functions, §3b handleIncomingMidiMessage). */
enum class ReplyKind
{
    Version,            // 48 bytes, raw[6] == 10
    GlobalParams,       // 17 bytes, raw[7] == 18
    Preset,             // 113 bytes, raw[7] == 06 (reply to 07)
    EditBuffer,         // 113 bytes, raw[7] == 12 (reply to 13, or preset changed on the pedal)
    ControlChange,      // 3 bytes, B0 cc vv (knob turned on the pedal)
    Tempo,              // 12 bytes
    ModuleChange,       // 19 bytes
    AmpCabChange,       // 21 bytes
    PresetSavedOnPedal, // 113 bytes, raw[7] == 0E
    IrData,             // 128 bytes, raw[7] == 58
    IrDownloadDone      // 128 bytes, raw[7] == 50, address 7F 7F 7F
};

[[nodiscard]] std::optional<ReplyKind> classify (const Bytes& raw);

// Read-side request builders (R3b §4). Names are the original's reply-check names without "Check".
[[nodiscard]] Command getVersion();
[[nodiscard]] Command getGlobalParameters();
/** Empty for a slot outside 0-71. */
[[nodiscard]] std::optional<Command> getOnePreset (int slot);
[[nodiscard]] Command getCurrentPreset();

/** The 103 preset bytes raw[9..111] of a 113-byte preset message (Preset, EditBuffer or
    PresetSavedOnPedal). */
[[nodiscard]] std::optional<Bytes> presetData (const Bytes& raw);
/** raw[8] of the same messages: slot (Preset, PresetSavedOnPedal) or current index (EditBuffer). */
[[nodiscard]] std::optional<int> presetSlot (const Bytes& raw);

// IR transfer helpers (R3b §3d). No IR command builders yet.

/** 8->7 packing: input size must be a multiple of 7. Output = one MSB byte per group of 7,
    then the low 7 bits of every input byte. In each MSB byte, bit 6 belongs to the group's first byte. */
[[nodiscard]] std::optional<Bytes> pack7 (std::span<const uint8_t> data);
/** Inverse of pack7: input size must be a multiple of 8 and all bytes < 0x80. */
[[nodiscard]] std::optional<Bytes> unpack7 (std::span<const uint8_t> packed);

/** Sum of the bytes & 0x3FFF (IR data frame checksum). */
[[nodiscard]] uint32_t sum14 (std::span<const uint8_t> data) noexcept;
/** Sum of the bytes & 0x0FFFFFFF (IR trailer total). */
[[nodiscard]] uint32_t sum28 (std::span<const uint8_t> data) noexcept;

/** Big-endian 7-bit splits: empty if the value does not fit. */
[[nodiscard]] std::optional<std::array<uint8_t, 2>> split14 (uint32_t value) noexcept;
[[nodiscard]] std::optional<std::array<uint8_t, 3>> split21 (uint32_t value) noexcept;
[[nodiscard]] std::optional<std::array<uint8_t, 4>> split28 (uint32_t value) noexcept;
/** Inverse of the splits: empty if any byte is >= 0x80. */
[[nodiscard]] std::optional<uint32_t> join7 (std::span<const uint8_t> bytes) noexcept;

// Control Change (R3b §3c)

/** `Bn cc vv` with channel 1-16; empty if channel, cc or value is out of range. */
[[nodiscard]] std::optional<Bytes> controlChange (int channel, int cc, int value);
/** `B0 3C <slot>`: CC 60 on channel 1 (R3b §3c, R4); empty for a slot outside 0-71.
    Plain bytes; selectPresetCommand wraps them for DeviceSession::submit. */
[[nodiscard]] std::optional<Bytes> selectPreset (int slot);

// Control Change commands for DeviceSession::submit (R7ab §2a/§2d). All on channel 1.

/** "selectPreset", LiveEdit: `B0 3C <slot>`; empty for a slot outside 0-71 (R7ab item 5). */
[[nodiscard]] std::optional<Command> selectPresetCommand (int slot);
/** "paramChange", LiveEdit: `B0 <cc> <value>` for the edit-buffer CCs of R7ab items 12-18 and 23:
    module type 0-10, params 11-58, mic 59, module select 66, IR level 71, patch level 74.
    Empty for any other CC or a value outside 0-127. */
[[nodiscard]] std::optional<Command> paramChangeCommand (int cc, int value);
/** "globalChange", SystemWrite: `B0 <cc> <value>` for the global CCs of R7ab items 20-22:
    master volume 0x40, routing 0x43, USB rec 0x44, USB play 0x45. Empty otherwise. */
[[nodiscard]] std::optional<Command> globalChangeCommand (int cc, int value);
} // namespace qtn::proto
