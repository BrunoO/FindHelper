#pragma once

#include <cctype>
#include <string_view>

namespace simple_regex {

// Forward declarations (PascalCase per .clang-tidy FunctionCase)
[[nodiscard]] inline bool MatchHere(std::string_view re, std::string_view text);
[[nodiscard]] inline bool MatchStar(char c, std::string_view re, std::string_view text);
[[nodiscard]] inline bool MatchHereI(std::string_view re, std::string_view text);
[[nodiscard]] inline bool MatchStarI(char c, std::string_view re, std::string_view text);

namespace detail {
  // Common pattern search loop - searches for pattern anywhere in text
  // Template parameter: Matcher function (matchhere or matchhereI)
  template<typename MatcherFunc>
  [[nodiscard]] inline bool SearchPatternAnywhere(std::string_view pattern, std::string_view text, MatcherFunc matcher) {
    do {
      if (matcher(pattern, text)) {
        return true;
      }
      if (text.empty()) {
        break;
      }
      text.remove_prefix(1);
    } while (true);
    return false;
  }

  // Character comparison functors for case-sensitive and case-insensitive matching
  struct CharEqual {
    [[nodiscard]] inline bool operator()(char a, char b) const {
      return a == b;
    }
  };

  struct CharEqualI {
    [[nodiscard]] inline bool operator()(char a, char b) const {
      return std::tolower(static_cast<unsigned char>(a)) ==
             std::tolower(static_cast<unsigned char>(b));
    }
  };

  // Common glob matching logic - template parameterized by character comparison
  template<typename CharCompare>
  [[nodiscard]] inline bool GlobMatchImpl(std::string_view pattern, std::string_view text, CharCompare char_equal) {  // NOLINT(misc-no-recursion) - recursive glob by design
    // Base case: empty pattern
    if (pattern.empty()) {
      return text.empty();
    }

    // Handle '*' wildcard
    if (pattern.front() == '*') {
      // Optimization: Trailing '*' matches everything
      if (pattern.size() == 1) {
        return true;
      }

      // Try to match the rest of the pattern with every suffix of text
      // This is a simple recursive implementation. For very long strings/patterns
      // with many stars, this can be slow, but for filenames it's usually fine.
      const std::string_view remaining_pattern = pattern.substr(1);
      std::string_view current_text = text;

      do {
        if (GlobMatchImpl(remaining_pattern, current_text, char_equal)) {
          return true;
        }
        if (current_text.empty()) {
          break;
        }
        current_text.remove_prefix(1);
      } while (true);

      return false;
    }

    // Handle '?' wildcard
    if (pattern.front() == '?') {
      if (text.empty()) {
        return false;
      }
      return GlobMatchImpl(pattern.substr(1), text.substr(1), char_equal);
    }

    // Handle literal character
    if (!text.empty() && char_equal(pattern.front(), text.front())) {
      return GlobMatchImpl(pattern.substr(1), text.substr(1), char_equal);
    }

    return false;
  }
}

// Rob Pike's simple regex matcher
// Adapted for C++ std::string_view
// Supports:
//   c    matches any literal character c
//   .    matches any single character
//   ^    matches beginning of input
//   $    matches end of input
//   *    matches zero or more occurrences of previous character
[[nodiscard]] inline bool RegExMatch(std::string_view pattern, std::string_view text) {
  if (!pattern.empty() && pattern.front() == '^') {
    return MatchHere(pattern.substr(1), text);
  }

  // Search for pattern anywhere in text
  return detail::SearchPatternAnywhere(pattern, text, MatchHere);
}

[[nodiscard]] inline bool MatchHere(std::string_view re, std::string_view text) {  // NOLINT(misc-no-recursion) - recursive regex by design
  if (re.empty()) {
    return true;
  }

  if (re.size() > 1 && re[1] == '*') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - regex engine, indexed by construction invariant
    return MatchStar(re.front(), re.substr(2), text);
  }

  if (re.front() == '$' && re.size() == 1) {
    return text.empty();
  }

  if (!text.empty() && (re.front() == '.' || re.front() == text.front())) {
    return MatchHere(re.substr(1), text.substr(1));
  }

  return false;
}

[[nodiscard]] inline bool MatchStar(char c, std::string_view re, std::string_view text) {  // NOLINT(misc-no-recursion) - recursive regex by design
  do {
    // A * matches zero or more instances
    if (MatchHere(re, text)) {
      return true;
    }
  } while (!text.empty() && (text.front() == c || c == '.') &&
           (text.remove_prefix(1), true));  // NOSONAR(cpp:S872) - Comma operator used intentionally for side effect (remove_prefix) in loop condition

  return false;
}

// Simple Glob Matcher
// Supports:
//   *    matches any sequence of characters
//   ?    matches any single character
[[nodiscard]] inline bool GlobMatch(std::string_view pattern, std::string_view text) {
  return detail::GlobMatchImpl(pattern, text, detail::CharEqual{});
}

// Case-insensitive character comparison helper
[[nodiscard]] inline bool CharEqualI(char a, char b) {
  return std::tolower(static_cast<unsigned char>(a)) ==
         std::tolower(static_cast<unsigned char>(b));
}

// Case-insensitive version of RegExMatch
[[nodiscard]] inline bool RegExMatchI(std::string_view pattern, std::string_view text) {
  if (!pattern.empty() && pattern.front() == '^') {
    return MatchHereI(pattern.substr(1), text);
  }

  // Search for pattern anywhere in text
  return detail::SearchPatternAnywhere(pattern, text, MatchHereI);
}

// Case-insensitive helper for RegExMatchI
[[nodiscard]] inline bool MatchHereI(std::string_view re, std::string_view text) {  // NOLINT(misc-no-recursion) - recursive regex by design
  if (re.empty()) {
    return true;
  }

  if (re.size() > 1 && re[1] == '*') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - regex engine, indexed by construction invariant
    return MatchStarI(re.front(), re.substr(2), text);
  }

  if (re.front() == '$' && re.size() == 1) {
    return text.empty();
  }

  if (!text.empty() && (re.front() == '.' || CharEqualI(re.front(), text.front()))) {
    return MatchHereI(re.substr(1), text.substr(1));
  }

  return false;
}

// Case-insensitive helper for RegExMatchI
[[nodiscard]] inline bool MatchStarI(char c, std::string_view re, std::string_view text) {  // NOLINT(misc-no-recursion) - recursive regex by design
  do {
    // A * matches zero or more instances
    if (MatchHereI(re, text)) {
      return true;
    }
  } while (!text.empty() && (CharEqualI(text.front(), c) || c == '.') &&
           (text.remove_prefix(1), true));  // NOSONAR(cpp:S872) - Comma operator used intentionally for side effect (remove_prefix) in loop condition

  return false;
}

// Case-insensitive version of GlobMatch
[[nodiscard]] inline bool GlobMatchI(std::string_view pattern, std::string_view text) {
  return detail::GlobMatchImpl(pattern, text, detail::CharEqualI{});
}

} // namespace simple_regex
