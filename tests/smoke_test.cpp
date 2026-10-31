#include "Version.h"
#include "support/TempDir.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("smoke_core_links")
{
    REQUIRE (qtn::versionString() == "0.1.0");
}

TEST_CASE ("temp_dir_name_carries_a_fresh_uuid")
{
    // Test processes run in parallel (ctest -j): a name probed on disk ("first free name") can be
    // picked by two processes at once, so each folder name carries its own UUID instead.
    const qtn::test::TempDir a;
    const qtn::test::TempDir b;

    const auto uuidOf = [] (const juce::File& f)
    { return juce::Uuid (f.getFileName().fromFirstOccurrenceOf ("qtn-test-", false, false)); };

    CHECK (a.get().isDirectory());
    CHECK (a.get().getParentDirectory() == juce::File::getSpecialLocation (juce::File::tempDirectory));
    CHECK_FALSE (uuidOf (a.get()).isNull());
    CHECK_FALSE (uuidOf (b.get()).isNull());
    CHECK (uuidOf (a.get()) != uuidOf (b.get()));
}
