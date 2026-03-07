/**
 * @file ui/ResultsTable.cpp
 * @brief Implementation of search results table rendering component
 */

#include "ui/ResultsTable.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include "platform/windows/ShellContextUtils.h"  // PinToQuickAccess, HWND (used below for Quick Access shortcut)
#endif  // _WIN32

#include "gui/GuiState.h"
#include "gui/ImGuiUtils.h"
#include "imgui.h"
#include "index/FileIndex.h"
#include "path/PathUtils.h"
#include "platform/FileOperations.h"  // Common interface header (platform-agnostic)
#include "search/SearchResultUtils.h"
#include "search/SearchResultsService.h"
#include "ui/IconsFontAwesome.h"
#include "ui/IncrementalSearchState.h"
#include "ui/ResultsTableKeyboard.h"
#include "ui/Theme.h"
#include "ui/UiStyleGuards.h"
#include "utils/ClipboardUtils.h"
#include "utils/FileSystemUtils.h"
#include "utils/Logger.h"
#include "utils/ThreadPool.h"

#if defined(_WIN32) || defined(__APPLE__)
#include "platform/windows/DragDropUtils.h"
#endif  // defined(_WIN32) || defined(__APPLE__)

namespace ui {

namespace {

using detail::MultiStyleColorGuard;

// Local alias for folder statistics cache stored in GuiState.
using FolderStats = GuiState::FolderStats;
using FolderStatsMap = std::unordered_map<std::string, FolderStats>;

// Debounce for context menu to prevent rapid open (ms).
constexpr int kContextMenuDebounceMs = 300;
static_assert(kContextMenuDebounceMs > 0, "debounce must be positive");

// Renders the results table header row; when streaming is true, header text uses Warning color.
void RenderTableHeadersRow(bool streaming) {
  if (streaming) {
    const MultiStyleColorGuard hdr({
        {ImGuiCol_TableHeaderBg, Theme::AccentTint(Theme::HeaderAlphas::TableHeader)},
        {ImGuiCol_Text, Theme::Colors::Warning},
    });
    ImGui::TableHeadersRow();
  } else {
    const MultiStyleColorGuard hdr(
        {{ImGuiCol_TableHeaderBg, Theme::AccentTint(Theme::HeaderAlphas::TableHeader)}});
    ImGui::TableHeadersRow();
  }
}

// Helper function to calculate display path with caching and truncation
// Reduces cognitive complexity of RenderPathColumnWithEllipsis
// Inlined for performance (hot path: called for every visible row)
inline const char* CalculateDisplayPath(
  SearchResult& result,  // NOLINT(readability-non-const-parameter) - Must modify result for caching
  std::string_view directory_path, float max_width) {
  // Check if we have a valid cached truncated path
  // Cache is valid if:
  // 1. Column width hasn't changed significantly (within 5 pixels tolerance)
  // 2. Cached path is not empty (was calculated previously)
  // 3. Column width was set (not -1)
  // NOLINTNEXTLINE(readability-identifier-naming) - Local constant follows project convention with
  // k prefix. Using F suffix for float literals is acceptable
  constexpr float kWidthTolerance = 5.0F;
  if (const bool cache_valid =  // NOLINT(cppcoreguidelines-init-variables) - Variable initialized
                                // in C++17 init-statement
      result.truncatedPathColumnWidth >= 0.0F &&
      std::fabs(result.truncatedPathColumnWidth - max_width) < kWidthTolerance &&
      !result.truncatedPathDisplay.empty();
      cache_valid) {
    // Use cached truncated path (fast path - no CalcTextSize calls)
    return result.truncatedPathDisplay.c_str();
  }

  // Handle root files (no directory)
  if (directory_path.empty()) {
    // Cache empty string to avoid recalculating
    result.truncatedPathDisplay.clear();
    result.truncatedPathColumnWidth = max_width;
    return "";  // Root file, no directory to display
  }

  // Use thread-local buffer for directory path (same pattern as filename/extension columns)
  static thread_local std::array<char, 512>
    dir_path_buffer;  // Thread-local buffer for performance-critical UI rendering (hot path)

  // Quick check: if directory path fits, use it directly (most common case)
  // Convert string_view to null-terminated string for ImGui
  if (directory_path.size() >= dir_path_buffer.size()) {
    // Very long directory path - need to truncate (convert to string for truncation)
    result.truncatedPathDisplay =
      ResultsTable::TruncatePathAtBeginning(directory_path, max_width);
    result.truncatedPathColumnWidth = max_width;
    return result.truncatedPathDisplay.c_str();
  }

  std::memcpy(dir_path_buffer.data(), directory_path.data(), directory_path.size());
  dir_path_buffer.at(directory_path.size()) = '\0';
  const char* dir_path_cstr = dir_path_buffer.data();

  if (const ImVec2 dir_path_size = ImGui::CalcTextSize(dir_path_cstr);
      dir_path_size.x <= max_width) {  // Use init-statement pattern
    // Mark cache as "fits without truncation" by storing directory path
    result.truncatedPathDisplay = std::string(directory_path);
    result.truncatedPathColumnWidth = max_width;
    return dir_path_cstr;
  }

  // Directory path doesn't fit, need to truncate
  result.truncatedPathDisplay =
    ResultsTable::TruncatePathAtBeginning(directory_path, max_width);
  result.truncatedPathColumnWidth = max_width;
  return result.truncatedPathDisplay.c_str();
}

// Helper function to render path tooltip with line breaks
// Reduces cognitive complexity of RenderPathColumnWithEllipsis
// Inlined for performance (called on hover, but tooltip rendering is still performance-sensitive)
inline void RenderPathTooltip(std::string_view full_path) {
  ImGui::BeginTooltip();

  // Render path with manual line breaks after separators to avoid string allocation
  const char* start = full_path.data();
  const char* end = start + full_path.size();  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                                               // - ImGui text buffer pattern
  const char* current = start;

  for (const char* p = start; p < end; ++p) {  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) - ImGui text buffer pattern
    if ((*p == '\\' || *p == '/') && (p + 1 < end)) {  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) - buffer bounds check
      ImGui::TextUnformatted(current, p + 1);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) - buffer segment
      current = p + 1;  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) - buffer advance
    }
  }
  // Render remaining part
  if (current < end) {
    ImGui::TextUnformatted(current, end);
  }

  ImGui::EndTooltip();
}

// HandleTableSorting moved to SearchResultsService

/**
 * @brief Parameters for rendering a results table row
 */
struct RenderRowParams {
  const SearchResult& result;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) -
                               // parameter struct, references avoid copy
  GuiState& state;             // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  FileIndex& file_index;       // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  NativeWindowHandle native_window{};
  GLFWwindow* glfw_window = nullptr;
  bool has_pending_deletions = false;
  bool show_path_hierarchy_indentation = true;
  const FolderStatsMap* folder_stats = nullptr;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  bool& drag_candidate_active;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  int& drag_candidate_row;      // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  ImVec2& drag_start_pos;       // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  bool& drag_started;           // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

/**
 * @brief Parameters for rendering visible table rows
 * Grouped to reduce function parameter count
 */
struct RenderVisibleRowsParams {
  const std::vector<SearchResult>& display_results;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  GuiState& state;        // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  FileIndex& file_index;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  NativeWindowHandle native_window{};
  GLFWwindow* glfw_window = nullptr;
  bool has_pending_deletions = false;
  bool show_path_hierarchy_indentation = true;
  const FolderStatsMap* folder_stats = nullptr;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  bool& drag_candidate_active;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  int& drag_candidate_row;      // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  ImVec2& drag_start_pos;       // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  bool& drag_started;           // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

/**
 * @brief Render a single row in the results table
 * Extracted to reduce nesting depth in Render()
 */
void RenderResultsTableRow(int row, const RenderRowParams& params);

/**
 * @brief Render all visible rows in the results table using ImGui clipper
 * Extracted to reduce nesting depth in Render()
 */
inline void RenderVisibleTableRows(const RenderVisibleRowsParams& params) {
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(
    params.display_results.size()));  // NOSONAR(cpp:S1905) - Required cast: ImGui API expects int
  if (params.state.scrollToSelectedRow && params.state.selectedRow >= 0 &&
      params.state.selectedRow < static_cast<int>(params.display_results.size())) {
    clipper.IncludeItemByIndex(params.state.selectedRow);
  }
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
      const auto& result = params.display_results[static_cast<size_t>(
        row)];  // NOSONAR(cpp:S1905) - Required cast for array indexing

      const RenderRowParams row_params{result,
                                       params.state,
                                       params.file_index,
                                       params.native_window,
                                       params.glfw_window,
                                       params.has_pending_deletions,
                                       params.show_path_hierarchy_indentation,
                                       params.folder_stats,
                                       params.drag_candidate_active,
                                       params.drag_candidate_row,
                                       params.drag_start_pos,
                                       params.drag_started};
      RenderResultsTableRow(row, row_params);
    }
  }
}

/**
 * @brief Get the folder depth of a search result for hierarchy indentation
 *
 * Counts path segments in the directory path. Root files (no directory) return 0.
 * E.g., "docs/review/2026-02-14-v2" -> 3, "docs" -> 1, "" -> 0.
 */
[[nodiscard]] inline int GetPathDepth(const SearchResult& result) {
  const std::string_view dir_path = ResultsTable::GetDirectoryPath(result);
  if (dir_path.empty()) {
    return 0;
  }
  int count = 1;
  for (const char c : dir_path) {
    if (c == '/' || c == '\\') {
      ++count;
    }
  }
  return count;
}

// Returns true if the existing folder statistics cache parameters still match the
// current display context (batch number, filters, and streaming state).
[[nodiscard]] bool FolderStatsParametersMatch(const GuiState& state) {
  return state.folderStatsValid &&
         state.folderStatsResultsBatchNumber == state.resultsBatchNumber &&
         state.folderStatsTimeFilter == state.timeFilter &&
         state.folderStatsSizeFilter == state.sizeFilter &&
         state.folderStatsShowingPartialResults == state.showingPartialResults;
}

// Accumulates folder statistics for a single file result by walking its directory
// ancestry and updating the per-folder counters. Directory results are ignored by
// the caller to keep this helper focused on files only.
void AccumulateFolderStatsForFile(GuiState& state, const SearchResult& result) {
  const uint64_t file_size = result.fileSize;
  const bool size_loaded = (file_size != kFileSizeNotLoaded && file_size != kFileSizeFailed);
  const uint64_t contributing_size = size_loaded ? file_size : 0U;

  std::string_view dir_path = ResultsTable::GetDirectoryPath(result);
  while (!dir_path.empty()) {
    FolderStats& stats = state.folderStatsByPath[std::string(dir_path)];
    ++stats.fileCount;
    stats.totalSizeBytes += contributing_size;

    const size_t last_separator = dir_path.find_last_of("\\/");
    if (last_separator == std::string_view::npos) {
      break;
    }
    dir_path = dir_path.substr(0, last_separator);
  }
}

// Build or refresh the folder statistics cache on GuiState when needed.
// Uses the current display_results vector as the source, and only recomputes
// when search results or filters have changed to avoid extra O(N) work per frame.
void BuildFolderStatsIfNeeded(GuiState& state, const std::vector<SearchResult>& display_results) {
  if (FolderStatsParametersMatch(state)) {
    return;
  }

  state.folderStatsByPath.clear();

  for (const auto& result : display_results) {
    if (!result.isDirectory) {
      AccumulateFolderStatsForFile(state, result);
    }
  }

  state.folderStatsValid = true;
  state.folderStatsResultsBatchNumber = state.resultsBatchNumber;
  state.folderStatsTimeFilter = state.timeFilter;
  state.folderStatsSizeFilter = state.sizeFilter;
  state.folderStatsShowingPartialResults = state.showingPartialResults;
}

// Returns C string for filename column display (name + extension); uses thread-local buffer when possible.
[[nodiscard]] const char* GetRowFilenameCstr(const SearchResult& result,
                                             std::array<char, 512>& filename_buffer) {
  const std::string_view base_name = result.GetFilename();
  const std::string_view extension = result.GetExtension();

  // Fallback: if we somehow don't have a filename or extension offset, use full path storage.
  if (base_name.empty() && extension.empty()) {
    return result.fullPath.data();  // Pool stores path + '\0', so data() is null-terminated
  }

  // Compose "name.ext" when an extension is present; otherwise just the base name.
  const bool has_extension = !extension.empty();
  const size_t max_len = filename_buffer.size() - 1U;  // Reserve 1 for null terminator
  size_t write_len = 0;

  if (!base_name.empty()) {
    const size_t copy_len = (std::min)(base_name.size(), max_len);
    for (size_t i = 0; i < copy_len; ++i) {
      filename_buffer.at(i) = base_name[i];
    }
    write_len += copy_len;
  }

  if (has_extension && write_len < max_len) {
    filename_buffer.at(write_len) = '.';
    ++write_len;
    const size_t remaining = max_len - write_len;
    if (remaining > 0U) {
      const size_t ext_copy_len = (std::min)(extension.size(), remaining);
      for (size_t i = 0; i < ext_copy_len; ++i) {
        filename_buffer.at(write_len + i) = extension[i];
      }
      write_len += ext_copy_len;
    }
  }

  filename_buffer.at(write_len) = '\0';
  return filename_buffer.data();
}

/**
 * @brief Render a single row in the results table
 * Extracted to reduce nesting depth in Render()
 */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Complex but cohesive UI row rendering; further splitting would hurt locality and clarity
void RenderResultsTableRow(int row, const RenderRowParams& params) {  // NOSONAR(cpp:S3776) - Cohesive row rendering; splitting would fragment locality
  // Ensure display strings are populated for this row (lazy loading).
  // During streaming, skip sync disk I/O to keep UI responsive (cloud files can block 100-500ms each).
  EnsureDisplayStringsPopulated(params.result, params.file_index,
                               !params.state.showingPartialResults);

  ImGui::PushID(row);
  ImGui::TableNextRow();

  // Alternating row background for easier scanning (subtle tint; selected/hover still visible)
  const bool is_even = (row % 2 == 0);
  // Use slightly lighter surface for even rows (alpha 0.3); odd rows use default table bg
  ImVec4 surface_hover_alpha = Theme::Colors::SurfaceHover;
  constexpr float kRowStripeAlpha = 0.3F;
  surface_hover_alpha.w = kRowStripeAlpha;
  const ImU32 row_bg_color = is_even ? ImGui::GetColorU32(surface_hover_alpha) : 0;
  // Note: 0 alpha means transparent, using default table bg.
  // Even rows use SurfaceHover with reduced alpha for subtle striping.

  // If marked, use a subtle accent tint for the background
  ImU32 final_row_bg_color = row_bg_color;
  const bool is_marked = (params.state.markedFileIds.find(params.result.fileId) != params.state.markedFileIds.end());
  if (is_marked) {
    ImVec4 marked_bg = Theme::Colors::Accent;
    constexpr float kMarkedRowAlpha = 0.1F;
    marked_bg.w = kMarkedRowAlpha;  // Very subtle accent tint
    final_row_bg_color = ImGui::GetColorU32(marked_bg);
  }

  // Actually, standard ImGui Table striping is often enough if ImGuiTableFlags_RowBg is set.
  // But let's respect manual coloring.
  if (final_row_bg_color != 0) {
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, final_row_bg_color);
  }

  const bool is_selected = (params.state.selectedRow == row);

  // Column 0: Mark (dired-style)
  ImGui::TableSetColumnIndex(ResultColumn::Mark);
  ImGui::PushID(ResultColumn::Mark);
  // Use unique hidden ID (##mark) to avoid collisions
  if (const char* mark_label = is_marked ? "*##mark" : " ##mark";
      ImGui::Selectable(mark_label, is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
    params.state.selectedRow = row;
    if (is_marked) {
      params.state.markedFileIds.erase(params.result.fileId);
    } else {
      params.state.markedFileIds.insert(params.result.fileId);
    }
  }
  ImGui::PopID();

  // Set text color
  if (const bool is_pending_delete =
        params.has_pending_deletions &&
        (params.state.pendingDeletions.find(params.result.fullPath) != params.state.pendingDeletions.end());
      !is_pending_delete) {
    if (is_selected) {
      ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::Accent);  // Highlight selected text
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::Text);
    }
  } else {
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDisabled);
  }

  // Column 0: Name (filename or folder name, with optional path hierarchy indentation - Phase 1 of Path Hierarchy Visualization)
  ImGui::TableSetColumnIndex(ResultColumn::Filename);
  const int path_depth =
    params.show_path_hierarchy_indentation ? GetPathDepth(params.result) : 0;
  constexpr float kIndentPerLevel = 14.0F;
  if (path_depth > 0) {
    ImGui::Indent(static_cast<float>(path_depth) * kIndentPerLevel);
  }
  ImGui::PushID(ResultColumn::Filename);
  static thread_local std::array<char, 512> filename_buffer;
  if (const char* filename_cstr = GetRowFilenameCstr(params.result, filename_buffer);  // NOLINT(cppcoreguidelines-init-variables) - initialized by GetRowFilenameCstr
      ImGui::Selectable(filename_cstr, is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
    params.state.selectedRow = row;
  }
  // Initiate potential drag from filename column.
  if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    params.drag_candidate_active = true;
    params.drag_candidate_row = row;
    params.drag_start_pos = ImGui::GetIO().MousePos;
    params.drag_started = false;
  }
  // Double-click to open file with default application
  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
    file_operations::OpenFileDefault(params.result.fullPath);
  }
#ifdef _WIN32
  // Right-click context menu (Windows only)
  // Use IsMouseDown instead of IsMouseClicked for more reliable detection
  // IsMouseClicked only returns true for one frame, which can be missed if frame rate is low
  // Check both IsMouseDown and a flag to prevent multiple rapid opens (debouncing)
  if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
      !params.state.contextMenuOpen) {
    // Debounce: prevent opening context menu if one was recently opened
    const auto now = std::chrono::steady_clock::now();
    const auto time_since_last =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - params.state.lastContextMenuTime)
        .count();
    if (time_since_last >= kContextMenuDebounceMs) {
      LOG_INFO_BUILD("Opening context menu for: " << params.result.fullPath);
      // Get mouse position in screen coordinates directly for TrackPopupMenu
      // TrackPopupMenu expects screen coordinates, so use GetCursorPos
      POINT pt;
      GetCursorPos(&pt);  // Get screen coordinates directly
      params.state.contextMenuOpen = true;
      params.state.lastContextMenuTime = now;
      ShowContextMenu(params.native_window, params.result.fullPath, pt);
      // ShowContextMenu handles converting NativeWindowHandle to HWND internally.
    }
  }
  // Reset context menu flag when right mouse button is released
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
    params.state.contextMenuOpen = false;
  }
#endif  // _WIN32
  // Ctrl+C (Windows) or Cmd+C (macOS) to copy filename (all platforms)
  if (ImGui::IsItemHovered() && IsCopyShortcutPressed()) {
    const char* filename_cstr = GetRowFilenameCstr(params.result, filename_buffer);
    clipboard_utils::SetClipboardText(params.glfw_window, filename_cstr);
  }
  ImGui::PopID();
  if (path_depth > 0) {
    ImGui::Unindent(static_cast<float>(path_depth) * kIndentPerLevel);
  }

  // Column 1: Size
  ImGui::TableSetColumnIndex(ResultColumn::Size);
  ImGui::PushID(ResultColumn::Size);
  if (ImGui::Selectable(ResultsTable::GetSizeDisplayText(params.result), is_selected, 0)) {
    params.state.selectedRow = row;
  }
  ImGui::PopID();

  // Column 2: Last Modified
  ImGui::TableSetColumnIndex(ResultColumn::LastModified);
  ImGui::PushID(ResultColumn::LastModified);
  if (ImGui::Selectable(ResultsTable::GetModTimeDisplayText(params.result), is_selected, 0)) {
    params.state.selectedRow = row;
  }
  ImGui::PopID();

  // Column 3: Full Path (shows directory path only, without filename, to avoid duplication)
  ImGui::TableSetColumnIndex(ResultColumn::FullPath);
  // Calculate column width dynamically inside the cell context.
  // This is the correct way to get the width in ImGui Tables.
  // NOLINTNEXTLINE(readability-identifier-naming) - Local constant follows project convention with
  // k prefix. Using F suffix for float literals is acceptable
  constexpr float kPathWidthPadding = 10.0F;
  const float current_path_column_width = ImGui::GetContentRegionAvail().x - kPathWidthPadding;

  ImGui::PushID(ResultColumn::FullPath);
  ResultsTable::RenderPathColumnWithEllipsis(params.result, is_selected, row, params.state,
                                             params.native_window, params.glfw_window,
                                             current_path_column_width);
  // Initiate potential drag from full path column.
  if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    params.drag_candidate_active = true;
    params.drag_candidate_row = row;
    params.drag_start_pos = ImGui::GetIO().MousePos;
    params.drag_started = false;
  }
  ImGui::PopID();

  // Column 4: Extension
  ImGui::TableSetColumnIndex(ResultColumn::Extension);
  ImGui::PushID(ResultColumn::Extension);
  // Use pre-extracted extension string_view to avoid allocation
  // Extensions are always small, use thread-local buffer
  static thread_local std::array<char, 64>
    ext_buffer;  // Thread-local buffer for performance-critical UI rendering (hot path)
  const char* ext_cstr = nullptr;  // Safe default, assigned in if-else below
  if (const std::string_view extension = params.result.GetExtension(); extension.empty()) {
    ext_cstr = "";
  } else if (extension.size() < ext_buffer.size()) {
    std::copy(extension.begin(), extension.end(), ext_buffer.begin());
    ext_buffer.at(extension.size()) = '\0';
    ext_cstr = ext_buffer.data();
  } else {
    // For very long extensions, truncate to buffer size to avoid allocation
    std::copy(extension.begin(), extension.begin() + (ext_buffer.size() - 1), ext_buffer.begin());
    ext_buffer.back() = '\0';
    ext_cstr = ext_buffer.data();
  }
  if (ImGui::Selectable(ext_cstr, is_selected, 0)) {
    params.state.selectedRow = row;
  }
  ImGui::PopID();

  // Optional folder statistics columns (recursive file count and total size).
  // Show values only for folder rows; leave cells empty for file rows.
  size_t folder_file_count = 0;
  uint64_t folder_total_size_bytes = 0;
  bool show_folder_stats_cells = false;  // true only for directory rows

  if (params.result.isDirectory && params.folder_stats != nullptr) {
    show_folder_stats_cells = true;
    const auto it = params.folder_stats->find(std::string(params.result.fullPath));
    if (it != params.folder_stats->end()) {
      folder_file_count = it->second.fileCount;
      folder_total_size_bytes = it->second.totalSizeBytes;
    }
  }

  ImGui::TableSetColumnIndex(ResultColumn::FolderFileCount);
  ImGui::PushID(ResultColumn::FolderFileCount);
  if (show_folder_stats_cells) {
    ImGui::Text("%zu", folder_file_count);  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API requires vararg
  }
  ImGui::PopID();

  ImGui::TableSetColumnIndex(ResultColumn::FolderTotalSize);
  ImGui::PushID(ResultColumn::FolderTotalSize);
  if (show_folder_stats_cells) {
    if (folder_total_size_bytes > 0U) {
      const std::string size_label = FormatFileSize(folder_total_size_bytes);
      ImGui::TextUnformatted(size_label.c_str());
    } else {
      ImGui::TextUnformatted("0 B");
    }
  }
  ImGui::PopID();

  // Handle scrolling to selected row if requested
  if (is_selected && params.state.scrollToSelectedRow) {
    ImGui::SetScrollHereY();
    params.state.scrollToSelectedRow = false;
  }

  ImGui::PopID();

  // Always pop the text color style (either high-contrast or grayed-out)
  ImGui::PopStyleColor();
}

/**
 * @brief Start file drag-drop if result is not a directory and not pending delete.
 * Extracted to keep EvaluateDragGesture nesting at most 3 levels (S134).
 */
void TryStartFileDragDropIfAllowed(const SearchResult& drag_result, const GuiState& state) {
  if (const bool is_pending_delete =
        (state.pendingDeletions.find(drag_result.fullPath) != state.pendingDeletions.end());
      drag_result.isDirectory || is_pending_delete) {
    return;
  }
#if defined(_WIN32) || defined(__APPLE__)
  drag_drop::StartFileDragDrop(drag_result.fullPath);
#endif  // defined(_WIN32) || defined(__APPLE__)
}

/**
 * @brief Handle drag-and-drop gesture evaluation
 *
 * Evaluates drag gesture and starts shell drag-drop if threshold is exceeded.
 * Updates drag state variables.
 *
 * @param drag_candidate_active Whether a drag candidate is active (modified)
 * @param drag_candidate_row Row index of drag candidate (modified)
 * @param drag_start_pos Starting position of drag (input)
 * @param drag_started Whether drag has started (modified)
 * @param drag_threshold Minimum movement in pixels to start drag
 * @param display_results Display results for drag target lookup
 * @param state GUI state for pending deletions check
 */
void HandleDragAndDrop(bool& drag_candidate_active, int& drag_candidate_row,
                       const ImVec2& drag_start_pos, bool& drag_started, float drag_threshold,
                       const std::vector<SearchResult>& display_results, const GuiState& state) {
  const ImGuiIO& imgui_io = ImGui::GetIO();
  if (drag_candidate_active) {
#if defined(_WIN32) || defined(__APPLE__)
    // Visual feedback: hand cursor while drag gesture is in progress (before and during drag).
    // Windows shows OLE default cursors once DoDragDrop runs; this covers the pre-threshold phase
    // and matches macOS behavior. Linux has no table drag-drop.
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
#endif  // defined(_WIN32) || defined(__APPLE__)
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      drag_candidate_active = false;
      drag_candidate_row = -1;
      drag_started = false;
    } else if (!drag_started) {
      auto delta =
        ImVec2(imgui_io.MousePos.x - drag_start_pos.x, imgui_io.MousePos.y - drag_start_pos.y);
      const bool over_threshold =  // NOLINT(cppcoreguidelines-init-variables) - initialized by expression
        std::fabs(delta.x) >= drag_threshold || std::fabs(delta.y) >= drag_threshold;
      const bool row_valid =
        drag_candidate_row >= 0 &&
        drag_candidate_row < static_cast<int>(display_results.size());  // NOSONAR(cpp:S1905) - Required cast for signed/unsigned comparison
      if (over_threshold && row_valid) {  // NOSONAR(cpp:S134) - Nesting required for drag gesture; extracted helper keeps table at 3 levels
        const auto& drag_result =
          display_results[static_cast<std::size_t>(drag_candidate_row)];  // NOSONAR(cpp:S1905) - Required cast for array indexing
        TryStartFileDragDropIfAllowed(drag_result, state);
        drag_started = true;
      }
    }
  } else {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      drag_started = false;
    }
  }
}

/**
 * @brief Handle delete key and confirmation popup
 *
 * Processes delete key press and renders confirmation modal.
 * Uses display_results so the delete target matches the visible table (streaming/filtered).
 *
 * @param state GUI state (modified by delete operations)
 * @param display_results Currently displayed results (same source as table)
 */
void HandleDeleteKeyAndPopup(GuiState& state, const std::vector<SearchResult>& display_results) {
  // Handle Delete Key: use display_results so we delete the row the user sees (streaming/filtered)
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      ImGui::IsKeyPressed(ImGuiKey_Delete) && state.selectedRow >= 0 &&
      state.selectedRow <
        static_cast<int>(
          display_results.size())) {  // NOSONAR(cpp:S1905) - Required cast for signed/unsigned
    state.fileToDelete = display_results[static_cast<std::size_t>(state.selectedRow)]
                           .fullPath;  // NOSONAR(cpp:S1905) - Required cast for array indexing
    state.showDeletePopup = true;
    LOG_INFO_BUILD("Opening delete popup for: " << state.fileToDelete);
  }

  if (state.showDeletePopup) {
    ImGui::OpenPopup("Confirm Delete");
    state.showDeletePopup = false;
  }

  // Confirmation Modal
  // Center popup in main window every time it appears
  const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  // NOLINTNEXTLINE(readability-identifier-naming) - Local constant follows project convention with
  // k prefix. Using F suffix for float literals is acceptable
  constexpr float kCenterFactor = 0.5F;
  ImGui::SetNextWindowPos(
    ImVec2(main_viewport->WorkPos.x + (main_viewport->WorkSize.x * kCenterFactor),
           main_viewport->WorkPos.y + (main_viewport->WorkSize.y * kCenterFactor)),
    ImGuiCond_Appearing, ImVec2(kCenterFactor, kCenterFactor));

  if (ImGui::BeginPopupModal("Confirm Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Are you sure you want to delete this file?");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::TextColored(Theme::Colors::Error, "%s", state.fileToDelete.c_str());  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Separator();

    constexpr float delete_cancel_button_width = 120.0F;
    if (ImGui::Button(ICON_FA_TRASH " Delete", ImVec2(delete_cancel_button_width, 0))) {
      // DeleteFileToRecycleBin is implemented on both Windows and macOS
      // Windows: Moves to Recycle Bin, macOS: Moves to Trash
      if (const bool deleted = file_operations::DeleteFileToRecycleBin(state.fileToDelete);
          deleted) {
        state.pendingDeletions.insert(state.fileToDelete);
      }
      state.selectedRow = -1;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK " Cancel", ImVec2(delete_cancel_button_width, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

// Returns marked IDs in deterministic order (by ID) for clipboard and bulk ops.
// hash_set_t iteration is unspecified; sorting restores repeatable, user-facing order.
std::vector<uint64_t> GetMarkedIdsInOrder(const GuiState& state) {
  std::vector<uint64_t> ids(state.markedFileIds.begin(), state.markedFileIds.end());
  std::sort(ids.begin(), ids.end());
  return ids;
}

void ExecuteBulkDelete(GuiState& state, const FileIndex& file_index) {
  const auto& path_accessor = file_index.GetPathAccessor();
  for (const uint64_t file_id : GetMarkedIdsInOrder(state)) {
    const std::string_view path = path_accessor.GetPathView(file_id);
    if (!path.empty() &&
        file_operations::DeleteFileToRecycleBin(std::string(path))) {
      state.pendingDeletions.emplace(path);
    }
  }
  state.markedFileIds.clear();
  state.selectedRow = -1;
  ImGui::CloseCurrentPopup();
}

void HandleBulkDeletePopup(GuiState& state, const FileIndex& file_index) {
  if (state.showBulkDeletePopup) {
    ImGui::OpenPopup("Confirm Bulk Delete");
    state.showBulkDeletePopup = false;
  }

  const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  constexpr float kCenterFactor = 0.5F;
  auto center =
    ImVec2(main_viewport->WorkPos.x + (main_viewport->WorkSize.x * kCenterFactor),
           main_viewport->WorkPos.y + (main_viewport->WorkSize.y * kCenterFactor));
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(kCenterFactor, kCenterFactor));

  if (ImGui::BeginPopupModal("Confirm Bulk Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const size_t marked_count = state.markedFileIds.size();

    const std::string delete_message =
      "Are you sure you want to delete " + std::to_string(marked_count) + " marked files?";
    ImGui::TextUnformatted(delete_message.c_str());

    {
      const detail::StyleColorGuard error_color_guard(ImGuiCol_Text, Theme::Colors::Error);
      ImGui::TextUnformatted("This action will move them to the Recycle Bin.");
    }
    ImGui::Separator();

    constexpr float delete_cancel_button_width = 120.0F;
    if (ImGui::Button(ICON_FA_TRASH " Delete All", ImVec2(delete_cancel_button_width, 0))) {
      ExecuteBulkDelete(state, file_index);
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK " Cancel", ImVec2(delete_cancel_button_width, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

}  // namespace

bool ResultsTable::RenderPathColumnWithEllipsis(const SearchResult& result, bool is_selected,
                                                int row, GuiState& state,
                                                [[maybe_unused]] NativeWindowHandle native_window,
                                                GLFWwindow* glfw_window, float max_width) {
  // max_width is pre-calculated and passed in to avoid repeated GetColumnWidth calls
  // This improves performance when clipping is active (only visible rows are rendered)

  // Extract directory path (no allocation, just string_view)
  // This avoids showing filename in Full Path column since it's already in Filename column
  const std::string_view directory_path = GetDirectoryPath(result);

  // Calculate display path with caching and truncation (extracted to reduce complexity)
  const char* display_path_cstr = CalculateDisplayPath(  // NOLINT(cppcoreguidelines-init-variables)
    const_cast<SearchResult&>(result), directory_path, max_width);  // NOSONAR(cpp:S859) NOLINT(cppcoreguidelines-pro-type-const-cast) - caching optimization

  bool selection_changed = false;
  if (ImGui::Selectable(display_path_cstr, is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
    state.selectedRow = row;
    selection_changed = true;
  }

  // Tooltip with full path (only allocated when hovered, which is rare)
  if (ImGui::IsItemHovered()) {
    RenderPathTooltip(result.fullPath);
  }

  // Double-click to open parent folder
  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
    file_operations::OpenParentFolder(result.fullPath);
  }

// Right-click context menu (Windows only)
// Use same improved detection as filename column (see above for details)
#ifdef _WIN32
  if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
      !state.contextMenuOpen) {
    // Debounce: prevent opening context menu if one was recently opened
    const auto now = std::chrono::steady_clock::now();
    const auto time_since_last =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - state.lastContextMenuTime)
        .count();
    if (time_since_last >= kContextMenuDebounceMs) {
      LOG_INFO_BUILD("Opening context menu for: " << result.fullPath);
      // Get mouse position in screen coordinates directly for TrackPopupMenu;
      // it expects screen coordinates, so use GetCursorPos here.
      POINT pt;
      GetCursorPos(&pt);  // Get screen coordinates directly
      state.contextMenuOpen = true;
      state.lastContextMenuTime = now;
      ShowContextMenu(native_window, result.fullPath, pt);
    }
  }
  // Reset context menu flag when right mouse button is released
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
    state.contextMenuOpen = false;
  }
#endif  // _WIN32

  // Ctrl+C (Windows) or Cmd+C (macOS) to copy full path
  if (ImGui::IsItemHovered() && IsCopyShortcutPressed()) {
    file_operations::CopyPathToClipboard(glfw_window, result.fullPath);
  }

  return selection_changed;
}

const char* ResultsTable::GetSizeDisplayText(const SearchResult& result) {
  if (result.isDirectory) {
    return "Folder";
  }

  if (result.fileSize == kFileSizeNotLoaded) {
    return "...";  // Loading indicator (not stored in cache)
  }

  if (result.fileSize == kFileSizeFailed) {
    return "N/A";  // Access denied or protected file
  }

  // Return cached display string (should be populated by
  // EnsureDisplayStringsPopulated)
  // Fallback: if display string is empty but size is loaded, format it now
  if (result.fileSizeDisplay.empty() && result.fileSize != kFileSizeNotLoaded &&
      result.fileSize != kFileSizeFailed) {
    result.fileSizeDisplay = FormatFileSize(result.fileSize);
  }

  return result.fileSizeDisplay.c_str();
}

const char* ResultsTable::GetModTimeDisplayText(const SearchResult& result) {
  if (result.isDirectory) {
    return "Folder";
  }

  if (IsSentinelTime(result.lastModificationTime)) {
    return "...";  // Loading indicator (not stored in cache)
  }

  if (IsFailedTime(result.lastModificationTime)) {
    return "N/A";  // Access denied or protected file
  }

  // Return cached display string (should be populated by
  // EnsureDisplayStringsPopulated)
  // Fallback: if display string is empty but time is loaded, format it now
  if (result.lastModificationDisplay.empty()) {
    result.lastModificationDisplay = FormatFileTime(result.lastModificationTime);
  }

  return result.lastModificationDisplay.c_str();
}

void RenderMarkedActionsToolbar(GuiState& state,
                                GLFWwindow* glfw_window,
                                const FileIndex& file_index,
                                [[maybe_unused]] bool shift) {
  const size_t marked_count = state.markedFileIds.size();
  if (marked_count == 0) {
    return;
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  {
    const detail::StyleColorGuard accent_color_guard(ImGuiCol_Text, Theme::Colors::Accent);
    const std::string marked_label =
      std::string(ICON_FA_CHECK_DOUBLE " ") + std::to_string(marked_count) + " items marked";
    ImGui::TextUnformatted(marked_label.c_str());
  }
  ImGui::SameLine();

  // Keyboard shortcut for copying marked paths is handled in HandleResultsTableKeyboardShortcuts
  if (ImGui::Button(ICON_FA_COPY " Copy Paths")) {
    CopyMarkedPathsToClipboard(state, glfw_window, file_index);
  }
  ImGui::SameLine();

  // Keyboard shortcut for bulk delete is handled in HandleResultsTableKeyboardShortcuts
  if (ImGui::Button(ICON_FA_TRASH " Delete Marked")) {
    state.showBulkDeletePopup = true;
  }
  ImGui::SameLine();

  if (ImGui::Button(ICON_FA_XMARK " Unmark All")) {
    state.markedFileIds.clear();
  }
}

std::string ResultsTable::TruncatePathAtBeginning(std::string_view path, float max_width) {
  // Create a text width calculator that uses ImGui
  const path_utils::TextWidthCalculator imgui_calc = [](std::string_view text) {
    const std::string text_copy{text};
    const ImVec2 size = ImGui::CalcTextSize(text_copy.c_str());
    return size.x;
  };

  return path_utils::TruncatePathAtBeginning(path, max_width, imgui_calc);
}

// Renders the visual area above the table: inline filter prompt, filter status, marked actions,
// or status label. Extracted to reduce cognitive complexity of Render() (Sonar S3776).
void RenderResultsTableHeaderArea(IncrementalSearchState& incremental_search,
                                 GuiState& state,
                                 GLFWwindow* glfw_window,
                                 FileIndex& file_index,
                                 bool results_shortcuts_active,
                                 bool shift) {
  if (incremental_search.IsPromptVisible()) {
    RenderIncrementalSearchPrompt(incremental_search);
  } else if (incremental_search.IsFilterActive()) {
    RenderIncrementalSearchFilterStatus(incremental_search);
  } else if (!state.markedFileIds.empty()) {
    RenderMarkedActionsToolbar(state, glfw_window, file_index, shift);
  } else if (results_shortcuts_active) {
    const detail::StyleColorGuard text_color_guard(ImGuiCol_Text, Theme::Colors::Accent);
#ifdef __APPLE__
    ImGui::TextUnformatted(
      "Results table active - arrows/N/P navigate; '/' starts Filter in results; Esc "
      "clears filters; Cmd+G cancels the filter; Ctrl+Shift+F toggles Matched Files/Matched Size columns (see Help).");
#else
    ImGui::TextUnformatted(
      "Results table active - arrows/N/P navigate; '/' starts Filter in results; Esc "
      "clears filters; Ctrl+G cancels the filter; Ctrl+Shift+F toggles Matched Files/Matched Size columns (see Help).");
#endif  // __APPLE__
  } else {
    const detail::StyleColorGuard text_color_guard(ImGuiCol_Text, Theme::Colors::TextDim);
#ifdef __APPLE__
    ImGui::TextUnformatted(
      "Click inside the results table to enable shortcuts - arrows/N/P navigate; "
      "'/' starts Filter in results; Esc clears filters; Cmd+G cancels the filter.");
#else
    ImGui::TextUnformatted(
      "Click inside the results table to enable shortcuts - arrows/N/P navigate; "
      "'/' starts Filter in results; Esc clears filters; Ctrl+G cancels the filter.");
#endif  // __APPLE__
  }
  ImGui::Spacing();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Complex but cohesive UI rendering; splitting further would harm locality and clarity
void ResultsTable::Render(
  GuiState& state,
  NativeWindowHandle
    native_window,
  GLFWwindow* glfw_window, ThreadPool& thread_pool, FileIndex& file_index,
  bool show_path_hierarchy_indentation) {
  // Drag-and-drop gesture tracking (per-frame static state).
  // These variables persist across frames to track drag gestures.
  static bool drag_candidate_active = false;
  static int drag_candidate_row = -1;
  static auto drag_start_pos = ImVec2(0.0F, 0.0F);
  static bool drag_started = false;
  const float drag_threshold = 5.0F;  // pixels - minimum movement to start drag

  static IncrementalSearchState incremental_search;

  // So ApplicationLogic skips "Escape: Clear all filters" only while the inline "Filter in results" prompt is visible.
  // After Accept (prompt hidden, filter may still be active), Esc should resume normal Clear All.
  state.incrementalSearchActive = incremental_search.IsPromptVisible();

  // Table fills remaining content area. (Reserved footer space for a "partial results" line
  // was planned per
  // Show results if search is active OR if we have results to display.
  // When streaming (!resultsComplete && showingPartialResults), table header text uses Warning
  // color and status bar shows "Displayed: N (no filters, no sort yet)".
  if (state.searchActive || !state.searchResults.empty() || !state.partialResults.empty()) {
    // CRITICAL: Check for async sorting completion BEFORE filter/sort so display reflects sorted
    // data
    search::SearchResultsService::CheckAndCompleteAsyncSort(state, file_index, thread_pool);

    const ImGuiIO& io = ImGui::GetIO();
    const bool shift = io.KeyShift;

    // Visual area above the table: either inline filter prompt, filter status, marked actions, or the status label.
    const bool results_shortcuts_active =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput;
    {
      RenderResultsTableHeaderArea(incremental_search, state, glfw_window, file_index,
                                  results_shortcuts_active, shift);
    }

    // Ensure filter caches are up to date (before table so sort can use them)
    search::SearchResultsService::UpdateFilterCaches(state, file_index, thread_pool);

    float table_scroll_y_for_search = 0.0F;

    if (constexpr int kResultColumnCount = 8;
        ImGui::BeginTable("SearchResults", kResultColumnCount,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |  // NOLINT(hicpp-signed-bitwise) - ImGui flags bitmask
                            ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
                            ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable,
                          ImVec2(0.0F, 0.0F))) {
      // Use 0.0F width with WidthFixed to let ImGui autosize to label width ("Mark")
      ImGui::TableSetupColumn("Mark", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 0.0F);
      ImGui::TableSetupColumn("Name");
      ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_PreferSortDescending);
      ImGui::TableSetupColumn("Last Modified", ImGuiTableColumnFlags_PreferSortDescending);
      constexpr float full_path_column_default_width = 600.0F;
      ImGui::TableSetupColumn("Full Path", ImGuiTableColumnFlags_WidthFixed,
                              full_path_column_default_width);
      ImGui::TableSetupColumn("Extension");
      ImGui::TableSetupColumn("Matched Files", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultHide);
      ImGui::TableSetupColumn("Matched Size", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_DefaultHide);
      ImGui::TableSetColumnEnabled(ResultColumn::FolderFileCount, state.showFolderStatsColumns);
      ImGui::TableSetColumnEnabled(ResultColumn::FolderTotalSize, state.showFolderStatsColumns);
      // Accent-tinted column headers; when streaming, header text uses Warning color for visibility
      const bool streaming =
          !state.resultsComplete && state.showingPartialResults;
      RenderTableHeadersRow(streaming);

      // Handle sorting (only for complete results). Must run BEFORE GetDisplayResults:
      // HandleTableSorting may rebuild filter caches (filteredResults/sizeFilteredResults).
      // If we took display_results before this, that pointer would be invalidated after rebuild.
      if (state.resultsComplete) {
        search::SearchResultsService::HandleTableSorting(state, file_index, thread_pool);
        // HandleTableSorting may invalidate displayed total (re-sort on new results).
        // Re-run total size computation so we make progress even when sort invalidates.
        UpdateDisplayedTotalSizeIfNeeded(state, file_index);
      }

      // Get display results AFTER sort/filter so we never hold a pointer that gets invalidated
      const std::vector<SearchResult>* base_results_ptr =
        search::SearchResultsService::GetDisplayResults(state);
      const std::vector<SearchResult>& base_results = *base_results_ptr;

      table_scroll_y_for_search = ImGui::GetScrollY();

      if (float restore_y = 0.0F; incremental_search.ConsumeScrollRestorePending(restore_y)) {
        ImGui::SetScrollY(restore_y);
      }

      incremental_search.CheckBatchNumber(state.resultsBatchNumber, state.selectedRow);

      const std::vector<SearchResult>* display_results_ptr = &base_results;
      if (incremental_search.IsFilterActive()) {
        display_results_ptr = &incremental_search.FilteredResults();
      }
      const std::vector<SearchResult>& display_results = *display_results_ptr;

      // Clamp selectedRow so it is never out of bounds for the current display (avoids
      // wrong selection when switching between partial/final or when results are cleared)
      if (state.selectedRow >= static_cast<int>(display_results.size())) {
        state.selectedRow =
          display_results.empty() ? -1 : static_cast<int>(display_results.size()) - 1;
      }

      // Early check: if pendingDeletions is empty, skip the lookup for all rows
      const bool has_pending_deletions = !state.pendingDeletions.empty();

      // Build folder statistics cache when underlying results/filters changed.
      BuildFolderStatsIfNeeded(state, display_results);

      // Render all visible rows using ImGui clipper (extracted to reduce nesting depth)
      const RenderVisibleRowsParams visible_rows_params{display_results,
                                                        state,
                                                        file_index,
                                                        native_window,
                                                        glfw_window,
                                                        has_pending_deletions,
                                                        show_path_hierarchy_indentation,
                                                        &state.folderStatsByPath,
                                                        drag_candidate_active,
                                                        drag_candidate_row,
                                                        drag_start_pos,
                                                        drag_started};
      RenderVisibleTableRows(visible_rows_params);

      // Evaluate drag gesture and start shell drag-drop if threshold exceeded
      HandleDragAndDrop(drag_candidate_active, drag_candidate_row, drag_start_pos, drag_started,
                        drag_threshold, display_results, state);

      ImGui::EndTable();

      HandleResultsTableKeyboardShortcuts(state,
                                          display_results,
                                          file_index,
                                          glfw_window,
                                          shift,
                                          native_window,
                                          incremental_search,
                                          table_scroll_y_for_search,
                                          base_results);

      // Handle Delete Key and confirmation popup (use display_results so delete target matches
      // table)
      HandleDeleteKeyAndPopup(state, display_results);
      HandleBulkDeletePopup(state, file_index);
    }
  }
}

void CopyMarkedPathsToClipboard(const GuiState& state,
                                GLFWwindow* glfw_window,
                                const FileIndex& file_index) {
  if (state.markedFileIds.empty() || glfw_window == nullptr) {
    return;
  }

  std::string all_paths;  // NOLINT(misc-const-correctness) - modified by append/push_back in loop
  const auto& path_accessor = file_index.GetPathAccessor();
  for (const uint64_t file_id : GetMarkedIdsInOrder(state)) {
    const std::string_view path = path_accessor.GetPathView(file_id);
    if (!path.empty()) {
      all_paths.append(path.data(), path.size());
      all_paths.push_back('\n');
    }
  }

  if (!all_paths.empty()) {
    clipboard_utils::SetClipboardText(glfw_window, all_paths);
  }
}

void CopyMarkedFilenamesToClipboard(const GuiState& state,
                                    GLFWwindow* glfw_window,
                                    const FileIndex& file_index) {
  if (state.markedFileIds.empty() || glfw_window == nullptr) {
    return;
  }

  std::string all_filenames;  // NOLINT(misc-const-correctness) - modified by append/push_back in loop
  const auto& path_accessor = file_index.GetPathAccessor();
  for (const uint64_t file_id : GetMarkedIdsInOrder(state)) {
    const std::string_view path = path_accessor.GetPathView(file_id);
    if (path.empty()) {
      continue;
    }
    const size_t last_separator = path.find_last_of("\\/");
    std::string_view filename =
      (last_separator == std::string_view::npos) ? path : path.substr(last_separator + 1);
    // Defensive guard: ensure we never include path separators in the clipboard text, even if
    // upstream paths contain unexpected separators (ImGui shortcut tests require filenames only).
    if (const size_t guard_last_sep = filename.find_last_of("\\/");
        guard_last_sep != std::string_view::npos && guard_last_sep + 1U < filename.size()) {
      filename = filename.substr(guard_last_sep + 1U);
    }
    if (!filename.empty()) {
      all_filenames.append(filename.data(), filename.size());
      all_filenames.push_back('\n');
    }
  }

  if (!all_filenames.empty()) {
    clipboard_utils::SetClipboardText(glfw_window, all_filenames);
  }
}

}  // namespace ui
