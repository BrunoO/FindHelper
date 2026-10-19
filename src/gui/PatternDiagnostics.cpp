#include "gui/PatternDiagnostics.h"

#include <string>

#include "path/PathPatternMatcher.h"
#include "search/SearchPatternUtils.h"

namespace {

// Compile one query as a PathPattern and return why it failed (kOk when fine).
// Non-PathPattern queries (plain substring, regex, fuzzy, or empty) have no
// PathPattern syntax to get wrong.
path_pattern::CompileStatus CheckQuery(std::string_view query, bool case_sensitive) {
  if (query.empty() ||
      search_pattern_utils::DetectPatternType(query) !=
        search_pattern_utils::PatternType::PathPattern) {
    return path_pattern::CompileStatus::kOk;
  }
  const std::string pattern = search_pattern_utils::ExtractPattern(query);
  if (pattern.empty()) {
    return path_pattern::CompileStatus::kOk;
  }
  const auto options = case_sensitive ? path_pattern::MatchOptions::kNone
                                       : path_pattern::MatchOptions::kCaseInsensitive;
  return path_pattern::CompilePathPattern(pattern, options).status;
}

}  // namespace

void UpdatePatternDiagnostics(std::string_view filename_query,
                              std::string_view path_query,
                              bool case_sensitive,
                              SearchPipelineState& pipeline) {
  if (pipeline.pattern_error_source_filename == filename_query &&
      pipeline.pattern_error_source_path == path_query &&
      pipeline.pattern_error_source_case_sensitive == case_sensitive) {
    return;  // inputs unchanged since the last verdict
  }
  pipeline.pattern_error_source_filename.assign(filename_query);
  pipeline.pattern_error_source_path.assign(path_query);
  pipeline.pattern_error_source_case_sensitive = case_sensitive;
  pipeline.pattern_error.clear();

  // Path first: when both are broken it is the more specific complaint.
  if (const path_pattern::CompileStatus status = CheckQuery(path_query, case_sensitive);
      status != path_pattern::CompileStatus::kOk) {
    pipeline.pattern_error = "Path pattern: ";
    pipeline.pattern_error += path_pattern::DescribeCompileStatus(status);
    return;
  }
  if (const path_pattern::CompileStatus name_status = CheckQuery(filename_query, case_sensitive);
      name_status != path_pattern::CompileStatus::kOk) {
    pipeline.pattern_error = "Name pattern: ";
    pipeline.pattern_error += path_pattern::DescribeCompileStatus(name_status);
  }
}