#include "model/Preset.h"

#include <algorithm>

namespace qtn
{
namespace
{
// Symbol tables from UserData::UserData (R4 §1b). Code n is the n-th character.
constexpr std::string_view nameSymbols = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ?!-':&@,.";
constexpr std::string_view irNameSymbols = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ: +.";

constexpr size_t typeOffset = 0x01;
constexpr size_t firstParamOffset = 0x0C;
constexpr size_t blockStride = 7;
constexpr size_t micPositionOffset = 0x43;
constexpr size_t nameLengthOffset = 0x44;
constexpr size_t nameOffset = 0x45;
constexpr size_t irNameLengthOffset = 0x57;
constexpr size_t irNameOffset = 0x58;
constexpr size_t irLevelOffset = 0x62;
constexpr size_t tempoHighOffset = 0x64;
constexpr size_t tempoLowOffset = 0x65;
constexpr size_t patchLevelOffset = 0x66;

static_assert (nameSymbols.size() == 72);
static_assert (irNameSymbols.size() == 40);

template <size_t N>
std::optional<std::string> decodeSymbols (std::string_view table, uint8_t length, const std::array<uint8_t, N>& codes)
{
    if (length > N)
        return std::nullopt;

    std::string text;
    text.reserve (length);
    for (size_t i = 0; i < length; ++i)
    {
        if (codes[i] >= table.size())
            return std::nullopt;
        text.push_back (table[codes[i]]);
    }
    return text;
}

/** Codes for `text`, padded with 0, or std::nullopt when too long or a character is not in `table`. */
template <size_t N>
std::optional<std::array<uint8_t, N>> encodeSymbols (std::string_view table, std::string_view text)
{
    if (text.size() > N)
        return std::nullopt;

    std::array<uint8_t, N> codes{};
    for (size_t i = 0; i < text.size(); ++i)
    {
        const auto pos = table.find (text[i]);
        if (pos == std::string_view::npos)
            return std::nullopt;
        codes[i] = static_cast<uint8_t> (pos);
    }
    return codes;
}

size_t moduleIndex (Module module) noexcept { return static_cast<size_t> (module) - 1; }

bool hasParams (Module module) noexcept
{
    return moduleIndex (module) < Preset::paramModuleCount;
}
} // namespace

std::optional<Preset> Preset::decode (const Bytes& bytes)
{
    if (bytes.size() != size)
        return std::nullopt;

    Preset preset;
    std::copy (bytes.begin(), bytes.end(), preset.raw.begin());

    const auto& raw = preset.raw;
    std::copy_n (raw.begin() + typeOffset, moduleCount, preset.moduleTypes.begin());
    for (size_t m = 0; m < paramModuleCount; ++m)
        std::copy_n (raw.begin() + firstParamOffset + m * blockStride, paramsPerModule, preset.params[m].begin());

    preset.micPosition = raw[micPositionOffset];
    preset.nameLength = raw[nameLengthOffset];
    std::copy_n (raw.begin() + nameOffset, maxNameLength, preset.nameCodes.begin());
    preset.irNameLength = raw[irNameLengthOffset];
    std::copy_n (raw.begin() + irNameOffset, maxIrNameLength, preset.irNameCodes.begin());
    preset.irLevel = raw[irLevelOffset];
    preset.tempoHigh = raw[tempoHighOffset];
    preset.tempoLow = raw[tempoLowOffset];
    preset.patchLevel = raw[patchLevelOffset];
    return preset;
}

Bytes Preset::encode() const
{
    Bytes bytes (raw.begin(), raw.end());

    std::copy (moduleTypes.begin(), moduleTypes.end(), bytes.begin() + typeOffset);
    for (size_t m = 0; m < paramModuleCount; ++m)
        std::copy (params[m].begin(), params[m].end(), bytes.begin() + firstParamOffset + m * blockStride);

    bytes[micPositionOffset] = micPosition;
    bytes[nameLengthOffset] = nameLength;
    std::copy (nameCodes.begin(), nameCodes.end(), bytes.begin() + nameOffset);
    bytes[irNameLengthOffset] = irNameLength;
    std::copy (irNameCodes.begin(), irNameCodes.end(), bytes.begin() + irNameOffset);
    bytes[irLevelOffset] = irLevel;
    bytes[tempoHighOffset] = tempoHigh;
    bytes[tempoLowOffset] = tempoLow;
    bytes[patchLevelOffset] = patchLevel;
    return bytes;
}

uint8_t Preset::type (Module module) const noexcept
{
    const auto index = moduleIndex (module);
    return index < moduleCount ? moduleTypes[index] : uint8_t{};
}

void Preset::setType (Module module, uint8_t value) noexcept
{
    const auto index = moduleIndex (module);
    if (index < moduleCount)
        moduleTypes[index] = value;
}

uint8_t Preset::param (Module module, size_t index) const noexcept
{
    if (! hasParams (module) || index >= paramsPerModule)
        return 0;
    return params[moduleIndex (module)][index];
}

void Preset::setParam (Module module, size_t index, uint8_t value) noexcept
{
    if (hasParams (module) && index < paramsPerModule)
        params[moduleIndex (module)][index] = value;
}

int Preset::tempoBpm() const noexcept
{
    return (tempoHigh << 4) | (tempoLow & 0x0F);
}

void Preset::setTempoBpm (int bpm) noexcept
{
    const auto clamped = std::clamp (bpm, 0, 0xFF);
    tempoHigh = static_cast<uint8_t> (clamped >> 4);
    tempoLow = static_cast<uint8_t> (clamped & 0x0F);
}

std::optional<std::string> Preset::name() const
{
    return decodeSymbols (nameSymbols, nameLength, nameCodes);
}

bool Preset::setName (std::string_view text)
{
    const auto codes = encodeSymbols<maxNameLength> (nameSymbols, text);
    if (! codes.has_value())
        return false;

    nameCodes = *codes;
    nameLength = static_cast<uint8_t> (text.size());
    return true;
}

bool Preset::hasIrName() const noexcept
{
    return irNameLength <= maxIrNameLength;
}

std::optional<std::string> Preset::irName() const
{
    return decodeSymbols (irNameSymbols, irNameLength, irNameCodes);
}

bool Preset::setIrName (std::string_view text)
{
    const auto codes = encodeSymbols<maxIrNameLength> (irNameSymbols, text);
    if (! codes.has_value())
        return false;

    irNameCodes = *codes;
    irNameLength = static_cast<uint8_t> (text.size());
    return true;
}
} // namespace qtn
