#pragma once

#include <string>
#include <string_view>
#include <vector>

// Forward declarations
struct SearchResult;
class FileIndex;

namespace search {
  /**
   * @brief Service for exporting search results to external formats (CSV)
   */
  class ExportSearchResultsService {
  public:
    /**
     * @brief Export search results to a CSV file
     *
     * Header: Filename,Size,Last Modified,Full Path,Extension
     *
     * @param results Search results to export
     * @param file_index File index for loading metadata
     * @param output_path Path to save the CSV file
     * @return true on success, false on failure
     */
    static bool ExportToCsv(const std::vector<SearchResult>& results,
                            const FileIndex& file_index,
                            std::string_view output_path);

    /**
     * @brief Escape a string for CSV format
     *
     * Wraps in quotes if contains comma, newline, or quotes.
     * Escapes internal quotes as double-quotes.
     *
     * @param str String to escape
     * @return Escaped string
     */
    static std::string EscapeCsv(std::string_view str);
  };
} // namespace search
