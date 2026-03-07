/**
 * @file CoverageAllSourcesMain.cpp
 * @brief Dummy main function for comprehensive coverage test executable
 *
 * This file exists solely to allow compilation of all source files
 * into a test executable for coverage reporting purposes.
 *
 * Files compiled into this executable will appear in coverage reports
 * with 0% coverage if not executed by other tests, which helps identify
 * untested code.
 *
 * This executable does not need to run - it's only compiled for coverage.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

// This test suite exists to ensure the executable compiles
// The actual purpose is to include all source files for coverage reporting
TEST_SUITE("CoverageAllSources") {
    TEST_CASE("Dummy test for compilation") {
        // This test always passes
        // Its purpose is to ensure the executable can be built
        CHECK(true);
    }
}

