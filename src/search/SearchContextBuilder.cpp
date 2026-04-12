#include "search/SearchContextBuilder.h"

#include "path/PathPatternMatcher.h"
#include "search/SearchContext.h"
#include "search/SearchPatternUtils.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

namespace search_context_builder_detail {

void CompilePatternIfNeeded(std::string_view query,
                            bool case_sensitive,
                            std::shared_ptr<path_pattern::CompiledPathPattern>& pattern,
                            [[maybe_unused]] const char* pattern_name) {
  if (query.empty()) {
    return;
  }
  if (auto pattern_type = search_pattern_utils::DetectPatternType(query);
      pattern_type != search_pattern_utils::PatternType::PathPattern) {
    return;
  }
  const std::string path_pattern = search_pattern_utils::ExtractPattern(query);
  if (path_pattern.empty()) {
    return;
  }
  auto options = case_sensitive ? path_pattern::MatchOptions::kNone
                                : path_pattern::MatchOptions::kCaseInsensitive;
  pattern = std::make_shared<path_pattern::CompiledPathPattern>(
      path_pattern::CompilePathPattern(path_pattern, options));
  LOG_INFO_BUILD("Pre-compiled " << pattern_name << " PathPattern: '"
                                 << path_pattern << "' (before threads start)");
}

}  // namespace search_context_builder_detail

SearchContext SearchContextBuilder::Build(
    std::string_view query,
    std::string_view path_query,
    const std::vector<std::string>* extensions,
    bool folders_only,
    bool case_sensitive,
    const std::atomic<bool>* cancel_flag) {
  SearchContext context;

  context.filename_query = query;
  context.path_query = path_query;
  context.case_sensitive = case_sensitive;
  context.folders_only = folders_only;
  context.cancel_flag = cancel_flag;

  if (!case_sensitive) {
    context.filename_query_lower = ToLower(query);
    context.path_query_lower = ToLower(path_query);
  }

  auto pattern_type = search_pattern_utils::DetectPatternType(query);
  context.use_glob = (pattern_type == search_pattern_utils::PatternType::Glob);

  if (!path_query.empty()) {
    auto path_pattern_type = search_pattern_utils::DetectPatternType(path_query);
    context.use_glob_path =
        (path_pattern_type == search_pattern_utils::PatternType::Glob);
  }

  const bool has_extension_filter =
      (extensions != nullptr && !extensions->empty());
  context.extension_only_mode =
      query.empty() && path_query.empty() && has_extension_filter;

  if (has_extension_filter && extensions != nullptr) {
    context.extensions = *extensions;
    context.PrepareExtensionSet();
  }

  if (!context.extension_only_mode) {
    search_context_builder_detail::CompilePatternIfNeeded(
        query, case_sensitive, context.filename_pattern, "filename");
    search_context_builder_detail::CompilePatternIfNeeded(
        path_query, case_sensitive, context.path_pattern, "path");
  }

  return context;
}
