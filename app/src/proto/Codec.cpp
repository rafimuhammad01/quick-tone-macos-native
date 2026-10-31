#include "proto/Codec.h"

#include <algorithm>
#include <string>
#include <utility>

namespace qtn::proto
{
namespace
{
constexpr uint8_t sysexStart = 0xF0;
constexpr uint8_t sysexEnd = 0xF7;

// Bytes 1-5: `00 11 22` manufacturer id, `66` family, `00` (R3b §2).
constexpr std::array<uint8_t, 5> prefix{0x00, 0x11, 0x22, 0x66, 0x00};

constexpr uint8_t typeVersionRequest = 0x00; // R3b §2
constexpr uint8_t typeVersionReply = 0x10;   // R3b §2
constexpr uint8_t typeCommand = 0x70;        // R3b §2

constexpr size_t headerLength = 8; // F0 + prefix + type + cmd
constexpr size_t minFrameLength = headerLength + 1;

constexpr uint8_t ccPresetSelect = 0x3C; // R3b §3c

// Edit-buffer CCs (R7ab §2a items 12-18, 23): module type = module index 0-10, params 11-58,
// mic 59 (0x3B), module select 66 (0x42), IR level 71 (0x47), patch level 74 (0x4A).
bool isEditBufferCc (int cc) noexcept
{
    return (cc >= 0 && cc <= 59) || cc == 0x42 || cc == 0x47 || cc == 0x4A;
}

// Global CCs (R7ab §2a items 20-22): master volume 0x40, routing 0x43, USB rec 0x44, USB play 0x45.
bool isGlobalCc (int cc) noexcept
{
    return cc == 0x40 || (cc >= 0x43 && cc <= 0x45);
}

std::optional<Command> ccCommand (std::string name, CommandKind kind, std::optional<Bytes> bytes)
{
    if (! bytes.has_value())
        return std::nullopt;
    return Command{std::move (name), kind, -1, std::move (*bytes)};
}

bool isSevenBit (uint8_t b) noexcept { return b < 0x80; }

bool isSysex (const Bytes& raw) noexcept
{
    return raw.size() >= 2 && raw.front() == sysexStart && raw.back() == sysexEnd;
}

bool isValidSlot (int slot) noexcept { return slot >= 0 && slot < presetCount; }

Bytes header (uint8_t type, uint8_t cmd)
{
    Bytes raw{sysexStart};
    raw.insert (raw.end(), prefix.begin(), prefix.end());
    raw.push_back (type);
    raw.push_back (cmd);
    return raw;
}

template <size_t N>
std::optional<std::array<uint8_t, N>> split7 (uint32_t value) noexcept
{
    if (value >= (uint32_t{1} << (7 * N)))
        return std::nullopt;

    std::array<uint8_t, N> out{};
    for (size_t i = 0; i < N; ++i)
        out[N - 1 - i] = static_cast<uint8_t> ((value >> (7 * i)) & 0x7F);
    return out;
}

uint32_t sum (std::span<const uint8_t> data) noexcept
{
    uint32_t total = 0;
    for (const auto b : data)
        total += b;
    return total;
}
} // namespace

Bytes frame (uint8_t cmd, const Bytes& args)
{
    if (! isSevenBit (cmd) || ! std::all_of (args.begin(), args.end(), isSevenBit))
        return {};

    auto raw = header (typeCommand, cmd);
    raw.insert (raw.end(), args.begin(), args.end());
    raw.push_back (sysexEnd);
    return raw;
}

Bytes versionRequestFrame()
{
    auto raw = header (typeVersionRequest, 0x00);
    raw.push_back (0x00);
    raw.push_back (sysexEnd);
    return raw;
}

std::optional<Frame> parse (const Bytes& raw)
{
    if (raw.size() < minFrameLength || ! isSysex (raw))
        return std::nullopt;

    if (! std::equal (prefix.begin(), prefix.end(), raw.begin() + 1))
        return std::nullopt;

    // UNVERIFIED (R3b §2): only the version reply's type byte (10) is checked in the original;
    // other replies are assumed to use 70 or 10.
    const auto type = raw[6];
    if (type != typeVersionRequest && type != typeVersionReply && type != typeCommand)
        return std::nullopt;

    if (! std::all_of (raw.begin() + 1, raw.end() - 1, isSevenBit))
        return std::nullopt;

    return Frame{raw[7], Bytes (raw.begin() + headerLength, raw.end() - 1)};
}

std::optional<ReplyKind> classify (const Bytes& raw)
{
    // Knob turned on the pedal: plain 3-byte CC (R3b §3b, L114-118).
    if (raw.size() == 3)
    {
        if ((raw[0] & 0xF0) == 0xB0)
            return ReplyKind::ControlChange;
        return std::nullopt;
    }

    // Like the original, only total length and raw[6]/raw[7] are tested; bytes 1-5 are not
    // (R3b §2: "never by bytes 1-6").
    if (! isSysex (raw) || raw.size() < minFrameLength)
        return std::nullopt;

    const auto cmd = raw[7];
    switch (raw.size())
    {
    case 48: // R3b §3a getVersionCheck
        if (raw[6] == typeVersionReply)
            return ReplyKind::Version;
        break;
    case 17: // R3b §3a getGlobleParameterCheck
        if (cmd == 0x18)
            return ReplyKind::GlobalParams;
        break;
    case 12: // R3b §3b L246-261
        return ReplyKind::Tempo;
    case 19: // R3b §3b L263-319
        return ReplyKind::ModuleChange;
    case 21: // R3b §3b L320-345
        return ReplyKind::AmpCabChange;
    case presetReplyLength: // R3b §3a getOnePresetCheck / getCurrentPresetCheck, §3b L347-392
        if (cmd == 0x06)
            return ReplyKind::Preset;
        if (cmd == 0x12)
            return ReplyKind::EditBuffer;
        if (cmd == 0x0E)
            return ReplyKind::PresetSavedOnPedal;
        break;
    case 128: // R3b §3b L130-237
        if (cmd == 0x58)
            return ReplyKind::IrData;
        if (cmd == 0x50 && raw[8] == 0x7F && raw[9] == 0x7F && raw[10] == 0x7F)
            return ReplyKind::IrDownloadDone;
        break;
    default:
        break;
    }
    return std::nullopt;
}

Command getVersion()
{
    return {"getVersion", CommandKind::Handshake, -1, versionRequestFrame()};
}

Command getGlobalParameters()
{
    // Allowlist name keeps the original's spelling (getGlobleParameterCheck).
    return {"getGlobleParameter", CommandKind::Read, -1, frame (0x19, {0x00})};
}

std::optional<Command> getOnePreset (int slot)
{
    if (! isValidSlot (slot))
        return std::nullopt;
    return Command{"getOnePreset", CommandKind::Read, -1, frame (0x07, {static_cast<uint8_t> (slot)})};
}

Command getCurrentPreset()
{
    return {"getCurrentPreset", CommandKind::Read, -1, frame (0x13, {0x00})};
}

std::optional<Bytes> presetData (const Bytes& raw)
{
    const auto kind = classify (raw);
    if (kind != ReplyKind::Preset && kind != ReplyKind::EditBuffer && kind != ReplyKind::PresetSavedOnPedal)
        return std::nullopt;

    // UNVERIFIED (R3b §2): preset bytes are assumed 7-bit on the wire; reject anything else.
    const auto first = raw.begin() + 9;
    const auto last = first + presetSize;
    if (! std::all_of (first, last, isSevenBit))
        return std::nullopt;

    return Bytes (first, last);
}

std::optional<int> presetSlot (const Bytes& raw)
{
    const auto kind = classify (raw);
    if (kind != ReplyKind::Preset && kind != ReplyKind::EditBuffer && kind != ReplyKind::PresetSavedOnPedal)
        return std::nullopt;

    // UNVERIFIED (R3b §4): getOnePresetCheck stores raw[8] without comparing it to the request.
    return raw[8];
}

std::optional<Bytes> pack7 (std::span<const uint8_t> data)
{
    if (data.size() % 7 != 0)
        return std::nullopt;

    const auto groups = data.size() / 7;
    Bytes out (groups + data.size(), 0x00);
    for (size_t k = 0; k < groups; ++k)
    {
        for (size_t j = 0; j < 7; ++j)
        {
            const auto b = data[7 * k + j];
            out[k] = static_cast<uint8_t> (out[k] | ((b >> 7) << (6 - j)));
            out[groups + 7 * k + j] = static_cast<uint8_t> (b & 0x7F);
        }
    }
    return out;
}

std::optional<Bytes> unpack7 (std::span<const uint8_t> packed)
{
    if (packed.size() % 8 != 0 || ! std::all_of (packed.begin(), packed.end(), isSevenBit))
        return std::nullopt;

    const auto groups = packed.size() / 8;
    Bytes out (7 * groups, 0x00);
    for (size_t k = 0; k < groups; ++k)
        for (size_t j = 0; j < 7; ++j)
            out[7 * k + j] = static_cast<uint8_t> (packed[groups + 7 * k + j] | (((packed[k] >> (6 - j)) & 1) << 7));
    return out;
}

uint32_t sum14 (std::span<const uint8_t> data) noexcept { return sum (data) & 0x3FFF; }

uint32_t sum28 (std::span<const uint8_t> data) noexcept { return sum (data) & 0x0FFFFFFF; }

std::optional<std::array<uint8_t, 2>> split14 (uint32_t value) noexcept { return split7<2> (value); }

std::optional<std::array<uint8_t, 3>> split21 (uint32_t value) noexcept { return split7<3> (value); }

// UNVERIFIED (R3b §3d): byte order of the 28-bit trailer total is not stated; big-endian like
// the 14-bit checksum and the 21-bit address.
std::optional<std::array<uint8_t, 4>> split28 (uint32_t value) noexcept { return split7<4> (value); }

std::optional<uint32_t> join7 (std::span<const uint8_t> bytes) noexcept
{
    if (bytes.size() > 4)
        return std::nullopt;

    uint32_t value = 0;
    for (const auto b : bytes)
    {
        if (! isSevenBit (b))
            return std::nullopt;
        value = (value << 7) | b;
    }
    return value;
}

std::optional<Bytes> controlChange (int channel, int cc, int value)
{
    if (channel < 1 || channel > 16 || cc < 0 || cc > 127 || value < 0 || value > 127)
        return std::nullopt;

    return Bytes{static_cast<uint8_t> (0xB0 | (channel - 1)), static_cast<uint8_t> (cc), static_cast<uint8_t> (value)};
}

std::optional<Bytes> selectPreset (int slot)
{
    if (! isValidSlot (slot))
        return std::nullopt;
    return controlChange (1, ccPresetSelect, slot);
}

std::optional<Command> selectPresetCommand (int slot)
{
    return ccCommand ("selectPreset", CommandKind::LiveEdit, selectPreset (slot));
}

std::optional<Command> paramChangeCommand (int cc, int value)
{
    if (! isEditBufferCc (cc))
        return std::nullopt;
    return ccCommand ("paramChange", CommandKind::LiveEdit, controlChange (1, cc, value));
}

std::optional<Command> globalChangeCommand (int cc, int value)
{
    if (! isGlobalCc (cc))
        return std::nullopt;
    return ccCommand ("globalChange", CommandKind::SystemWrite, controlChange (1, cc, value));
}
} // namespace qtn::proto
