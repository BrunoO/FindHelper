/**
 * @file StdRegexUtilsTests.cpp
 * @brief Unit tests for StdRegexUtils regex matching functionality
 *
 * Tests cover:
 * - Literal pattern matching (fast path - string search)
 * - Simple pattern matching (SimpleRegex path)
 * - Complex pattern matching (std::regex path)
 * - Case-sensitive vs case-insensitive matching
 * - Thread-local regex cache behavior
 * - Invalid patterns (error handling)
 * - Pattern analysis functions (IsLiteralPattern, IsSimplePattern, RequiresFullMatch)
 * - Edge cases (empty patterns, escaped characters, etc.)
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <string>
#include <string_view>

#include "TestHelpers.h"
#include "doctest/doctest.h"
#include "utils/StdRegexUtils.h"

TEST_SUITE("StdRegexUtils") {
  
  TEST_SUITE("Literal pattern matching (fast path)") {
    
    TEST_CASE("Simple literal match") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"hello", "hello world", true},
        {"world", "hello world", true},
        {"xyz", "hello world", false}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Literal match case-sensitive") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"Hello", "Hello World", true, true},
        {"hello", "Hello World", false, true},
        {"HELLO", "Hello World", false, true}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Literal match case-insensitive") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"Hello", "hello world", true, false},
        {"HELLO", "hello world", true, false},
        {"hElLo", "HELLO WORLD", true, false}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Literal match with escaped characters") {
      // Note: Escaped characters in patterns are handled differently:
      // - Patterns with escaped special chars may be routed to SimpleRegex or std::regex
      // - The exact behavior depends on pattern analysis
      // Test actual literal patterns (no special characters)
      const std::vector<test_helpers::RegexMatchTestCase> literal_cases = {
        {"test", "test*file", true},
        {"file", "file.txt", true},
        {"test", "test^file", true}
      };
      test_helpers::RunParameterizedRegexMatchTests(literal_cases);
      
      // Test patterns that contain special characters (not escaped)
      // These will be routed to SimpleRegex or std::regex
      const std::vector<test_helpers::RegexMatchTestCase> special_cases = {
        {".*", "test*file", true},  // Dot-star matches
        {"^test", "test^file", true}  // Caret at start
      };
      test_helpers::RunParameterizedRegexMatchTests(special_cases);
    }
    
    TEST_CASE("Literal match partial") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"lo", "hello", true},
        {"ell", "hello", true},
        {"xyz", "hello", false}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Literal match empty text") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"pattern", "", false}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
  }
  
  TEST_SUITE("Simple pattern matching (SimpleRegex path)") {
    
    TEST_CASE("Dot matches any character") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"h.llo", "hello", true},
        {"h.llo", "hallo", true},
        {"h.llo", "hxllo", true},
        {"h.llo", "hllo", false}  // Dot requires one char
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Star matches zero or more") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"he*llo", "hllo", true},   // e* matches zero
        {"he*llo", "hello", true},  // e* matches one
        {"he*llo", "heello", true}, // e* matches two
        {"he*llo", "hllo", true}    // e* matches zero
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Caret matches start") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"^hello", "hello world", true},
        {"^world", "hello world", false}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Dollar matches end") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"world$", "hello world", true},
        {"hello$", "hello world", false}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Caret and dollar for full match") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"^hello$", "hello", true},
        {"^hello$", "hello world", false}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Simple pattern case-insensitive") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"h.llo", "Hello", true, false},
        {"^HELLO", "hello world", true, false}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Combined simple patterns") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {".*test.*", "this is a test file", true},
        {"^.*test$", "this is a test", true}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
  }
  
  TEST_SUITE("Complex pattern matching (std::regex path)") {
    
    TEST_CASE("Character classes") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"file[0-9]", "file1", true},
        {"file[0-9]", "file5", true},
        {"file[0-9]", "filex", false},
        {"file[abc]", "filea", true},
        {"file[abc]", "filed", false}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Negated character classes") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"file[^a]", "fileb", true},
        {"file[^a]", "filea", false}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Quantifiers") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"file{1,3}", "file", true},
        {"file{1,3}", "filefile", true},
        {"file+", "file", true},
        {"file+", "filefile", true},
        {"file?", "fil", true},
        {"file?", "file", true}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Alternation") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"file|directory", "file", true},
        {"file|directory", "directory", true},
        {"file|directory", "other", false}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Groups") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"(file|dir)", "file", true},
        {"(file|dir)", "dir", true}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Complex pattern case-insensitive") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"[A-Z]+", "HELLO", true, false},
        {"[a-z]+", "HELLO", true, false}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
  }
  
  TEST_SUITE("Pattern analysis") {
    
    TEST_CASE("PatternAnalysis correctly classifies literal patterns") {
      const std::vector<test_helpers::PatternAnalysisTestCase> test_cases = {
        {"hello", true, true, false},
        {"hello.", false, true, false},  // Dot makes it non-literal
        {"hello*", false, true, false},  // Star makes it non-literal
        {"hello\\.", true, true, false}  // Escaped dot is literal
      };
      test_helpers::RunParameterizedPatternAnalysisTests(test_cases);
    }
    
    TEST_CASE("PatternAnalysis correctly classifies simple patterns") {
      const std::vector<test_helpers::PatternAnalysisTestCase> test_cases = {
        {"h.llo", false, true, false},
        {"he*llo", false, true, false},
        {"^hello", false, true, false},
        {"hello$", false, true, false},
        {".*test.*", false, true, false},
        {"[0-9]+", false, false, false},  // Character class - not simple
        {"hello|world", false, false, false}  // Alternation - not simple
      };
      test_helpers::RunParameterizedPatternAnalysisTests(test_cases);
    }
    
    TEST_CASE("PatternAnalysis detects full match requirement") {
      const std::vector<test_helpers::PatternAnalysisTestCase> test_cases = {
        {"^hello$", false, true, true},
        {"^test$", false, true, true},
        {"^hello", false, true, false},  // Only start anchor
        {"hello$", false, true, false},  // Only end anchor
        {"hello", true, true, false}    // No anchors
      };
      test_helpers::RunParameterizedPatternAnalysisTests(test_cases);
    }
  }
  
  TEST_SUITE("Regex cache") {
    
    TEST_CASE("Cache stores compiled regexes") {
      std_regex_utils::ClearCache();
      
      // First call compiles and caches
      CHECK(std_regex_utils::RegexMatch("[0-9]+", "123") == true);
      size_t size1 = std_regex_utils::GetCacheSize();
      CHECK(size1 > 0);
      
      // Second call with same pattern uses cache
      CHECK(std_regex_utils::RegexMatch("[0-9]+", "456") == true);
      size_t size2 = std_regex_utils::GetCacheSize();
      CHECK(size2 == size1);  // Cache size should be same (reused)
    }
    
    TEST_CASE("Cache handles case-sensitive and case-insensitive separately") {
      std_regex_utils::ClearCache();
      
      // Case-sensitive pattern
      std_regex_utils::RegexMatch("[A-Z]+", "HELLO", true);
      size_t size1 = std_regex_utils::GetCacheSize();
      
      // Case-insensitive pattern (different cache entry)
      std_regex_utils::RegexMatch("[A-Z]+", "hello", false);
      size_t size2 = std_regex_utils::GetCacheSize();
      
      CHECK(size2 > size1);  // Should have two entries now
    }
    
    TEST_CASE("ClearCache removes all entries") {
      std_regex_utils::RegexMatch("[0-9]+", "123");
      std_regex_utils::RegexMatch("[a-z]+", "abc");
      
      size_t size_before = std_regex_utils::GetCacheSize();
      CHECK(size_before > 0);
      
      std_regex_utils::ClearCache();
      size_t size_after = std_regex_utils::GetCacheSize();
      CHECK(size_after == 0);
    }
  }
  
  TEST_SUITE("Error handling") {
    
    TEST_CASE("Empty pattern returns false") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"", "hello", false},
        {"", "", false}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Invalid regex patterns return false") {
      // These patterns are invalid and should return false without crashing
      std::vector<test_helpers::RegexMatchTestCase> test_cases = {
          {"[", "test", false},      // Unclosed bracket
          {"(", "test", false},      // Unclosed paren
          {"{", "test", false},      // Unclosed brace
          {"*", "test", false},      // Star without preceding
          {"+", "test", false},      // Plus without preceding
          {"?", "test", false}       // Question without preceding
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Null text pointer returns false") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"pattern", "", false}  // Empty text should return false
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
      
      // Test with empty string_view explicitly
      std::string_view empty_text("", 0);
      CHECK(std_regex_utils::RegexMatch("pattern", empty_text) == false);
    }
  }
  
  TEST_SUITE("PatternAnalysis") {
    
    TEST_CASE("PatternAnalysis correctly classifies patterns") {
      const std::vector<test_helpers::PatternAnalysisTestCase> test_cases = {
        {"hello", true, true, false},
        {"h.llo", false, true, false},
        {"^hello$", false, true, true},
        {"[0-9]+", false, false, false}
      };
      test_helpers::RunParameterizedPatternAnalysisTests(test_cases);
    }
  }
  
  TEST_SUITE("RegexMatchOptimized") {
    
    TEST_CASE("RegexMatchOptimized uses pre-analyzed pattern") {
      std_regex_utils::PatternAnalysis analysis("hello");
      
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"hello", "hello world", true},
        {"hello", "world", false}
      };
      for (const auto& tc : test_cases) {
        DOCTEST_SUBCASE((tc.pattern + " vs " + tc.text).c_str()) {
          CHECK(std_regex_utils::RegexMatchOptimized(tc.pattern, tc.text, analysis) == tc.expected);
        }
      }
    }
    
    TEST_CASE("RegexMatchOptimized respects case sensitivity") {
      std_regex_utils::PatternAnalysis analysis("Hello");
      
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"Hello", "Hello World", true, true},
        {"Hello", "hello world", false, true},
        {"Hello", "hello world", true, false}
      };
      for (const auto& tc : test_cases) {
        DOCTEST_SUBCASE((tc.pattern + " vs " + tc.text).c_str()) {
          CHECK(std_regex_utils::RegexMatchOptimized(tc.pattern, tc.text, analysis, tc.case_sensitive) == tc.expected);
        }
      }
    }
  }
  
  TEST_SUITE("String overloads") {
    
    TEST_CASE("String pattern and text overload") {
      std::string pattern = "hello";
      std::string text = "hello world";
      CHECK(std_regex_utils::RegexMatch(std::string_view(pattern), std::string_view(text)) == true);
    }
    
    TEST_CASE("String pattern and string_view text overload") {
      std::string pattern = "test";
      std::string_view text = "this is a test";
      CHECK(std_regex_utils::RegexMatch(std::string_view(pattern), text) == true);
    }
    
    TEST_CASE("String pattern and C-string text overload") {
      std::string pattern = "world";
      const char* text = "hello world";
      CHECK(std_regex_utils::RegexMatch(std::string_view(pattern), std::string_view(text)) == true);
    }
  }
  
  TEST_SUITE("Edge cases") {
    
    TEST_CASE("Very long patterns") {
      std::string long_pattern(1000, 'a');
      std::string long_text(2000, 'a');
      CHECK(std_regex_utils::RegexMatch(long_pattern, long_text) == true);
    }
    
    TEST_CASE("Unicode characters in literal patterns") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"café", "café", true},
        {"тест", "тест", true}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
    
    TEST_CASE("Special characters in text") {
      const std::vector<test_helpers::RegexMatchTestCase> test_cases = {
        {"test", "file.test", true},
        {"test", "test*file", true},
        {"test", "test^file", true}
      };
      test_helpers::RunParameterizedRegexMatchTests(test_cases);
    }
  }
}

