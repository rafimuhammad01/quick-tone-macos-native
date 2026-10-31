#pragma once

#include "Bytes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace qtn
{
/** Effect modules of the MG-300 signal chain. The value is both the preset byte that holds the
    module's effect type and the CC that selects it (research/R4-preset-model.md §1, §3). */
enum class Module : uint8_t
{
    cmp = 1,
    efx,
    amp,
    eq,
    nr,
    mod,
    dly,
    rvb,
    cab,
    mic
};

/** One 103-byte MG-300 preset (R4 §1). Named fields hold what the layout explains; a private
    copy of the decoded bytes keeps the unknown ones (00, 0B, 12, 19, 20, 27, 2E, 35, 3C, 63) so
    they survive an encode. encode() starts from that copy and writes the named fields over it,
    so it is the only way to read the bytes and always reflects every edit. */
struct Preset
{
    static constexpr size_t size = 103;
    static constexpr size_t maxNameLength = 18;
    static constexpr size_t maxIrNameLength = 10;
    static constexpr size_t paramsPerModule = 6;
    static constexpr size_t moduleCount = 10;     // CMP .. MIC
    static constexpr size_t paramModuleCount = 8; // CMP .. RVB own a 7-byte block

    /** Bytes 01-0A, index = module number - 1. 0 = bypass (except MIC, which is 1-based). */
    std::array<uint8_t, moduleCount> moduleTypes{};
    /** Bytes 0C-42: for CMP..RVB, params 1-6 at 0x0C + 7 * (module - 1) + index. */
    std::array<std::array<uint8_t, paramsPerModule>, paramModuleCount> params{};
    uint8_t micPosition{};                              // 0x43
    uint8_t nameLength{};                               // 0x44
    std::array<uint8_t, maxNameLength> nameCodes{};     // 0x45-0x56
    uint8_t irNameLength{};                             // 0x57; > 10 = no user IR
    std::array<uint8_t, maxIrNameLength> irNameCodes{}; // 0x58-0x61
    uint8_t irLevel{};                                  // 0x62
    uint8_t tempoHigh{};                                // 0x64, bpm >> 4
    uint8_t tempoLow{};                                 // 0x65, bpm & 0xF
    uint8_t patchLevel{};                               // 0x66

    /** Decodes exactly 103 bytes; any other size gives std::nullopt. */
    [[nodiscard]] static std::optional<Preset> decode (const Bytes& bytes);
    [[nodiscard]] Bytes encode() const;

    [[nodiscard]] uint8_t type (Module module) const noexcept;
    void setType (Module module, uint8_t value) noexcept;

    /** Param `index` (0-5) of CMP..RVB. Out-of-range module/index reads 0 and writes nothing. */
    [[nodiscard]] uint8_t param (Module module, size_t index) const noexcept;
    void setParam (Module module, size_t index, uint8_t value) noexcept;

    [[nodiscard]] int tempoBpm() const noexcept;
    void setTempoBpm (int bpm) noexcept;

    /** The preset name, or std::nullopt when the length exceeds 18 or a code is outside the
        72-symbol table. */
    [[nodiscard]] std::optional<std::string> name() const;
    /** Encodes `text` (at most 18 symbols from the table); unused codes are set to 0 like
        QuickTone does. Returns false and leaves the preset unchanged otherwise. */
    [[nodiscard]] bool setName (std::string_view text);

    /** True when byte 0x57 is 0-10, i.e. the preset names a user IR. */
    [[nodiscard]] bool hasIrName() const noexcept;
    /** The IR name, or std::nullopt when there is none or a code is outside the 40-symbol table. */
    [[nodiscard]] std::optional<std::string> irName() const;
    /** Encodes `text` (at most 10 symbols from the IR table); false leaves the preset unchanged. */
    [[nodiscard]] bool setIrName (std::string_view text);

  private:
    std::array<uint8_t, size> raw{};
};
} // namespace qtn
