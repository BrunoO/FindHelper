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
    [[nodiscard]] bool operator()(char a, char b) const {
      return a == b;
    }
  };

  struct CharEqualI {
    [[nodiscard]] bool operator()(char a, char b) const {
      return std::tolower(static_cast<unsigned char>(a)) ==
             std::tolower(static_cast<unsigned char>(b));
    }
  };

  // Linear glob matching - iterative two-pointer with a single backtrack slot.
  // Same byte semantics as the previous recursive version: '*' matches any byte
  // run (including empty), '?' matches exactly one byte. O(n*m) worst case,
  // O(n) typical; no recursion, no per-suffix call overhead, which matters for
  // long haystacks (full paths) with 2+ stars.
  template<typename CharCompare>
  [[nodiscard]] inline bool GlobMatchImpl(std::string_view pattern, std::string_view text, CharCompare char_equal) {
    size_t pat_idx = 0;
    size_t txt_idx = 0;
    size_t star_idx = std::string_view::npos;  // Pattern index just past the last '*' seen
    size_t resume_idx = 0;                     // Text index to retry from after backtrack

    while (txt_idx < text.size()) {
      // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - every index is guarded by a < size() check in the same condition
      if (pat_idx < pattern.size() &&
          (pattern[pat_idx] == '?' || char_equal(pattern[pat_idx], text[txt_idx]))) {
        ++pat_idx;
        ++txt_idx;
      } else if (pat_idx < pattern.size() && pattern[pat_idx] == '*') {
        star_idx = ++pat_idx;
        resume_idx = txt_idx;
      } else if (star_idx != std::string_view::npos) {
        pat_idx = star_idx;
        txt_idx = ++resume_idx;
      } else {
        return false;
      }
      // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    }
    while (pat_idx < pattern.size() && pattern[pat_idx] == '*') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by < size() in the condition
      ++pat_idx;
    }
    return pat_idx == pattern.size();
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

[[nodiscard]] inline bool MatchHere(std::string_view re, std::string_view text) {
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

[[nodiscard]] inline bool MatchStar(char c, std::string_view re, std::string_view text) {
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
[[nodiscard]] inline bool MatchHereI(std::string_view re, std::string_view text) {
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
[[nodiscard]] inline bool MatchStarI(char c, std::string_view re, std::string_view text) {
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
