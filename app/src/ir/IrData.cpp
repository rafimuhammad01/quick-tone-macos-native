#include "ir/IrData.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace qtn
{
static_assert (std::endian::native == std::endian::little, "IR images are float32 little-endian");

std::optional<IrData> IrData::fromDeviceBytes (std::span<const uint8_t> bytes)
{
    if (bytes.size() != byteCount)
        return std::nullopt;

    IrData ir;
    std::copy (bytes.begin(), bytes.end(), ir.bytes.begin());
    return ir;
}

IrData IrData::fromDeviceSamples (std::span<const float, sampleCount> samples)
{
    IrData ir;
    std::memcpy (ir.bytes.data(), samples.data(), byteCount);
    return ir;
}

Bytes IrData::toDeviceBytes() const { return Bytes (bytes.begin(), bytes.end()); }

float IrData::deviceSample (size_t index) const noexcept
{
    float value = 0.0f;
    if (index < sampleCount)
        std::memcpy (&value, bytes.data() + 4 * index, sizeof (value));
    return value;
}

std::vector<float> IrData::timeOrderSamples() const
{
    // Export reads the image back to front (R7cd §1, ghidra:100032550).
    std::vector<float> out (sampleCount);
    for (size_t i = 0; i < sampleCount; ++i)
        out[i] = deviceSample (sampleCount - 1 - i);
    return out;
}

std::optional<std::string> IrData::invalidReason() const
{
    if (std::all_of (bytes.begin(), bytes.end(), [] (uint8_t b)
                     { return b == 0xFF; }))
        return "IR image is erased (all 0xFF = NaN samples); it must not be used or uploaded";

    for (size_t i = 0; i < sampleCount; ++i)
        if (! std::isfinite (deviceSample (i)))
            return "IR sample " + std::to_string (i) + " is NaN or infinite";

    return std::nullopt;
}
} // namespace qtn
