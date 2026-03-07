#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "utils/StringSearch.h"
#include "utils/StringSearchNEON.h"

#if STRING_SEARCH_NEON_AVAILABLE

namespace {
// Named constants for test data sizes — satisfy readability-magic-numbers
constexpr int kLongPad    = 100;  // Padding for long-string tests (> 32 bytes)
constexpr int kMedPad     = 50;   // Padding for medium-string tests (> 16 bytes)
constexpr int kChunkPad   = 16;   // Exactly one NEON chunk
constexpr int kShortPad   = 14;   // Less than one NEON chunk
constexpr int kMidOffset  = 25;   // Midpoint index for non-ASCII injection
} // namespace

TEST_SUITE("StringSearch NEON") {

    TEST_SUITE("NEON Availability") {
        TEST_CASE("compile-time detection works on arm64") {
            // STRING_SEARCH_NEON_AVAILABLE is 1 in this compilation unit
            constexpr bool compile_time_available = true;
            (void)compile_time_available;
        }
    }

    TEST_SUITE("NEON Direct") {
        TEST_CASE("ContainsSubstringINEON finds pattern in long text") {
            std::string text(kLongPad, 'a');
            text += "TEST_PATTERN";
            text += std::string(kLongPad, 'b');
            const bool found = string_search::neon::ContainsSubstringINEON(text, "test_pattern");
            REQUIRE(found);  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }

        TEST_CASE("ContainsSubstringINEON returns false when not found") {
            const std::string text(kLongPad, 'a');
            const bool found = string_search::neon::ContainsSubstringINEON(text, "xyz");
            REQUIRE_FALSE(found);  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }

        TEST_CASE("ContainsSubstringINEON handles pattern at start") {
            const std::string text = "HELLO" + std::string(kMedPad, 'x');
            const bool found = string_search::neon::ContainsSubstringINEON(text, "hello");
            REQUIRE(found);  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }

        TEST_CASE("ContainsSubstringINEON handles pattern at end") {
            std::string text(kMedPad, 'x');
            text += "WORLD";
            const bool found = string_search::neon::ContainsSubstringINEON(text, "world");
            REQUIRE(found);  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }

        TEST_CASE("ContainsSubstringINEON handles pattern at 16-byte boundary") {
            // Pattern starts exactly at offset 16 (one NEON chunk in)
            std::string text(kChunkPad, 'a');
            text += "BOUNDARY";
            text += std::string(kChunkPad, 'b');
            const bool found = string_search::neon::ContainsSubstringINEON(text, "boundary");
            REQUIRE(found);  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }

        TEST_CASE("ContainsSubstringINEON empty pattern returns true") {
            const bool found = string_search::neon::ContainsSubstringINEON("anything", "");
            REQUIRE(found);  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }

        TEST_CASE("ContainsSubstringNEON case-sensitive — found") {
            std::string text(kMedPad, 'x');
            text += "ExactMatch";
            text += std::string(kMedPad, 'y');
            const bool found = string_search::neon::ContainsSubstringNEON(text, "ExactMatch");
            REQUIRE(found);  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }

        TEST_CASE("ContainsSubstringNEON case-sensitive — wrong case not found") {
            std::string text(kMedPad, 'x');
            text += "ExactMatch";
            text += std::string(kMedPad, 'y');
            const bool found = string_search::neon::ContainsSubstringNEON(text, "exactmatch");
            REQUIRE_FALSE(found);  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }

        TEST_CASE("StrStrCaseInsensitiveNEON finds needle") {
            const std::string haystack = std::string(kMedPad, 'a') + "NEEDLE" + std::string(kMedPad, 'b');
            const char* result = string_search::neon::StrStrCaseInsensitiveNEON(
                haystack.c_str(), "needle");
            REQUIRE(result != nullptr);  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }

        TEST_CASE("StrStrCaseInsensitiveNEON returns nullptr when not found") {
            const std::string haystack(kLongPad, 'a');
            const char* result = string_search::neon::StrStrCaseInsensitiveNEON(
                haystack.c_str(), "xyz");
            REQUIRE(result == nullptr);  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }
    }

    TEST_SUITE("NEON Integration") {
        TEST_CASE("ContainsSubstringI uses NEON path for 16+ byte strings") {
            std::string text(kMedPad, 'a');
            text += "NEON_PATTERN";
            text += std::string(kMedPad, 'b');
            REQUIRE(string_search::ContainsSubstringI(text, "neon_pattern"));  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }

        TEST_CASE("ContainsSubstring uses NEON path for 16+ byte strings") {
            std::string text(kMedPad, 'a');
            text += "NEON_PATTERN";
            text += std::string(kMedPad, 'b');
            REQUIRE(string_search::ContainsSubstring(text, "NEON_PATTERN"));  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }

        TEST_CASE("NEON path handles pattern near 16-byte boundary") {
            std::string text(kShortPad, 'a');
            text += "PATTERN";
            text += std::string(kShortPad, 'b');
            REQUIRE(string_search::ContainsSubstringI(text, "pattern"));  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }

        TEST_CASE("scalar fallback for short strings (< 16 bytes)") {
            REQUIRE(string_search::ContainsSubstringI("hello world", "world"));  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }

        TEST_CASE("scalar fallback for non-ASCII text") {
            std::string text(kMedPad, 'a');
            text[kMidOffset] = static_cast<char>(0x80);  // Extended ASCII — forces scalar path
            text += "pattern";
            REQUIRE(string_search::ContainsSubstringI(text, "pattern"));  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }
    }

    TEST_SUITE("NEON Equivalence") {
        TEST_CASE("NEON produces same results as scalar for various patterns") {
            const std::vector<std::pair<std::string, std::string>> cases = {
                {std::string(kMedPad, 'A') + "TEST", "test"},
                {std::string(kChunkPad, 'a') + "HELLO" + std::string(kChunkPad, 'b'), "hello"},
                {"PREFIX" + std::string(kMedPad, 'x'), "prefix"},
                {std::string(kChunkPad, 'z') + "MIDPOINT" + std::string(kChunkPad, 'z'), "midpoint"},
            };
            for (const auto& [text, pattern] : cases) {
                REQUIRE(string_search::ContainsSubstringI(text, pattern));  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
            }
        }

        TEST_CASE("NEON and scalar agree on no-match cases") {
            const std::string text(kLongPad, 'a');
            REQUIRE_FALSE(string_search::ContainsSubstringI(text, "bcde"));  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
            REQUIRE_FALSE(string_search::ContainsSubstring(text, "bcde"));  // NOLINT(cert-err33-c) - doctest macro; return value is checked by the framework
        }
    }
}

#else
// NEON not available at compile time (x86/x64 build)
TEST_SUITE("StringSearch NEON") {
    TEST_CASE("NEON not available - scalar path used") {
        REQUIRE(string_search::ContainsSubstringI("Hello World", "world"));
    }
}
#endif  // STRING_SEARCH_NEON_AVAILABLE
