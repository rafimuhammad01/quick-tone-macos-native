#pragma once

#include "Bytes.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace qtn
{
/** The pedal's answer to the version request (R3b §3a getVersionCheck). */
struct DeviceInfo
{
    /** raw[7..38] (32 characters, up to the first 00), trimmed. Passed to UserData::setVersion.
        Verified by research/captures/001-connect.txt: "V01_20191106". */
    juce::String firmware;
    /** raw[39..46] (8 characters, up to the first 00), trimmed. Passed to UserData::setName.
        Verified by research/captures/001-connect.txt: "NUX_MG1X" (resolves R3b open question 2). */
    juce::String name;
    /** The whole 48-byte reply. */
    Bytes raw;
};

/** Parses a 48-byte version reply (`raw[6] == 10`); empty for any other message. */
[[nodiscard]] std::optional<DeviceInfo> parseVersionReply (const Bytes& raw);
} // namespace qtn
