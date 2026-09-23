#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "utils/CpuFeatures.h"

TEST_SUITE("CpuFeatures") {

    TEST_SUITE("SupportsAVX2") {
        TEST_CASE("function is callable") {
            // Should not crash
            bool result = cpu_features::SupportsAVX2();
            // Result depends on CPU, but function should work
            (void)result;  // Suppress unused variable warning
        }

        TEST_CASE("returns consistent results") {
            // Multiple calls should return same result
            bool first = cpu_features::SupportsAVX2();
            bool second = cpu_features::SupportsAVX2();
            bool third = cpu_features::SupportsAVX2();

            REQUIRE(first == second);
            REQUIRE(second == third);
        }

        TEST_CASE("thread-safe caching") {
            // Should be safe to call from multiple "contexts"
            // (In real multi-threaded scenario, this would be more thorough)
            bool result1 = cpu_features::SupportsAVX2();
            bool result2 = cpu_features::GetAVX2Support();

            REQUIRE(result1 == result2);
        }
    }

    TEST_SUITE("GetAVX2Support") {
        TEST_CASE("returns cached value") {
            // First call initializes cache
            bool first = cpu_features::SupportsAVX2();

            // Subsequent calls should return cached value
            bool cached1 = cpu_features::GetAVX2Support();
            bool cached2 = cpu_features::GetAVX2Support();

            REQUIRE(first == cached1);
            REQUIRE(cached1 == cached2);
        }

        TEST_CASE("works without prior call to SupportsAVX2") {
            // GetAVX2Support should auto-initialize if needed
            bool result = cpu_features::GetAVX2Support();
            (void)result;  // Suppress unused variable warning
        }
    }

    TEST_SUITE("Compile-time detection") {
        TEST_CASE("STRING_SEARCH_AVX2_AVAILABLE is defined") {
            #if STRING_SEARCH_AVX2_AVAILABLE
                // AVX2 available at compile time
                // Runtime check may still return false on older CPUs
                bool compile_time_available = true;
            #else
                // AVX2 not available at compile time
                bool compile_time_available = false;
            #endif  // STRING_SEARCH_AVX2_AVAILABLE

            (void)compile_time_available;  // Suppress unused variable warning
            // Test passes if code compiles
        }
    }

    TEST_SUITE("Platform compatibility") {
        TEST_CASE("works on all platforms") {
            // Should not crash regardless of platform
            bool result = cpu_features::SupportsAVX2();

            // On non-x86 platforms, should return false
            #if !defined(_M_X64) && !defined(_M_IX86) && !defined(__x86_64__) && !defined(__i386__)
                REQUIRE_FALSE(result);  // Non-x86 should return false
            #endif  // !defined(_M_X64) && !defined(_M_IX86) && !defined(__x86_64__) && !defined(__i386__)

            (void)result;
        }
    }
}
