#pragma once

#include "search/FilenameTrigramExtractor.h"
#include "search/SearchContext.h"
#include "search/SearchPatternUtils.h"
#include "utils/StdRegexUtils.h"
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

} // namespace detail

class TrigramQueryPlanner {
public:
  /**
   * Given a search context, extract the required trigrams needed to match the query.
   * Returns nullopt if no reliable trigrams can be extracted (e.g. query < 3 chars).
   */
  [[nodiscard]] static std::optional<std::vector<TrigramKey>> Plan(const SearchContext& context) {
    if (context.filename_query.empty()) {
      return std::nullopt;
    }

    const std::string_view query = context.filename_query;
    std::vector<TrigramKey> all_trigrams;
    std::vector<TrigramKey> scratch_trigrams;

    switch (const auto pattern_type = search_pattern_utils::DetectPatternType(query); pattern_type) {
      case search_pattern_utils::PatternType::Substring: {
        ExtractTrigrams(query, all_trigrams);
        break;
      }
      case search_pattern_utils::PatternType::Fuzzy:
        return std::nullopt;
      case search_pattern_utils::PatternType::PathPattern: {
        const std::string_view raw_pattern = search_pattern_utils::ExtractPatternView(query);
        // Fall back to parallel scan if the pattern specifies path separators or
        // complex/anchored pattern features that cannot be soundly deconstructed.
        if (raw_pattern.find_first_of("/\\^$[]{}") != std::string_view::npos) {
          return std::nullopt;
        }

        detail::ExtractWildcardSegments(raw_pattern, all_trigrams, scratch_trigrams);
        break;
      }
      case search_pattern_utils::PatternType::Glob: {
        detail::ExtractWildcardSegments(query, all_trigrams, scratch_trigrams);
        break;
      }
      case search_pattern_utils::PatternType::StdRegex: {
        const std::string_view regex_str = search_pattern_utils::ExtractPatternView(query);
        if (const std::string literal = std_regex_utils::detail::ExtractRequiredLiteral(regex_str);
            literal.size() >= 3) {
          ExtractTrigrams(literal, all_trigrams);
        }
        break;
      }
      default:
        return std::nullopt;
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
