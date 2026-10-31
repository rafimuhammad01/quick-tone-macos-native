#pragma once

#include "Bytes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qtn
{
/** Why an IR could not be converted, decoded or used. QuickTone shows one hint for every
    import error: "IR Sampling Rate Error" (R7cd §2 step 8); `reason` is the detail. */
struct IrError
{
    static constexpr std::string_view hint = "IR Sampling Rate Error";
    std::string reason;
};

/** One user IR as the pedal stores it (R7cd §1): 2048 float32 little-endian samples,
    48 kHz mono, time-reversed (last sample first) = 8192 bytes. The bytes are kept exactly as
    given, so an erased image (all 0xFF = NaN) survives a round trip unchanged. */
class IrData
{
  public:
    static constexpr size_t sampleCount = 2048;
    static constexpr size_t byteCount = sampleCount * 4;

    /** All-zero image. */
    IrData() = default;

    /** The 8192-byte wire / file image; std::nullopt for any other size. */
    [[nodiscard]] static std::optional<IrData> fromDeviceBytes (std::span<const uint8_t> bytes);
    /** An image from samples in device order (already reversed). */
    [[nodiscard]] static IrData fromDeviceSamples (std::span<const float, sampleCount> samples);

    [[nodiscard]] Bytes toDeviceBytes() const;
    /** Sample `index` in device order (index 0 = the IR's last sample). */
    [[nodiscard]] float deviceSample (size_t index) const noexcept;
    /** The samples in playing order (reversed back), as QuickTone's WAV export reads them. */
    [[nodiscard]] std::vector<float> timeOrderSamples() const;

    /** Empty when every sample is finite, else why the image must not be used or uploaded. */
    [[nodiscard]] std::optional<std::string> invalidReason() const;
    [[nodiscard]] bool isValid() const { return ! invalidReason().has_value(); }

    friend bool operator== (const IrData&, const IrData&) = default;

  private:
    std::array<uint8_t, byteCount> bytes{};
};
} // namespace qtn
