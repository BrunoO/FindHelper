#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "doctest/doctest.h"

// Enable test mode for non-Windows platforms
#ifndef _WIN32
#ifndef USN_WINDOWS_TESTS
#define USN_WINDOWS_TESTS
#endif  // USN_WINDOWS_TESTS
#endif  // _WIN32

#include <sstream>

#include "TestHelpers.h"
#include "search/SearchContext.h"
#include "search/SearchPatternUtils.h"
#include "utils/Logger.h"

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Aggregated doctest suite with many cases
TEST_SUITE("SearchContext - ValidateAndClamp") {  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - doctest macro expands to a global test suite object with required name
  
  TEST_CASE("ValidateAndClamp - valid values unchanged") {
    constexpr int kValidChunkSize = 1000;
    constexpr int kValidHybridPercent = 75;
    const SearchContext ctx = test_helpers::CreateValidatedSearchContext(kValidChunkSize, kValidHybridPercent);
    
    CHECK(ctx.dynamic_chunk_size == kValidChunkSize);
    CHECK(ctx.hybrid_initial_percent == kValidHybridPercent);
  }
  
  TEST_CASE("ValidateAndClamp - clamps dynamic_chunk_size too small") {
    constexpr int kBelowMinChunkSize = 50;
    constexpr int kMinChunkSize = 100;
    constexpr int kHybridPercent = 75;
    const SearchContext ctx = test_helpers::CreateValidatedSearchContext(kBelowMinChunkSize, kHybridPercent);  // 50 is below minimum (100)
    
    CHECK(ctx.dynamic_chunk_size == kMinChunkSize);
    CHECK(ctx.hybrid_initial_percent == kHybridPercent);
  }
  
  TEST_CASE("ValidateAndClamp - clamps dynamic_chunk_size too large") {
    constexpr int kAboveMaxChunkSize = 200000;
    constexpr int kMaxChunkSize = 100000;
    constexpr int kHybridPercent = 75;
    const SearchContext ctx = test_helpers::CreateValidatedSearchContext(kAboveMaxChunkSize, kHybridPercent);  // 200000 is above maximum (100000)
    
    CHECK(ctx.dynamic_chunk_size == kMaxChunkSize);
    CHECK(ctx.hybrid_initial_percent == kHybridPercent);
  }
  
  TEST_CASE("ValidateAndClamp - clamps hybrid_initial_percent too small") {
    constexpr int kChunkSize = 1000;
    constexpr int kBelowMinHybrid = 30;
    constexpr int kMinHybrid = 50;
    const SearchContext ctx = test_helpers::CreateValidatedSearchContext(kChunkSize, kBelowMinHybrid);  // 30 is below minimum (50)
    
    CHECK(ctx.dynamic_chunk_size == kChunkSize);
    CHECK(ctx.hybrid_initial_percent == kMinHybrid);
  }
  
  TEST_CASE("ValidateAndClamp - clamps hybrid_initial_percent too large") {
    constexpr int kChunkSize = 1000;
    constexpr int kAboveMaxHybrid = 99;
    constexpr int kMaxHybrid = 95;
    const SearchContext ctx = test_helpers::CreateValidatedSearchContext(kChunkSize, kAboveMaxHybrid);  // 99 is above maximum (95)
    
    CHECK(ctx.dynamic_chunk_size == kChunkSize);
    CHECK(ctx.hybrid_initial_percent == kMaxHybrid);
  }
  
  TEST_CASE("ValidateAndClamp - clamps both values") {
    constexpr int kBelowMinChunk = 50;
    constexpr int kAboveMaxHybrid = 99;
    constexpr int kMinChunk = 100;
    constexpr int kMaxHybrid = 95;
    const SearchContext ctx = test_helpers::CreateValidatedSearchContext(kBelowMinChunk, kAboveMaxHybrid);  // 50 too small, 99 too large
    
    CHECK(ctx.dynamic_chunk_size == kMinChunk);
    CHECK(ctx.hybrid_initial_percent == kMaxHybrid);
  }
  
  TEST_CASE("ValidateAndClamp - boundary values") {
    SearchContext ctx;
    
    // Test minimum boundary
    ctx.dynamic_chunk_size = 100;
    ctx.hybrid_initial_percent = 50;
    ctx.ValidateAndClamp();
    CHECK(ctx.dynamic_chunk_size == 100);
    CHECK(ctx.hybrid_initial_percent == 50);
    
    // Test maximum boundary
    ctx.dynamic_chunk_size = 100000;
    ctx.hybrid_initial_percent = 95;
    ctx.ValidateAndClamp();
    CHECK(ctx.dynamic_chunk_size == 100000);
    CHECK(ctx.hybrid_initial_percent == 95);
    
    // Test just below minimum
    ctx.dynamic_chunk_size = 99;
    ctx.hybrid_initial_percent = 49;
    ctx.ValidateAndClamp();
    CHECK(ctx.dynamic_chunk_size == 100);
    CHECK(ctx.hybrid_initial_percent == 50);
    
    // Test just above maximum
    ctx.dynamic_chunk_size = 100001;
    ctx.hybrid_initial_percent = 96;
    ctx.ValidateAndClamp();
    CHECK(ctx.dynamic_chunk_size == 100000);
    CHECK(ctx.hybrid_initial_percent == 95);
  }
  
  TEST_CASE("ValidateAndClamp - zero values") {
    SearchContext ctx = test_helpers::CreateValidatedSearchContext(0, 0);
    
    CHECK(ctx.dynamic_chunk_size == 100);
    CHECK(ctx.hybrid_initial_percent == 50);
  }
}

TEST_SUITE("SearchContext - PrepareExtensionSet") {  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - doctest macro expands to a global test suite object with required name
  
  TEST_CASE("PrepareExtensionSet - empty extensions") {
    SearchContext ctx;
    ctx.extensions.clear();
    ctx.case_sensitive = false;
    
    ctx.PrepareExtensionSet();
    
    CHECK(ctx.extension_set.empty());
  }
  
  TEST_CASE("PrepareExtensionSet - case-insensitive lowercase") {
    SearchContext ctx = test_helpers::CreateSearchContextWithExtensions({".txt", ".cpp", ".h"}, false);
    
    CHECK(ctx.extension_set.find("txt") != ctx.extension_set.end());
    CHECK(ctx.extension_set.find("cpp") != ctx.extension_set.end());
    CHECK(ctx.extension_set.find("h") != ctx.extension_set.end());
  }
  
  TEST_CASE("PrepareExtensionSet - case-insensitive uppercase") {
    SearchContext ctx = test_helpers::CreateSearchContextWithExtensions({".TXT", ".CPP", ".H"}, false);
    
    // Should be converted to lowercase
    CHECK(ctx.extension_set.find("txt") != ctx.extension_set.end());
    CHECK(ctx.extension_set.find("cpp") != ctx.extension_set.end());
    CHECK(ctx.extension_set.find("h") != ctx.extension_set.end());
  }
  
  TEST_CASE("PrepareExtensionSet - case-sensitive") {
    SearchContext ctx = test_helpers::CreateSearchContextWithExtensions({".txt", ".TXT"}, true);
    
    // Case-sensitive should preserve case
    CHECK(ctx.extension_set.find("txt") != ctx.extension_set.end());
    CHECK(ctx.extension_set.find("TXT") != ctx.extension_set.end());
  }
  
  TEST_CASE("PrepareExtensionSet - removes leading dot") {
    SearchContext ctx = test_helpers::CreateSearchContextWithExtensions({".txt", "cpp"}, false);  // Mix of with and without dot
    
    // Both should be stored without leading dot
    CHECK(ctx.extension_set.find("txt") != ctx.extension_set.end());
    CHECK(ctx.extension_set.find("cpp") != ctx.extension_set.end());
  }
}

TEST_SUITE("SearchContext - ExtensionMatches (extension filter)") {  // NOLINT(readability-identifier-naming,cppcoreguidelines-avoid-non-const-global-variables) - doctest macro expands to a global test suite object with required name

  TEST_CASE("ExtensionMatches - case-insensitive matches") {
    SearchContext ctx = test_helpers::CreateSearchContextWithExtensions({".cpp", ".h", ".txt"}, false);

    CHECK(search_pattern_utils::ExtensionMatches("cpp", ctx.extension_set, ctx.case_sensitive));
    CHECK(search_pattern_utils::ExtensionMatches("CPP", ctx.extension_set, ctx.case_sensitive));
    CHECK(search_pattern_utils::ExtensionMatches("txt", ctx.extension_set, ctx.case_sensitive));
    CHECK_FALSE(search_pattern_utils::ExtensionMatches("java", ctx.extension_set, ctx.case_sensitive));
  }

  TEST_CASE("ExtensionMatches - case-sensitive matches") {
    SearchContext ctx = test_helpers::CreateSearchContextWithExtensions({".cpp", ".h"}, true);

    CHECK(search_pattern_utils::ExtensionMatches("cpp", ctx.extension_set, ctx.case_sensitive));
    CHECK(search_pattern_utils::ExtensionMatches("h", ctx.extension_set, ctx.case_sensitive));
    CHECK_FALSE(search_pattern_utils::ExtensionMatches("CPP", ctx.extension_set, ctx.case_sensitive));
  }

  TEST_CASE("ExtensionMatches - empty set") {
    SearchContext ctx = test_helpers::CreateSearchContextWithExtensions({}, false);

    CHECK_FALSE(search_pattern_utils::ExtensionMatches("cpp", ctx.extension_set, ctx.case_sensitive));
  }
}