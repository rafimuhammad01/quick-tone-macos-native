#pragma once

#include "model/Preset.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

namespace qtn
{
/** One effect parameter (a knob, fader or switch) as listed in effects.json. */
struct ParamInfo
{
    std::string name;
    std::string languageKey;
    int index{};  // 0-5 within the module block
    int offset{}; // preset byte
    int cc{};     // live-edit CC; value = byte value
    int min{};
    int max{};
    int defaultValue{-1}; // -1 = unknown (R4: UNVERIFIED)
    std::string unit;
    std::string control;
};

struct EffectInfo
{
    int id{}; // value of the module's type byte
    std::string name;
    std::vector<ParamInfo> params;
};

struct ModuleInfo
{
    Module module{};
    std::string id; // "CMP", "AMP", ...
    std::vector<EffectInfo> effects;
    std::optional<int> blockOffset;  // byte before param 1; absent for CAB and MIC
    std::optional<int> firstParamCc; // CC of param 1; absent for CAB and MIC
};

/** Where a parameter CC lands in a preset. */
struct CcTarget
{
    Module module{};
    int paramIndex{}; // 0-5
    int offset{};     // preset byte
};

/** The MG-300 effect catalog (app/resources/effects.json, from research/R4-effects.json). */
class EffectCatalog
{
  public:
    /** Parses the catalog JSON; std::nullopt when it is malformed or misses required fields. */
    [[nodiscard]] static std::optional<EffectCatalog> load (const juce::String& json);

    [[nodiscard]] const std::vector<ModuleInfo>& modules() const noexcept { return moduleList; }
    [[nodiscard]] const ModuleInfo* module (Module module) const noexcept;
    [[nodiscard]] const EffectInfo* effect (Module module, int effectId) const noexcept;
    /** The parameter of `effectId` driven by `cc`, or nullptr when that effect has none there. */
    [[nodiscard]] const ParamInfo* param (Module module, int effectId, int cc) const noexcept;
    /** Module and preset byte for a module parameter CC (11-58); std::nullopt for other CCs. */
    [[nodiscard]] std::optional<CcTarget> ccTarget (int cc) const noexcept;
    [[nodiscard]] const ParamInfo& patchLevel() const noexcept { return patchLevelInfo; }

  private:
    std::vector<ModuleInfo> moduleList;
    ParamInfo patchLevelInfo;
};
} // namespace qtn
