#include "Version.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("smoke_core_links")
{
    REQUIRE (qtn::versionString() == "0.1.0");
}
