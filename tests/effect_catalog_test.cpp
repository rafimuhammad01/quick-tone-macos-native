#include "model/EffectCatalog.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
std::optional<qtn::EffectCatalog> shippedCatalog()
{
    const juce::File file (juce::String (QTN_RESOURCES_DIR) + "/effects.json");
    return qtn::EffectCatalog::load (file.loadFileAsString());
}
} // namespace

TEST_CASE ("catalog_rejects_malformed_json")
{
    CHECK_FALSE (qtn::EffectCatalog::load ("").has_value());
    CHECK_FALSE (qtn::EffectCatalog::load ("{ not json").has_value());
    CHECK_FALSE (qtn::EffectCatalog::load ("{\"modules\": 3}").has_value());
    CHECK_FALSE (qtn::EffectCatalog::load ("[]").has_value());
}

TEST_CASE ("catalog_loads_shipped_modules")
{
    const auto catalog = shippedCatalog();
    REQUIRE (catalog.has_value());

    CHECK (catalog->modules().size() == 10);
    const auto* amp = catalog->module (qtn::Module::amp);
    REQUIRE (amp != nullptr);
    CHECK (amp->id == "AMP");
    CHECK (amp->effects.size() == 27);

    const auto* stageman = catalog->effect (qtn::Module::amp, 40);
    REQUIRE (stageman != nullptr);
    CHECK (stageman->name == "STAGEMAN");
    CHECK (catalog->effect (qtn::Module::amp, 26) == nullptr);

    const auto* userCab = catalog->effect (qtn::Module::cab, 100);
    REQUIRE (userCab != nullptr);
    CHECK (userCab->name == "USER");
    CHECK (userCab->params.empty());
}

TEST_CASE ("catalog_maps_cc_to_param")
{
    const auto catalog = shippedCatalog();
    REQUIRE (catalog.has_value());

    const auto first = catalog->ccTarget (11);
    REQUIRE (first.has_value());
    CHECK (first->module == qtn::Module::cmp);
    CHECK (first->paramIndex == 0);
    CHECK (first->offset == 0x0C);

    const auto amp = catalog->ccTarget (25);
    REQUIRE (amp.has_value());
    CHECK (amp->module == qtn::Module::amp);
    CHECK (amp->paramIndex == 2);
    CHECK (amp->offset == 0x1C);

    const auto last = catalog->ccTarget (58);
    REQUIRE (last.has_value());
    CHECK (last->module == qtn::Module::rvb);
    CHECK (last->paramIndex == 5);
    CHECK (last->offset == 0x42);

    CHECK_FALSE (catalog->ccTarget (10).has_value());
    CHECK_FALSE (catalog->ccTarget (59).has_value());
}

TEST_CASE ("catalog_param_lookup_matches_cc_target")
{
    const auto catalog = shippedCatalog();
    REQUIRE (catalog.has_value());

    // Every listed param's offset agrees with the module CC map.
    for (const auto& module : catalog->modules())
        for (const auto& effect : module.effects)
            for (const auto& param : effect.params)
            {
                const auto target = catalog->ccTarget (param.cc);
                REQUIRE (target.has_value());
                CHECK (target->module == module.module);
                CHECK (target->offset == param.offset);
                CHECK (target->paramIndex == param.index);
            }

    const auto* harmonyKey = catalog->param (qtn::Module::mod, 13, 43);
    REQUIRE (harmonyKey != nullptr);
    CHECK (harmonyKey->max == 9);
    CHECK (catalog->param (qtn::Module::mod, 13, 11) == nullptr);
}

TEST_CASE ("catalog_exposes_patch_level")
{
    const auto catalog = shippedCatalog();
    REQUIRE (catalog.has_value());
    CHECK (catalog->patchLevel().cc == 74);
    CHECK (catalog->patchLevel().offset == 0x66);
    CHECK (catalog->patchLevel().max == 99);
}
