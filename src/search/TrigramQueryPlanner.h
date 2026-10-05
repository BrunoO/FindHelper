#pragma once

#include "search/FilenameTrigramExtractor.h"
#include "search/SearchContext.h"
#include "search/SearchPatternUtils.h"
#include "utils/StdRegexUtils.h"
#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

namespace filename_trigram {

namespace detail {

inline void ExtractWildcardSegments(std::string_view pattern,
                                    std::vector<TrigramKey>& out,
                                    std::vector<TrigramKey>& scratch) {
  std::string_view remaining = pattern;
  while (!remaining.empty()) {
    const size_t wildcard_pos = remaining.find_first_of("*?");
    if (const std::string_view segment = remaining.substr(0, wildcard_pos); segment.size() >= 3) {
      ExtractTrigrams(segment, scratch);
      out.insert(out.end(), scratch.begin(), scratch.end());
    }
    if (wildcard_pos == std::string_view::npos) {
      break;
    }
    remaining.remove_prefix(wildcard_pos + 1);
  }
}

/**
 * Returns the substring after the last '/' or '\' in a prefix-stripped pattern.
 * Callers must strip any rs:/pp:/fz: prefix with ExtractPatternView first.
 * Returns an empty view when the pattern ends with a separator or is empty.
 */
[[nodiscard]] inline std::string_view ExtractLeafPattern(std::string_view raw_pattern) {
  const size_t sep_pos = raw_pattern.find_last_of("/\\");
  if (sep_pos == std::string_view::npos) {
    return raw_pattern;
  }
  if (sep_pos + 1 >= raw_pattern.size()) {
    return std::string_view{};
  }
  return raw_pattern.substr(sep_pos + 1);
}

[[nodiscard]] inline bool HasPathSeparator(std::string_view text) {
  return text.find_first_of("/\\") != std::string_view::npos;
}

/**
 * Whole-leaf disqualification: any of these metacharacters anywhere in the
 * leaf makes trigram extraction unsound without a full sub-language parser.
 */
[[nodiscard]] inline bool HasComplexLeafMetacharacters(std::string_view leaf) {
  return leaf.find_first_of("^$[]{}") != std::string_view::npos;
}

/**
 * \d and \w escapes contain a backslash, which would corrupt leaf splitting
 * (the backslash looks like a directory separator). Bail out conservatively.
 */
[[nodiscard]] inline bool HasRegexShorthandEscape(std::string_view text) {
  return text.find("\\d") != std::string_view::npos ||
         text.find("\\w") != std::string_view::npos;
}

/**
 * Plans trigrams for a separator-aware leaf literal: strips any rs:/pp:/fz:
 * prefix, takes the segment after the last separator, and extracts trigrams
 * from its wildcard-free runs of 3+ characters. Shared by the PathPattern
 * cases of PlanFilenameQuery and PlanPathQuery (identical bodies).
 */
[[nodiscard]] inline std::optional<std::vector<TrigramKey>> PlanLeafTrigrams(
    std::string_view query, std::vector<TrigramKey>& scratch) {
  const std::string_view raw_pattern = search_pattern_utils::ExtractPatternView(query);
  if (HasRegexShorthandEscape(raw_pattern)) {
    return std::nullopt;
  }
  const std::string_view leaf = ExtractLeafPattern(raw_pattern);
  if (leaf.empty() || HasComplexLeafMetacharacters(leaf)) {
    return std::nullopt;
  }
  std::vector<TrigramKey> out;
  ExtractWildcardSegments(leaf, out, scratch);
  if (out.empty()) {
    return std::nullopt;
  }
  return out;
}

/**
 * Plans trigrams for the filename query side (matched against the filename
 * only via matchers.filename_matcher).
 */
[[nodiscard]] inline std::optional<std::vector<TrigramKey>> PlanFilenameQuery(
    std::string_view query, std::vector<TrigramKey>& scratch) {
  switch (const auto pattern_type = search_pattern_utils::DetectPatternType(query); pattern_type) {
    case search_pattern_utils::PatternType::Substring: {
      // A slash never appears in the indexed filename; such a query can only
      // match nothing, so there is nothing worth accelerating.
      if (HasPathSeparator(query)) {
        return std::nullopt;
      }
      std::vector<TrigramKey> out;
      ExtractTrigrams(query, out);
      if (out.empty()) {
        return std::nullopt;
      }
      return out;
    }
    case search_pattern_utils::PatternType::Fuzzy:
      return std::nullopt;
    case search_pattern_utils::PatternType::PathPattern: {
      return PlanLeafTrigrams(query, scratch);
    }
    case search_pattern_utils::PatternType::Glob: {
      // A Glob containing separators must use leaf-only extraction: directory
      // segments (e.g. "src") are not in the filename index, so requiring
      // their trigrams would cause false negatives.
      const std::string_view target = HasPathSeparator(query) ? ExtractLeafPattern(query) : query;
      if (target.empty() || HasComplexLeafMetacharacters(target)) {
        return std::nullopt;
      }
      std::vector<TrigramKey> out;
      ExtractWildcardSegments(target, out, scratch);
      if (out.empty()) {
        return std::nullopt;
      }
      return out;
    }
    case search_pattern_utils::PatternType::StdRegex: {
      // Note: backslashes here are regex escapes (e.g. "\\."), not path
      // separators, so no separator check. A required literal containing '/'
      // simply has no postings in the filename index, yielding no candidates
      // (correct: no filename contains a slash).
      const std::string_view regex_str = search_pattern_utils::ExtractPatternView(query);
      if (const std::string literal = std_regex_utils::detail::ExtractRequiredLiteral(regex_str);
          literal.size() >= 3) {
        std::vector<TrigramKey> out;
        ExtractTrigrams(literal, out);
        if (out.empty()) {
          return std::nullopt;
        }
        return out;
      }
      return std::nullopt;
    }
    default:
      return std::nullopt;
  }
}

/**
 * Plans trigrams for the path query side (matched against the full path via
 * matchers.path_matcher). Only leaf literals are usable: the index holds
 * filename trigrams, and a separator-free path pattern may match a directory
 * portion, so it yields no filename constraint.
 *
 * Soundness: in PathPattern '*' and '?' never cross separators while '**'
 * does, so a leaf literal required by the pattern must appear in the filename
 * (the last path segment).
 */
[[nodiscard]] inline std::optional<std::vector<TrigramKey>> PlanPathQuery(
    std::string_view query, std::vector<TrigramKey>& scratch) {
  switch (const auto pattern_type = search_pattern_utils::DetectPatternType(query); pattern_type) {
    case search_pattern_utils::PatternType::Substring:
    case search_pattern_utils::PatternType::Fuzzy:
    case search_pattern_utils::PatternType::StdRegex:
      // Substring without separators may match a directory portion of the full
      // path (no filename constraint); Fuzzy has no usable literals; a regex
      // on the full path cannot be safely mapped to filename trigrams.
      return std::nullopt;
    case search_pattern_utils::PatternType::PathPattern: {
      return PlanLeafTrigrams(query, scratch);
    }
    case search_pattern_utils::PatternType::Glob: {
      // Without separators the glob may match a directory: no constraint.
      if (!HasPathSeparator(query)) {
        return std::nullopt;
      }
      const std::string_view leaf = ExtractLeafPattern(query);
      if (leaf.empty() || HasComplexLeafMetacharacters(leaf)) {
        return std::nullopt;
      }
      std::vector<TrigramKey> out;
      ExtractWildcardSegments(leaf, out, scratch);
      if (out.empty()) {
        return std::nullopt;
      }
      return out;
    }
    default:
      return std::nullopt;
  }
}

} // namespace detail

class TrigramQueryPlanner {
public:
  /**
   * Given a search context, extract the required trigrams needed to match the query.
   * Plans the filename side and the path side independently and intersects
   * (concatenates) their requirements: a match must satisfy both matchers, so
   * every trigram from either side is required. A single successful side alone
   * is still a sound (weaker) filter.
   * Returns nullopt if no reliable trigrams can be extracted.
   */
  [[nodiscard]] static std::optional<std::vector<TrigramKey>> Plan(const SearchContext& context) {
    if (context.filename_query.empty() && context.path_query.empty()) {
      return std::nullopt;
    }

    std::vector<TrigramKey> all_trigrams;
    std::vector<TrigramKey> scratch_trigrams;

    if (!context.filename_query.empty()) {
      if (auto filename_trigrams =
              detail::PlanFilenameQuery(context.filename_query, scratch_trigrams);
          filename_trigrams.has_value()) {
        all_trigrams.insert(all_trigrams.end(), filename_trigrams->begin(), filename_trigrams->end());
      }
    }

    if (!context.path_query.empty()) {
      if (auto path_trigrams = detail::PlanPathQuery(context.path_query, scratch_trigrams);
          path_trigrams.has_value()) {
        all_trigrams.insert(all_trigrams.end(), path_trigrams->begin(), path_trigrams->end());
      }
    }

    if (all_trigrams.empty()) {
      return std::nullopt;
    }

    // Deduplicate combined trigrams
    std::sort(all_trigrams.begin(), all_trigrams.end());
    all_trigrams.erase(std::unique(all_trigrams.begin(), all_trigrams.end()), all_trigrams.end());

    return all_trigrams;
  }
};

} // namespace filename_trigram
