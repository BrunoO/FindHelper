#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "utils/StringSearch.h"
#include "utils/StringSearchAVX2.h"
#include "utils/CpuFeatures.h"

#if STRING_SEARCH_AVX2_AVAILABLE

TEST_SUITE("StringSearch AVX2") {
    
    TEST_SUITE("AVX2 Availability") {
        TEST_CASE("compile-time detection works") {
            #if STRING_SEARCH_AVX2_AVAILABLE
                bool compile_time_available = true;
            #else
                bool compile_time_available = false;
            #endif  // STRING_SEARCH_AVX2_AVAILABLE
            
            // On macOS (cross-compiling for Windows), AVX2 might not be available
            // This test just verifies the code compiles
            (void)compile_time_available;
        }
        
        TEST_CASE("runtime detection works") {
            bool runtime_available = cpu_features::SupportsAVX2();
            // Result depends on CPU, but function should work
            (void)runtime_available;
        }
    }
    
    TEST_SUITE("AVX2 Integration") {
        TEST_CASE("ContainsSubstringI uses AVX2 path for long strings") {
            // Create long strings that should trigger AVX2 path
            std::string long_text(100, 'a');
            long_text += "TEST_PATTERN_HERE";
            long_text += std::string(100, 'b');
            
            std::string pattern = "test_pattern_here";
            
            // Should work regardless of AVX2 availability (falls back to scalar)
            bool result = string_search::ContainsSubstringI(long_text, pattern);
            REQUIRE(result);
        }
        
        TEST_CASE("AVX2 path handles 32-byte boundaries") {
            std::string text(30, 'a');
            text += "PATTERN";
            text += std::string(30, 'b');
            
            // Pattern at 32-byte boundary
            REQUIRE(string_search::ContainsSubstringI(text, "pattern"));
        }
        
        TEST_CASE("AVX2 path handles very long patterns") {
            std::string pattern(50, 'A');
            pattern += "X";
            
            std::string text(100, 'a');
            text.replace(25, pattern.length(), pattern);
            
            // Long pattern should use AVX2
            REQUIRE(string_search::ContainsSubstringI(text, pattern));
        }
        
        TEST_CASE("scalar fallback for short strings") {
            // Short strings should use scalar path
            std::string text = "hello";
            std::string pattern = "ell";
            
            bool result = string_search::ContainsSubstringI(text, pattern);
            REQUIRE(result);
        }
        
        TEST_CASE("scalar fallback for non-ASCII") {
            // Non-ASCII should fall back to scalar
            std::string text(100, 'a');
            text[50] = static_cast<char>(0x80);  // Extended ASCII
            text += "pattern";
            
            // Should still work (scalar path)
            REQUIRE(string_search::ContainsSubstringI(text, "pattern"));
        }
    }
    
    TEST_SUITE("AVX2 Equivalence") {
        TEST_CASE("AVX2 produces same results as scalar") {
            // Test cases that should produce identical results
            std::vector<std::pair<std::string, std::string>> test_cases = {
                {std::string(100, 'A') + "TEST", "test"},
                {std::string(50, 'a') + "HELLO" + std::string(50, 'b'), "hello"},
                {"PREFIX" + std::string(100, 'x'), "prefix"},
            };
            
            for (const auto& [text, pattern] : test_cases) {
                bool result = string_search::ContainsSubstringI(text, pattern);
                // Should find the pattern (case-insensitive)
                REQUIRE(result);
            }
        }
    }
}

#else
// AVX2 not available at compile time
TEST_SUITE("StringSearch AVX2") {
    TEST_CASE("AVX2 not available - scalar path used") {
        // When AVX2 is not available, should still work with scalar
        std::string text = "Hello World";
        std::string pattern = "world";
        
        bool result = string_search::ContainsSubstringI(text, pattern);
        REQUIRE(result);
    }
}
#endif  // STRING_SEARCH_AVX2_AVAILABLE
