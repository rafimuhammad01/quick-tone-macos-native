#include "model/EffectCatalog.h"

#include <algorithm>

namespace qtn
{
namespace
{
std::optional<int> intField (const juce::var& object, const char* key)
{
    const auto& value = object[key];
    if (! (value.isInt() || value.isInt64() || value.isDouble()))
        return std::nullopt;
    return static_cast<int> (value);
}

std::optional<std::string> stringField (const juce::var& object, const char* key)
{
    const auto& value = object[key];
    if (! value.isString())
        return std::nullopt;
    return value.toString().toStdString();
}

std::string optionalString (const juce::var& object, const char* key)
{
    return stringField (object, key).value_or (std::string{});
}

/** Reads offset/cc/min/max (required) and default (optional). */
std::optional<ParamInfo> parseNumericParam (const juce::var& json)
{
    const auto offset = intField (json, "offset");
    const auto cc = intField (json, "cc");
    const auto min = intField (json, "min");
    const auto max = intField (json, "max");
    if (! offset || ! cc || ! min || ! max || *offset < 0 || *offset >= static_cast<int> (Preset::size))
        return std::nullopt;

    ParamInfo param;
    param.offset = *offset;
    param.cc = *cc;
    param.min = *min;
    param.max = *max;
    param.defaultValue = intField (json, "default").value_or (-1);
    return param;
}

std::optional<ParamInfo> parseParam (const juce::var& json)
{
    auto param = parseNumericParam (json);
    const auto name = stringField (json, "name");
    const auto index = intField (json, "index");
    if (! param || ! name || ! index || *index < 0 || *index >= static_cast<int> (Preset::paramsPerModule))
        return std::nullopt;

    param->name = *name;
    param->index = *index;
    param->languageKey = optionalString (json, "languageKey");
    param->unit = optionalString (json, "unit");
    param->control = optionalString (json, "control");
    return param;
}

std::optional<EffectInfo> parseEffect (const juce::var& json)
{
    const auto id = intField (json, "id");
    const auto name = stringField (json, "name");
    const auto* params = json["params"].getArray();
    if (! id || ! name || params == nullptr)
        return std::nullopt;

    EffectInfo effect{*id, *name, {}};
    for (const auto& p : *params)
    {
        auto param = parseParam (p);
        if (! param)
            return std::nullopt;
        effect.params.push_back (std::move (*param));
    }
    return effect;
}

std::optional<ModuleInfo> parseModule (const juce::var& json)
{
    const auto id = stringField (json, "id");
    const auto slot = intField (json, "slotIndex");
    const auto* effects = json["effects"].getArray();
    if (! id || ! slot || *slot < 1 || *slot > static_cast<int> (Preset::moduleCount) || effects == nullptr)
        return std::nullopt;

    ModuleInfo module;
    module.module = static_cast<Module> (*slot);
    module.id = *id;
    module.blockOffset = intField (json, "blockOffset");
    if (const auto* ccs = json["paramCCs"].getArray(); ccs != nullptr && ! ccs->isEmpty())
    {
        const auto& first = ccs->getReference (0);
        if (first.isInt() || first.isInt64() || first.isDouble())
            module.firstParamCc = static_cast<int> (first);
    }

    for (const auto& e : *effects)
    {
        auto effect = parseEffect (e);
        if (! effect)
            return std::nullopt;
        module.effects.push_back (std::move (*effect));
    }
    return module;
}
} // namespace

std::optional<EffectCatalog> EffectCatalog::load (const juce::String& json)
{
    juce::var root;
    if (json.isEmpty() || juce::JSON::parse (json, root).failed() || ! root.isObject())
        return std::nullopt;

    const auto* modules = root["modules"].getArray();
    auto patchLevel = parseNumericParam (root["patchLevel"]);
    if (modules == nullptr || ! patchLevel)
        return std::nullopt;

    EffectCatalog catalog;
    patchLevel->name = "PATCH LEVEL";
    catalog.patchLevelInfo = *patchLevel;
    for (const auto& m : *modules)
    {
        auto module = parseModule (m);
        if (! module)
            return std::nullopt;
        catalog.moduleList.push_back (std::move (*module));
    }
    return catalog;
}

const ModuleInfo* EffectCatalog::module (Module wanted) const noexcept
{
    const auto it = std::find_if (moduleList.begin(), moduleList.end(), [wanted] (const ModuleInfo& m)
                                  { return m.module == wanted; });
    return it != moduleList.end() ? &*it : nullptr;
}

const EffectInfo* EffectCatalog::effect (Module wanted, int effectId) const noexcept
{
    const auto* info = module (wanted);
    if (info == nullptr)
        return nullptr;

    const auto it = std::find_if (info->effects.begin(), info->effects.end(), [effectId] (const EffectInfo& e)
                                  { return e.id == effectId; });
    return it != info->effects.end() ? &*it : nullptr;
}

const ParamInfo* EffectCatalog::param (Module wanted, int effectId, int cc) const noexcept
{
    const auto* info = effect (wanted, effectId);
    if (info == nullptr)
        return nullptr;

    const auto it = std::find_if (info->params.begin(), info->params.end(), [cc] (const ParamInfo& p)
                                  { return p.cc == cc; });
    return it != info->params.end() ? &*it : nullptr;
}

std::optional<CcTarget> EffectCatalog::ccTarget (int cc) const noexcept
{
    for (const auto& m : moduleList)
    {
        if (! m.firstParamCc || ! m.blockOffset)
            continue;

        const auto index = cc - *m.firstParamCc;
        if (index >= 0 && index < static_cast<int> (Preset::paramsPerModule))
            return CcTarget{m.module, index, *m.blockOffset + 1 + index};
    }
    return std::nullopt;
}
} // namespace qtn
