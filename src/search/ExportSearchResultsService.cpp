#include "search/ExportSearchResultsService.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "index/FileIndex.h"
#include "search/SearchResultUtils.h"
#include "search/SearchTypes.h"
#include "utils/Logger.h"

namespace search {

bool ExportSearchResultsService::ExportToCsv(const std::vector<SearchResult>& results,
                                              const FileIndex& file_index,
                                              std::string_view output_path) {
  const ScopedTimer timer("Export CSV");
  if (output_path.empty()) {
    LOG_ERROR("ExportToCsv called with empty output path");
    return false;
  }

  const std::string path_str(output_path);
  std::ofstream file(path_str, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    LOG_ERROR("Failed to open file for CSV export: " + path_str);
    return false;
  }

  // Write header
  file << "Filename,Size,Last Modified,Full Path,Extension\n";

  for (const auto& result : results) {
    // Ensure metadata is loaded for the result
    EnsureDisplayStringsPopulated(result, file_index);

    file << EscapeCsv(result.GetFilename()) << ",";
    file << EscapeCsv(result.fileSizeDisplay) << ",";
    file << EscapeCsv(result.lastModificationDisplay) << ",";
    file << EscapeCsv(result.fullPath) << ",";
    file << EscapeCsv(result.GetExtension()) << "\n";
  }

  LOG_INFO("Exported " + std::to_string(results.size()) + " results to " + std::string(output_path));
  return true;
}

std::string ExportSearchResultsService::EscapeCsv(std::string_view str) {
  if (str.empty()) {
    return "";
  }

  bool needs_quotes = false;
  if (str.find(',') != std::string_view::npos ||
      str.find('\n') != std::string_view::npos ||
      str.find('\r') != std::string_view::npos ||
      str.find('"') != std::string_view::npos) {
    needs_quotes = true;
  }

  // Prevent formula injection by checking the first character
  // If it starts with =, +, -, or @, we prepend a single quote and force double quotes
  bool needs_formula_escape = false;
  if (const char first_char = str[0];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - str is non-empty: early return for empty str at line 49 above
      first_char == '=' || first_char == '+' || first_char == '-' || first_char == '@') {
    needs_formula_escape = true;
  }

  if (!needs_quotes && !needs_formula_escape) {
    return std::string(str);
  }

  std::string escaped = "\"";
  if (needs_formula_escape) {
    escaped += "'";
  }

  for (const char c : str) {
    if (c == '"') {
      escaped += "\"\"";
    } else {
      escaped += c;
    }
  }
  escaped += "\"";
  return escaped;
}

} // namespace search
