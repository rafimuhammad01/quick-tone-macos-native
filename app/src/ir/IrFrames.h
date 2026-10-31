#pragma once

#include "Bytes.h"
#include "ir/IrData.h"
#include "proto/Command.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

/** IR transfer frames of the NME-300 (R7cd §3): built like IR_DataList::ChangeWaveData2Cabinet,
    checked like NuxMidiTransmission::handleIncomingMidiMessage. Bytes only: nothing is sent here
    and nothing is added to the allowlist. */
namespace qtn::IrFrames
{
inline constexpr size_t frameSize = 128;
inline constexpr size_t bytesPerDataFrame = 98; // 14 groups of 7
inline constexpr size_t dataFrameCount = 84;    // ceil(8192 / 98)
inline constexpr size_t uploadFrameCount = 87;  // start + 84 data + trailer + end
inline constexpr int firstSlot = 0;
inline constexpr int lastSlot = 35; // R7cd §8 Q6: our app restricts IR slots to the user presets

/** Name of every upload frame: the original's cab-popup upload (`importIR_Check` acks it). */
inline constexpr const char* uploadName = "importIR";

/** Transfer total T of the trailer (28 bits): per frame, after each 7-byte group the running
    sum of the frame's raw bytes so far is added (R7cd §3, asm@0x10005f0d7). */
[[nodiscard]] uint32_t transferTotal (std::span<const uint8_t> image) noexcept;

/** The 87 upload frames for `ir` into preset slot `slot`, in sending order, each a 128-byte
    sysex with kind IRWrite and targetSlot = slot. Empty for a slot outside 0-35 or an IR that
    is not valid (NaN/Inf; R7cd §8 Q9: never upload one). */
[[nodiscard]] std::optional<std::vector<Command>> encodeUpload (const IrData& ir, int slot);

/** Rebuilds the image from a download (`59` reply): 84 data frames `58` (address 98k, counter k,
    14-bit sum of the raw bytes) and the trailer `58 7F 7F 7F` (total T, count 84). Start/end
    frames `50` are skipped. Every check failing gives an IrError. */
[[nodiscard]] std::variant<IrData, IrError> decodeDownload (std::span<const Bytes> frames);
} // namespace qtn::IrFrames
