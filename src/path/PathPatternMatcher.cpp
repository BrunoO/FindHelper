/**
 * @file PathPatternMatcher.cpp
 * @brief Implementation of glob-style path pattern matching
 *
 * Implements a high-performance glob path pattern matcher supporting:
 * - Literals (including brackets, braces, and '+', which match literally)
 * - Wildcards: '*' (within segment), '?' (single non-separator), '**' (recursive across separators)
 * - Segment wildcards: double-star-slash (zero or more whole directories), slash-double-star (trailing directories)
 * - Anchors: '^' (start), '$' (end)
 * - Case-sensitive and case-insensitive matching
 * - Separator normalization ('/' and '\\' are interchangeable)
 *
 * @see PathPatternMatcher.h
 */

#include "path/PathPatternMatcher.h"

#include "utils/Logger.h"
#include "utils/StringSearch.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace path_pattern {

namespace {

inline bool IsSeparator(char c) {
  return c == '/' || c == '\\';
}

using string_search::ToLowerChar;

inline bool CharsEqual(char a, char b, bool case_insensitive) {
  if (!case_insensitive) {
    return a == b;
  }
  return ToLowerChar(static_cast<unsigned char>(a)) == ToLowerChar(static_cast<unsigned char>(b));
}

using StateMask = std::uint64_t;
static_assert(kMaxSimpleTokens == sizeof(StateMask) * 8U,
              "StateMask is 64-bit; kMaxSimpleTokens must equal 64");

inline StateMask Bit(std::size_t i) {
  assert(i < kMaxSimpleTokens);
  return static_cast<StateMask>(1) << i;
}

void AppendCompiledToken(CompiledPathPattern& compiled, SimpleToken tok) {
  if (compiled.token_count < kMaxSimpleTokens) {
    compiled.inline_tokens[compiled.token_count] = tok;  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index) - index validated by surrounding bounds check
    ++compiled.token_count;
    return;
  }
  if (compiled.token_count == kMaxSimpleTokens) {
    compiled.overflow_tokens.reserve(compiled.pattern_string.size());
    compiled.overflow_tokens.assign(compiled.inline_tokens.begin(), compiled.inline_tokens.end());
  }
  compiled.overflow_tokens.push_back(tok);
  ++compiled.token_count;
}

void BuildSimpleTokens(std::string_view pattern, bool case_insensitive,
                       CompiledPathPattern& compiled) {
  compiled.token_count = 0;
  compiled.overflow_tokens.clear();

  for (std::size_t i = 0; i < pattern.size(); ++i) {  // NOSONAR(cpp:S886) - pattern size is stable for this loop
    if (const char c = pattern[i]; c == '*') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index validated by surrounding bounds check
      if (i + 1 < pattern.size() && pattern[i + 1] == '*') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index validated by surrounding bounds check
        AppendCompiledToken(compiled, SimpleToken{SimpleTokKind::kStarAny, '\0'});
        ++i;  // consume second '*'
      } else {
        AppendCompiledToken(compiled, SimpleToken{SimpleTokKind::kStarNonSep, '\0'});
      }
    } else if (c == '?') {
      AppendCompiledToken(compiled, SimpleToken{SimpleTokKind::kAnyNonSep, '\0'});
    } else if (c == '/' || c == '\\') {
      AppendCompiledToken(compiled, SimpleToken{SimpleTokKind::kSeparator, '\0'});
    } else {
      const char lit = case_insensitive ? ToLowerChar(static_cast<unsigned char>(c)) : c;
      AppendCompiledToken(compiled, SimpleToken{SimpleTokKind::kLiteral, lit});
    }
  }
}

StateMask ComputeEpsilonMask(const SimpleToken* tokens, size_t count) {
  StateMask mask = 0;
  for (size_t i = 0; i < count; ++i) {
    if (tokens[i].kind == SimpleTokKind::kStarNonSep || tokens[i].kind == SimpleTokKind::kStarAny) {
      mask |= Bit(i);
    }
  }
  return mask;
}

inline StateMask ApplyEpsilonClosure(StateMask epsilon_mask, StateMask active) {
  StateMask prev = 0;
  do {
    prev = active;
    active |= (active & epsilon_mask) << 1u;
  } while (active != prev);
  return active;
}

StateMask StepSimpleNfaOptimized(const SimpleToken* tokens, size_t n, StateMask epsilon_mask,
                                 StateMask active, char c, bool case_insensitive) {
  StateMask next = 0;
  const char lowered_c = case_insensitive ? ToLowerChar(static_cast<unsigned char>(c)) : c;

  for (std::size_t i = 0; i < n; ++i) {
    if ((active & Bit(i)) == 0) {
      continue;
    }

    switch (const SimpleToken& tok = tokens[i]; tok.kind) {
      case SimpleTokKind::kLiteral:
        if (lowered_c == tok.literal) {
          next |= Bit(i + 1);
        }
        break;

      case SimpleTokKind::kSeparator:
        if (IsSeparator(c)) {
          next |= Bit(i + 1);
        }
        break;

      case SimpleTokKind::kAnyNonSep:
        if (!IsSeparator(c)) {
          next |= Bit(i + 1);
        }
        break;

      case SimpleTokKind::kStarNonSep:
        if (!IsSeparator(c)) {
          next |= Bit(i);
        }
        break;

      case SimpleTokKind::kStarAny:
        next |= Bit(i);
        break;
    }
  }

  return ApplyEpsilonClosure(epsilon_mask, next);
}

bool NeedsSegmentSemantics(std::string_view pattern) {
  if (pattern.find("**/") != std::string_view::npos ||
      pattern.find("**\\") != std::string_view::npos) {
    return true;
  }
  if (pattern.size() >= 3) {
    if (const std::string_view tail = pattern.substr(pattern.size() - 3);
        tail == "/**" || tail == "\\**") {
      return true;
    }
  }
  return false;
}

bool IsLiteralOnly(std::string_view pattern) {
  return std::all_of(pattern.begin(), pattern.end(),
                     [](char c) { return c != '*' && c != '?'; });
}

[[nodiscard]] inline bool IsSafeLiteralChar(char c) {
  return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '.' || c == '_' || c == '-';
}

[[nodiscard]] std::string_view StripAnchors(std::string_view pattern) {
  std::string_view stripped = pattern;
  if (!stripped.empty() && stripped.front() == '^') {
    stripped.remove_prefix(1);
  }
  if (!stripped.empty() && stripped.back() == '$') {
    stripped.remove_suffix(1);
  }
  return stripped;
}

auto ElapsedMicroseconds(std::chrono::high_resolution_clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::high_resolution_clock::now() - start);
}

constexpr size_t kMaxBacktrackSteps = 10000;

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - recursive matcher
// NOSONAR(cpp:S3776) - recursive matcher
bool MatchTokensRecursive(const SimpleToken* tokens, size_t token_count, size_t tok_idx,
                          std::string_view path, size_t path_idx, bool case_insensitive,
                          size_t& steps) {
  if (++steps > kMaxBacktrackSteps) {
    return false;  // fail closed on pathological input
  }

  if (tok_idx == token_count) {
    return path_idx == path.size();
  }

  const SimpleToken& tok = tokens[tok_idx];

  // Zero-segment skip for **/ (DoubleStarSlash):
  if (tok.kind == SimpleTokKind::kStarAny && tok_idx + 1 < token_count &&
      tokens[tok_idx + 1].kind == SimpleTokKind::kSeparator &&
      MatchTokensRecursive(tokens, token_count, tok_idx + 2, path, path_idx, case_insensitive, steps)) {
    return true;
  }

  // Zero-segment skip for trailing double-star slash (TrailingSlashDoubleStar):
  if (tok.kind == SimpleTokKind::kSeparator && tok_idx + 1 < token_count &&
      tokens[tok_idx + 1].kind == SimpleTokKind::kStarAny && tok_idx + 2 == token_count &&
      path_idx == path.size()) {
    return true;
  }

  switch (tok.kind) {
    case SimpleTokKind::kLiteral:
      if (path_idx < path.size()) {
        if (const char lowered_c = case_insensitive
                                     ? ToLowerChar(static_cast<unsigned char>(path[path_idx]))  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - path_idx < path.size() checked above
                                     : path[path_idx];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - path_idx < path.size() checked above
            lowered_c == tok.literal) {
          return MatchTokensRecursive(tokens, token_count, tok_idx + 1, path, path_idx + 1, case_insensitive, steps);
        }
      }
      return false;

    case SimpleTokKind::kSeparator:
      if (path_idx < path.size() && IsSeparator(path[path_idx])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index validated by surrounding bounds check
        return MatchTokensRecursive(tokens, token_count, tok_idx + 1, path, path_idx + 1, case_insensitive, steps);
      }
      return false;

    case SimpleTokKind::kAnyNonSep:
      if (path_idx < path.size() && !IsSeparator(path[path_idx])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index validated by surrounding bounds check
        return MatchTokensRecursive(tokens, token_count, tok_idx + 1, path, path_idx + 1, case_insensitive, steps);
      }
      return false;

    case SimpleTokKind::kStarNonSep: {
      // Greedily advance within the segment, then backtrack
      size_t end = path_idx;
      while (end < path.size() && !IsSeparator(path[end])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index validated by surrounding bounds check
        ++end;
      }
      for (size_t p = end; p >= path_idx; --p) {
        if (MatchTokensRecursive(tokens, token_count, tok_idx + 1, path, p, case_insensitive, steps)) {
          return true;
        }
        if (p == path_idx) {
          break;
        }
      }
      return false;
    }

    case SimpleTokKind::kStarAny: {
      // Greedily advance across all characters, then backtrack
      for (size_t p = path.size(); p >= path_idx; --p) {
        if (MatchTokensRecursive(tokens, token_count, tok_idx + 1, path, p, case_insensitive, steps)) {
          return true;
        }
        if (p == path_idx) {
          break;
        }
      }
      return false;
    }
  }
  return false;
}

inline bool MatchSimpleTokensBacktracking(const CompiledPathPattern& compiled,
                                          std::string_view path) {
  size_t steps = 0;
  return MatchTokensRecursive(compiled.TokensData(), compiled.token_count, 0, path, 0,
                              compiled.case_insensitive, steps);
}

std::string LowerRunForPrefilter(std::string_view run, bool case_insensitive) {
  if (!case_insensitive) {
    return std::string(run);
  }
  std::string lower;
  lower.reserve(run.size());
  for (const char c : run) {
    lower.push_back(ToLowerChar(static_cast<unsigned char>(c)));
  }
  return lower;
}

struct RequiredSubstringInfo {
  std::string substring;
  bool is_prefix = false;
  bool is_suffix = false;
};

RequiredSubstringInfo ExtractRequiredSubstring(std::string_view pattern, bool case_insensitive) {
  if (pattern.length() < kMinRequiredSubstringLength) {
    return {};
  }

  std::vector<std::string_view> runs;
  AppendLiteralRuns(pattern, runs);
  const std::string_view* best = nullptr;
  for (const std::string_view& run : runs) {
    if (best == nullptr || run.size() > best->size()) {
      best = &run;
    }
  }
  if (best == nullptr || best->size() < kMinRequiredSubstringLength) {
    return {};
  }

  RequiredSubstringInfo info;
  info.substring = LowerRunForPrefilter(*best, case_insensitive);
  const auto best_offset = static_cast<size_t>(best->data() - pattern.data());
  info.is_prefix = (best_offset == 0);
  info.is_suffix = (best_offset + best->size() == pattern.size());
  if (!info.is_suffix && !pattern.empty() && IsSafeLiteralChar(pattern.back())) {
    if (const std::string_view trailing = runs.back();
        trailing.size() == best->size() &&
        LowerRunForPrefilter(trailing, case_insensitive) == info.substring) {
      info.is_suffix = true;
    }
  }
  return info;
}

void CollectExtraRequiredSubstrings(CompiledPathPattern& compiled, std::string_view pattern) {
  std::vector<std::string_view> runs;
  AppendLiteralRuns(pattern, runs);
  if (runs.empty()) {
    return;
  }
  const bool has_trailing_run = !pattern.empty() && IsSafeLiteralChar(pattern.back());
  const bool primary_covers_suffix =
      compiled.has_required_substring && compiled.required_substring_is_suffix;

  for (const std::string_view run : runs) {
    if (run.size() < kMinRequiredSubstringLength) {
      continue;
    }
    const size_t run_end = static_cast<size_t>(run.data() - pattern.data()) + run.size();
    const bool is_trailing = has_trailing_run && run_end == pattern.size();
    std::string lowered = LowerRunForPrefilter(run, compiled.case_insensitive);
    if (is_trailing) {
      if (!(primary_covers_suffix && lowered == compiled.required_substring)) {
        compiled.required_suffix = std::move(lowered);
        compiled.has_required_suffix = true;
      }
      continue;
    }
    if (compiled.has_required_substring && lowered == compiled.required_substring) {
      continue;
    }
    compiled.extra_required_substrings.push_back(std::move(lowered));
  }
  compiled.extra_required_hints.reserve(compiled.extra_required_substrings.size());
  for (const std::string& extra : compiled.extra_required_substrings) {
    compiled.extra_required_hints.push_back(string_search::MakeSimdSubstringHints(extra));
  }
}

inline bool CheckRequiredSubstringPrefix(std::string_view path, std::string_view required,
                                         bool case_insensitive) {
  if (case_insensitive) {
    return string_search::string_search_detail::CheckPrefix<
        string_search::string_search_detail::CaseInsensitive>(path, required);
  }
  return string_search::string_search_detail::CheckPrefix<
      string_search::string_search_detail::CaseSensitive>(path, required);
}

inline bool CheckRequiredSubstringSuffix(std::string_view path, std::string_view required,
                                         bool case_insensitive) {
  if (path.size() < required.size()) {
    return false;
  }
  const size_t start_pos = path.size() - required.size();
  if (case_insensitive) {
    return string_search::string_search_detail::ReverseCompare<
        string_search::string_search_detail::CaseInsensitive>(path, required, start_pos);
  }
  return string_search::string_search_detail::ReverseCompare<
      string_search::string_search_detail::CaseSensitive>(path, required, start_pos);
}

inline bool CheckRequiredSubstringContains(std::string_view path, std::string_view required,
                                           const string_search::SimdSubstringHints& hints,
                                           bool case_insensitive) {
  return case_insensitive ? string_search::ContainsSubstringI(path, required, hints)
                           : string_search::ContainsSubstring(path, required, hints);
}

inline bool MatchLiteralOnlyPath(const CompiledPathPattern& compiled,
                                  std::string_view path) {
  if (path.size() != compiled.pattern_string.size()) {
    return false;
  }
  return std::equal(path.begin(), path.end(), compiled.pattern_string.begin(),
                    [case_insensitive = compiled.case_insensitive](char a, char b) {
                      if (IsSeparator(a) && IsSeparator(b)) {
                        return true;
                      }
                      return CharsEqual(a, b, case_insensitive);
                    });
}

inline bool HasPrefilterConstraints(const CompiledPathPattern& compiled) {
  return compiled.has_required_substring || compiled.has_required_suffix ||
         !compiled.extra_required_substrings.empty();
}

inline bool CheckRequiredSubstringForMatch(
    const CompiledPathPattern& compiled, std::string_view path) {
  if (compiled.has_required_substring) {
    bool primary_ok = false;
    if (compiled.required_substring_is_prefix) {
      primary_ok = CheckRequiredSubstringPrefix(path, compiled.required_substring,
                                                compiled.case_insensitive);
    } else if (compiled.required_substring_is_suffix) {
      primary_ok = CheckRequiredSubstringSuffix(path, compiled.required_substring,
                                                compiled.case_insensitive);
    } else {
      primary_ok = CheckRequiredSubstringContains(path, compiled.required_substring,
                                                  compiled.required_substring_hints,
                                                  compiled.case_insensitive);
    }
    if (!primary_ok) {
      return false;
    }
  }
  if (compiled.has_required_suffix &&
      !CheckRequiredSubstringSuffix(path, compiled.required_suffix,
                                    compiled.case_insensitive)) {
    return false;
  }
  for (size_t i = 0; i < compiled.extra_required_substrings.size(); ++i) {
    if (!CheckRequiredSubstringContains(path, compiled.extra_required_substrings[i],  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index validated by surrounding bounds check
                                        compiled.extra_required_hints[i],  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index validated by surrounding bounds check
                                        compiled.case_insensitive)) {
      return false;
    }
  }
  return true;
}

inline bool MatchSimplePathPattern(const CompiledPathPattern& compiled,
                                   std::string_view path) {
  const SimpleToken* tokens = compiled.inline_tokens.data();
  const size_t n = compiled.token_count;
  StateMask active = ApplyEpsilonClosure(compiled.epsilon_mask, Bit(0));
  for (const char c : path) {
    active = StepSimpleNfaOptimized(tokens, n, compiled.epsilon_mask, active, c,
                                    compiled.case_insensitive);
    if (active == 0) {
      return false;
    }
  }
  return (active & Bit(n)) != 0;
}

}  // namespace

CompiledPathPattern CompilePathPattern(std::string_view pattern, bool case_insensitive) {
  const auto compile_start = std::chrono::high_resolution_clock::now();

  const std::string_view pattern_without_anchors = StripAnchors(pattern);

  CompiledPathPattern compiled;
  compiled.case_insensitive = case_insensitive;
  compiled.pattern_string = pattern_without_anchors;

  if (IsAllPassPathPattern(pattern_without_anchors)) {
    compiled.is_all_pass = true;
    return compiled;
  }

  compiled.is_literal_only = IsLiteralOnly(pattern_without_anchors);
  if (compiled.is_literal_only) {
    [[maybe_unused]] const auto compile_duration = ElapsedMicroseconds(compile_start);
    LOG_INFO_BUILD("PathPattern compilation [LITERAL]: pattern='"
                   << compiled.pattern_string
                   << "', case_insensitive=" << (compiled.case_insensitive ? "true" : "false")
                   << ", total_time=" << compile_duration.count() << "μs");
    return compiled;
  }

  BuildSimpleTokens(pattern_without_anchors, case_insensitive, compiled);

  if (!NeedsSegmentSemantics(pattern_without_anchors) &&
      compiled.token_count + 1 <= kMaxSimpleTokens) {
    compiled.needs_backtracking = false;
    compiled.epsilon_mask = ComputeEpsilonMask(compiled.inline_tokens.data(), compiled.token_count);
  } else {
    compiled.needs_backtracking = true;
  }

  if (auto req_substr_info = ExtractRequiredSubstring(pattern_without_anchors, case_insensitive);
      !req_substr_info.substring.empty()) {
    compiled.required_substring = std::move(req_substr_info.substring);
    compiled.required_substring_hints =
        string_search::MakeSimdSubstringHints(compiled.required_substring);
    compiled.has_required_substring = true;
    compiled.required_substring_is_prefix = req_substr_info.is_prefix;
    compiled.required_substring_is_suffix = req_substr_info.is_suffix;
  }

  CollectExtraRequiredSubstrings(compiled, pattern_without_anchors);

  [[maybe_unused]] const auto compile_duration = ElapsedMicroseconds(compile_start);
  LOG_INFO_BUILD("PathPattern compilation: pattern='"
                 << compiled.pattern_string
                 << "', case_insensitive=" << (compiled.case_insensitive ? "true" : "false")
                 << ", tokens=" << compiled.token_count
                 << ", needs_backtracking=" << (compiled.needs_backtracking ? "true" : "false")
                 << ", total_time=" << compile_duration.count() << "μs");

  return compiled;
}

const char* DescribeCompileStatus(CompileStatus status) {
  switch (status) {
    case CompileStatus::kOk:
      return "ok";
    case CompileStatus::kMovedFrom:
      return "moved-from pattern";
  }
  return "unknown";
}

bool PathPatternMatches(const CompiledPathPattern& compiled, std::string_view path) {
  if (!compiled.IsValid()) {
    return false;
  }
  if (compiled.is_all_pass) {
    return true;
  }
  if (compiled.is_literal_only) {
    return MatchLiteralOnlyPath(compiled, path);
  }
  if (HasPrefilterConstraints(compiled) && !CheckRequiredSubstringForMatch(compiled, path)) {
    return false;
  }
  if (!compiled.needs_backtracking) {
    return MatchSimplePathPattern(compiled, path);
  }
  return MatchSimpleTokensBacktracking(compiled, path);
}

bool PathPatternMatches(std::string_view pattern, std::string_view path, bool case_insensitive) {
  const CompiledPathPattern compiled = CompilePathPattern(pattern, case_insensitive);
  return PathPatternMatches(compiled, path);
}

void AppendLiteralRuns(std::string_view pattern, std::vector<std::string_view>& out_runs) {
  out_runs.clear();
  out_runs.reserve((pattern.size() + 1U) / 2U);
  size_t i = 0;
  while (i < pattern.size()) {
    while (i < pattern.size() && !IsSafeLiteralChar(pattern[i])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index validated by surrounding bounds check
      ++i;
    }
    const size_t run_start = i;
    while (i < pattern.size() && IsSafeLiteralChar(pattern[i])) {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index validated by surrounding bounds check
      ++i;
    }
    if (i > run_start) {
      out_runs.push_back(pattern.substr(run_start, i - run_start));
    }
  }
}

bool FindPathPatternSpans(std::string_view pattern, std::string_view text, bool case_sensitive,
                          std::vector<string_search::MatchSpan>& out_spans) {
  out_spans.clear();
  if (pattern.empty() || text.empty()) {
    return false;
  }

  std::vector<std::string_view> runs;
  AppendLiteralRuns(pattern, runs);
  if (runs.empty()) {
    return false;
  }

  out_spans.reserve(runs.size());

  size_t text_pos = 0;
  std::vector<string_search::MatchSpan> one;
  one.reserve(1);
  for (const std::string_view lit : runs) {
    if (text_pos > text.size()) {
      break;
    }
    if (const std::string_view remaining = text.substr(text_pos);
        !string_search::FindSubstringSpans(remaining, lit, case_sensitive, one) || one.empty()) {
      continue;
    }
    const string_search::MatchSpan& hit = one.front();
    out_spans.push_back(
        string_search::MatchSpan{text_pos + hit.start_, text_pos + hit.end_});
    text_pos = text_pos + hit.end_;
  }

  string_search::string_search_detail::MergeAdjacentSpans(out_spans);

  return !out_spans.empty();
}

}  // namespace path_pattern
