#include "device/DeviceInfo.h"

#include "proto/Codec.h"

#include <algorithm>

namespace qtn
{
namespace
{
// R3b §3a getVersionCheck: raw[7..38] -> setVersion, raw[39..46] -> setName.
constexpr size_t versionOffset = 7;
constexpr size_t versionLength = 32;
constexpr size_t nameOffset = versionOffset + versionLength;
constexpr size_t nameLength = 8;

/** The characters of raw[offset .. offset+length) up to the first 00, trimmed. */
juce::String text (const Bytes& raw, size_t offset, size_t length)
{
    const auto first = raw.begin() + static_cast<std::ptrdiff_t> (offset);
    const auto last = std::find (first, first + static_cast<std::ptrdiff_t> (length), uint8_t{0});
    return juce::String::fromUTF8 (reinterpret_cast<const char*> (&*first), static_cast<int> (last - first)).trim();
}
} // namespace

std::optional<DeviceInfo> parseVersionReply (const Bytes& raw)
{
    if (proto::classify (raw) != proto::ReplyKind::Version)
        return std::nullopt;

    return DeviceInfo{text (raw, versionOffset, versionLength), text (raw, nameOffset, nameLength), raw};
}
} // namespace qtn
