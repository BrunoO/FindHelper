/**
 * @file ui/ResultsTable.cpp
 * @brief Implementation of search results table rendering component
 */

#include "ui/ResultsTable.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
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
#include "search/FolderSizeAggregator.h"
#include "search/SearchResultUtils.h"
#include "search/SearchResultsService.h"
#include "ui/IconsFontAwesome.h"
#include "ui/IncrementalSearchState.h"
#include "ui/LayoutConstants.h"
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

// Debounce for context menu to prevent rapid open (ms).
constexpr int kContextMenuDebounceMs = 300;
static_assert(kContextMenuDebounceMs > 0, "debounce must be positive");

// Renders the results table header row with accent-tinted background.
void RenderTableHeadersRow() {
  const MultiStyleColorGuard hdr(
      {{ImGuiCol_TableHeaderBg, Theme::AccentTint(Theme::HeaderAlphas::TableHeader)}});
  ImGui::TableHeadersRow();
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
  if (const bool cache_valid =
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
  const char* end = start + full_path.size();
  const char* current = start;

  for (const char* p = start; p < end; ++p) {
    if ((*p == '\\' || *p == '/') && (p + 1 < end)) {
      ImGui::TextUnformatted(current, p + 1);
      current = p + 1;
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
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  const SearchResult& result;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members) -
                               // parameter struct, references avoid copy
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  GuiState& state;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  FileIndex& file_index;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  NativeWindowHandle native_window{};
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  GLFWwindow* glfw_window = nullptr;
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  bool has_pending_deletions = false;
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  bool show_path_hierarchy_indentation = true;
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  FolderSizeAggregator* folder_aggregator = nullptr;
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  bool& drag_candidate_active;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  int& drag_candidate_row;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  ImVec2& drag_start_pos;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  bool& drag_started;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  int focus_row = -1;  // Row that should receive SetKeyboardFocusHere(0); -1 = none
};

/**
 * @brief Parameters for rendering visible table rows
 * Grouped to reduce function parameter count
 */
struct RenderVisibleRowsParams {
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  const std::vector<SearchResult>& display_results;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  GuiState& state;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  FileIndex& file_index;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  NativeWindowHandle native_window{};
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  GLFWwindow* glfw_window = nullptr;
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  bool has_pending_deletions = false;
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  bool show_path_hierarchy_indentation = true;
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  FolderSizeAggregator* folder_aggregator = nullptr;
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  bool& drag_candidate_active;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  int& drag_candidate_row;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  ImVec2& drag_start_pos;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  bool& drag_started;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  // Row index that ImGui's multi-select uses as the range anchor (RangeSrcItem from
  // BeginMultiSelect). Must always be submitted to the clipper so it is never clipped out;
  // corrupt range selections result silently if it is skipped. -1 means no active range.
  // NOLINTNEXTLINE(readability-identifier-naming) - render param bundle; names mirror call sites
  int range_src_item = -1;
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
  // Consume the pending focus request once before the clipper loop. Since all keyboard navigation
  // paths that call RequestFocusForRow also set scrollToSelectedRow = true, the target row is
  // always forced into the visible range by IncludeItemByIndex, so consuming here is safe.
  const int focus_row = params.state.selection.ConsumeFocusRequest();

  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(
    params.display_results.size()));  // NOSONAR(cpp:S1905) - Required cast: ImGui API expects int
  if (const int selected_row_for_scroll = params.state.selection.GetSelectedRow();
      params.state.selection.scrollToSelectedRow && selected_row_for_scroll >= 0 &&
      selected_row_for_scroll < static_cast<int>(params.display_results.size())) {
    clipper.IncludeItemByIndex(selected_row_for_scroll);
  }
  // Always include the multi-select range anchor so ImGui can compute ranges correctly.
  // If RangeSrcItem is clipped, range selections corrupt silently (ImGui API contract).
  if (const auto item_count = static_cast<int>(params.display_results.size());
      params.range_src_item >= 0 && params.range_src_item < item_count) {
    clipper.IncludeItemByIndex(params.range_src_item);
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
                                       params.folder_aggregator,
                                       params.drag_candidate_active,
                                       params.drag_candidate_row,
                                       params.drag_start_pos,
                                       params.drag_started,
                                       focus_row};
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
    if (copy_len > 0U) {
      std::copy_n(base_name.begin(),
                  static_cast<std::ptrdiff_t>(copy_len),
                  filename_buffer.begin());
      write_len += copy_len;
    }
  }

  if (has_extension && write_len < max_len) {
    filename_buffer.at(write_len) = '.';
    ++write_len;
    const size_t remaining = max_len - write_len;
    if (remaining > 0U) {
      const size_t ext_copy_len = (std::min)(extension.size(), remaining);
      if (ext_copy_len > 0U) {
        auto dest_it = filename_buffer.begin()
                       + static_cast<std::ptrdiff_t>(write_len);
        std::copy_n(extension.begin(),
                    static_cast<std::ptrdiff_t>(ext_copy_len),
                    dest_it);
        write_len += ext_copy_len;
      }
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
  EnsureDisplayStringsPopulated(params.result, params.file_index);

  // For directories with no size or folder-file-count yet, request calculation from aggregator.
  // Both fields are written together by the aggregator, so checking either sentinel is sufficient
  // to gate the request. Check both so the condition remains correct if either field is ever
  // populated by a separate path in the future.
  if (params.result.isDirectory &&
      (params.result.fileSize == kFileSizeNotLoaded ||
       params.result.folderFileCount == kFolderFileCountNotLoaded) &&
      params.folder_aggregator != nullptr) {
    params.folder_aggregator->Request(params.result.fileId, params.result.fullPath);
    if (const auto stats = params.folder_aggregator->GetResult(params.result.fileId);
        stats.has_value()) {
      params.result.fileSize = stats->total_size;
      // Always reformat: fileSizeDisplay may hold the "..." placeholder set by
      // EnsureDisplayStringsPopulated, and must be replaced with the real value.
      params.result.fileSizeDisplay = FormatFileSize(params.result.fileSize);
      params.result.folderFileCount = stats->file_count;
      params.result.folderFileCountDisplay = FormatFileCount(params.result.folderFileCount);
    }
  }
  // Post-condition: if we wrote a real size, the placeholder must be gone.
  assert(!(params.result.isDirectory && params.result.fileSize != kFileSizeNotLoaded &&  // NOSONAR(cpp:S2583) - Intentional debug invariant; Sonar path analysis sees the preceding write as making this trivially true, but this assertion catches cases where the display string was not refreshed after an earlier size write
           params.result.fileSize != kFileSizeFailed &&
           params.result.fileSizeDisplay == "...") &&
         "ResultsTable: directory has real size but display still shows placeholder");

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
  const bool is_marked =
    (params.state.selection.markedFileIds.find(params.result.fileId) !=
     params.state.selection.markedFileIds.end());
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

  const bool is_selected = params.state.selection.IsRowSelected(row);

  // Column 0: Mark (dired-style)
  ImGui::TableSetColumnIndex(ResultColumn::Mark);
  ImGui::PushID(ResultColumn::Mark);
  // Use unique hidden ID (##mark) to avoid collisions
  ImGui::SetNextItemSelectionUserData(row);
  if (const char* mark_label = is_marked ? "*##mark" : " ##mark";
      ImGui::Selectable(mark_label, is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
    // Selection handled by ImGui multi-select. Toggle mark only:
    if (is_marked) {
      params.state.selection.markedFileIds.erase(params.result.fileId);
    } else {
      params.state.selection.markedFileIds.insert(params.result.fileId);
    }
  }
  ImGui::PopID();

  // Set text color
  if (const bool is_pending_delete =
        params.has_pending_deletions && params.state.selection.IsPendingDeletion(params.result.fullPath);
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
  // Focus sync: if a keyboard navigation set a focus request for this row, call
  // SetKeyboardFocusHere(0) immediately before the Selectable so ImGui's focus
  // rectangle tracks the selection. The request was already consumed before the
  // clipper loop (see RenderVisibleTableRows) to avoid re-firing across frames.
  if (row == params.focus_row) {
    ImGui::SetKeyboardFocusHere(0);
  }
  ImGui::SetNextItemSelectionUserData(row);
  if (const char* filename_cstr = GetRowFilenameCstr(params.result, filename_buffer);
      ImGui::Selectable(filename_cstr, is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
    // Selection handled by ImGui multi-select. No per-column action here.
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
  ImGui::SetNextItemSelectionUserData(row);
  if (ImGui::Selectable(ResultsTable::GetSizeDisplayText(params.result), is_selected, 0)) {
    // Selection handled by ImGui multi-select.
  }
  ImGui::PopID();

  // Column 2: Last Modified
  ImGui::TableSetColumnIndex(ResultColumn::LastModified);
  ImGui::PushID(ResultColumn::LastModified);
  ImGui::SetNextItemSelectionUserData(row);
  if (ImGui::Selectable(ResultsTable::GetModTimeDisplayText(params.result), is_selected, 0)) {
    // Selection handled by ImGui multi-select.
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
  ImGui::SetNextItemSelectionUserData(row);
  if (ImGui::Selectable(ext_cstr, is_selected, 0)) {
    // Selection handled by ImGui multi-select.
  }
  ImGui::PopID();

  // Column 6: # Files (recursive non-directory file count; directories only)
  ImGui::TableSetColumnIndex(ResultColumn::FolderFiles);
  ImGui::PushID(ResultColumn::FolderFiles);
  ImGui::SetNextItemSelectionUserData(row);
  if (ImGui::Selectable(ResultsTable::GetFolderFilesDisplayText(params.result), is_selected, 0)) {
    // Selection handled by ImGui multi-select.
  }
  ImGui::PopID();

  // Handle scrolling to the primary selected row. Compare against selectedRow (the scroll
  // target, set to new_primary by SetSelectionRange) rather than is_selected so that range
  // selections scroll to the correct end of the range, not the first selected row rendered.
  if (row == params.state.selection.GetSelectedRow() && params.state.selection.scrollToSelectedRow) {
    ImGui::SetScrollHereY();
    params.state.selection.scrollToSelectedRow = false;
  }

  ImGui::PopID();

  // Always pop the text color style (either high-contrast or grayed-out)
  ImGui::PopStyleColor();
}

// Returns true if result is a file that may be dragged: not a directory and not pending delete.
[[nodiscard]] bool IsEligibleForDrag(const SearchResult& result, const GuiState& state) {
  return !result.isDirectory && !state.selection.IsPendingDeletion(result.fullPath);
}

/**
 * @brief Start file drag-drop if result is not a directory and not pending delete.
 * Extracted to keep EvaluateDragGesture nesting at most 3 levels (S134).
 */
void TryStartFileDragDropIfAllowed(const SearchResult& drag_result, const GuiState& state) {
  if (!IsEligibleForDrag(drag_result, state)) {
    return;
  }
#if defined(_WIN32) || defined(__APPLE__)
  drag_drop::StartFileDragDrop(drag_result.fullPath);
#endif  // defined(_WIN32) || defined(__APPLE__)
}

/**
 * @brief Collect eligible drag paths from the current multi-selection.
 *
 * Iterates selected rows in display order, skipping directories and rows
 * pending deletion. The returned views point into display_results, which
 * must outlive the returned vector.
 */
std::vector<std::string_view> CollectEligibleDragPaths(
  const std::vector<SearchResult>& display_results,
  const GuiState& state) {
  const auto item_count = static_cast<int>(display_results.size());
  std::vector<std::string_view> paths;
  for (const int row : state.selection.GetSelectedRows()) {
    if (row < 0 || row >= item_count) {
      continue;
    }
    const auto& result = display_results[static_cast<std::size_t>(row)];  // NOSONAR(cpp:S1905) - Required cast for array indexing
    if (IsEligibleForDrag(result, state)) {
      paths.push_back(result.fullPath);
    }
  }
  return paths;
}

/**
 * @brief After movement threshold: snap selection for unselected row or multi-file drag for selection.
 * Extracted to keep HandleDragAndDrop nesting within Sonar S134 limits.
 */
void StartDragAfterThresholdExceeded(const SearchResult& drag_result, int drag_candidate_row,
                                     const std::vector<SearchResult>& display_results,
                                     GuiState& state) {
  if (!state.selection.IsRowSelected(drag_candidate_row)) {
    state.selection.SetSelectedRow(drag_candidate_row);
    TryStartFileDragDropIfAllowed(drag_result, state);
    return;
  }
  const auto eligible_paths = CollectEligibleDragPaths(display_results, state);
  if (eligible_paths.empty()) {
    return;
  }
#if defined(_WIN32) || defined(__APPLE__)
  drag_drop::StartFileDragDrop(eligible_paths);
#endif  // defined(_WIN32) || defined(__APPLE__)
}

/**
 * @brief Handle drag-and-drop gesture evaluation.
 *
 * Evaluates the drag gesture and starts a shell drag-drop when the movement threshold is
 * exceeded. Selection-aware: dragging from an unselected row snaps the selection to that
 * row and performs a single-file drag. Dragging from a selected row collects all eligible
 * selected files and initiates a multi-file shell drag for all of them.
 *
 * @param drag_candidate_active Whether a drag candidate is active (modified)
 * @param drag_candidate_row    Row index of drag candidate (modified)
 * @param drag_start_pos        Starting position of drag (input)
 * @param drag_started          Whether drag has started (modified)
 * @param drag_threshold        Minimum movement in pixels to start drag
 * @param display_results       Display results for drag target lookup
 * @param state                 GUI state for selection and pending deletions (modified)
 */
void HandleDragAndDrop(bool& drag_candidate_active, int& drag_candidate_row,
                       const ImVec2& drag_start_pos, bool& drag_started, float drag_threshold,
                       const std::vector<SearchResult>& display_results, GuiState& state) {
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
      const bool over_threshold =
        std::fabs(delta.x) >= drag_threshold || std::fabs(delta.y) >= drag_threshold;
      const bool row_valid =
        drag_candidate_row >= 0 &&
        drag_candidate_row < static_cast<int>(display_results.size());  // NOSONAR(cpp:S1905) - Required cast for signed/unsigned comparison
      if (over_threshold && row_valid) {
        const auto& drag_result =
          display_results[static_cast<std::size_t>(drag_candidate_row)];  // NOSONAR(cpp:S1905) - Required cast for array indexing
        StartDragAfterThresholdExceeded(drag_result, drag_candidate_row, display_results, state);
        drag_started = true;
      }
    }
  } else {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      drag_started = false;
    }
  }
}

// Attempt deletion of every path in state.selection.filesToDelete, clear the list, and report aggregate
// feedback via state.exportNotification / state.exportErrorMessage.
void ExecuteMultiDelete(GuiState& state) {
  int deleted_count = 0;   // NOLINT(misc-const-correctness) - incremented inside loop
  int failed_count = 0;    // NOLINT(misc-const-correctness) - incremented inside loop
  std::string failed_paths;  // NOLINT(misc-const-correctness) - appended inside loop
  for (const auto& path : state.selection.filesToDelete) {
    if (file_operations::DeleteFileToRecycleBin(path)) {
      state.selection.pendingDeletions.insert(path);
      ++deleted_count;
    } else {
      if (!failed_paths.empty()) {
        failed_paths += ", ";
      }
      failed_paths += path;
      ++failed_count;
    }
  }
  state.selection.filesToDelete.clear();

  if (failed_paths.empty()) {
    state.exportNotification =
      (deleted_count == 1) ? "Deleted 1 item"
                           : "Deleted " + std::to_string(deleted_count) + " items";
    state.exportErrorMessage = "";
  } else {
    state.exportNotification =
      (deleted_count > 0) ? "Deleted " + std::to_string(deleted_count) + "/" +
                              std::to_string(deleted_count + failed_count) + " items"
                          : "";
    state.exportErrorMessage = "Failed to delete: " + failed_paths;
  }
  state.exportNotificationTime = std::chrono::steady_clock::now();
}

/**
 * @brief Handle delete key and confirmation popup
 *
 * Renders the delete confirmation modal for one or more selected files.
 * Delete key handling is in HandleResultsTableKeyboardShortcuts; this only opens and draws the popup.
 *
 * @param state GUI state (modified by delete operations)
 */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Cohesive delete confirmation modal: single/multi-item display, per-item loop, aggregate feedback; splitting would fragment closely related logic
void HandleDeleteKeyAndPopup(GuiState& state) {  // NOSONAR(cpp:S3776) - Same rationale as above
  if (state.selection.showDeletePopup) {
    ImGui::OpenPopup("Confirm Delete");
    state.selection.showDeletePopup = false;
  }

  // Confirmation Modal
  // Center popup in main window every time it appears
  CenterNextWindowInMainWindow();

  if (ImGui::BeginPopupModal("Confirm Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (const auto delete_count = state.selection.filesToDelete.size(); delete_count == 1) {
      ImGui::Text("Are you sure you want to delete this file?");
      ImGui::TextColored(Theme::Colors::Error, "%s", state.selection.filesToDelete[0].c_str());  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by delete_count == 1
    } else {
      const std::string header_msg =
        "Are you sure you want to delete " + std::to_string(delete_count) + " items?";
      ImGui::TextUnformatted(header_msg.c_str());
      constexpr std::size_t kMaxListedPaths = 3;
      const std::size_t listed = (std::min)(delete_count, kMaxListedPaths);
      for (std::size_t i = 0; i < listed; ++i) {
        ImGui::TextColored(Theme::Colors::Error, "%s", state.selection.filesToDelete[i].c_str());  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - guarded by i < listed <= delete_count
      }
      if (delete_count > kMaxListedPaths) {
        ImGui::Text("... and %zu more", delete_count - kMaxListedPaths);
      }
    }
    ImGui::Separator();

    if (ImGui::Button(ICON_FA_TRASH " Delete", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
      // DeleteFileToRecycleBin is implemented on both Windows and macOS
      // Windows: Moves to Recycle Bin, macOS: Moves to Trash
      ExecuteMultiDelete(state);
      state.selection.ClearSelection();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK " Cancel", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
      state.selection.filesToDelete.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

// Returns marked IDs in deterministic order (by ID) for clipboard and bulk ops.
// hash_set_t iteration is unspecified; sorting restores repeatable, user-facing order.
std::vector<uint64_t> GetMarkedIdsInOrder(const GuiState& state) {
  std::vector<uint64_t> ids(state.selection.markedFileIds.begin(), state.selection.markedFileIds.end());
  std::sort(ids.begin(), ids.end());
  return ids;
}

// Iterates marked file paths in deterministic order; eliminates GetPathAccessor +
// GetMarkedIdsInOrder boilerplate from bulk-copy and bulk-delete callers.
template <typename Callback>
void ForEachMarkedPath(const GuiState& state, const FileIndex& file_index,
                       Callback callback) {
  const auto path_accessor = file_index.GetPathAccessor();
  for (const uint64_t file_id : GetMarkedIdsInOrder(state)) {
    const std::string path = path_accessor.GetPathCopy(file_id);
    callback(path);
  }
}

void ExecuteBulkDelete(GuiState& state, const FileIndex& file_index) {
  ForEachMarkedPath(state, file_index, [&state](std::string_view path) {
    if (!path.empty() &&
        file_operations::DeleteFileToRecycleBin(std::string(path))) {
      state.selection.pendingDeletions.emplace(path);
    }
  });
  state.selection.markedFileIds.clear();
  state.selection.ClearSelection();
  ImGui::CloseCurrentPopup();
}

void HandleBulkDeletePopup(GuiState& state, const FileIndex& file_index) {
  if (state.selection.showBulkDeletePopup) {
    ImGui::OpenPopup("Confirm Bulk Delete");
    state.selection.showBulkDeletePopup = false;
  }

  CenterNextWindowInMainWindow();

  if (ImGui::BeginPopupModal("Confirm Bulk Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const size_t marked_count = state.selection.markedFileIds.size();

    const std::string delete_message =
      "Are you sure you want to delete " + std::to_string(marked_count) + " marked files?";
    ImGui::TextUnformatted(delete_message.c_str());

    {
      const detail::StyleColorGuard error_color_guard(ImGuiCol_Text, Theme::Colors::Error);
      ImGui::TextUnformatted("This action will move them to the Recycle Bin.");
    }
    ImGui::Separator();

    if (ImGui::Button(ICON_FA_TRASH " Delete All", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
      ExecuteBulkDelete(state, file_index);
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK " Cancel", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

// Translate ImGui multi-select requests into GuiState selection mutations.
// Called twice per frame: once after BeginMultiSelect (pending nav requests from
// the previous frame) and once after EndMultiSelect (requests from this frame).
void ApplyMultiSelectRequests(const ImGuiMultiSelectIO* ms_io, GuiState& state, int item_count) {
  if (ms_io->Requests.empty()) {
    return;
  }
  for (const ImGuiSelectionRequest& req : ms_io->Requests) {
    if (req.Type == ImGuiSelectionRequestType_SetAll) {
      if (req.Selected && item_count > 0) {
        state.selection.SetSelectionRange(0, item_count - 1, 0, 0);
      } else {
        state.selection.ClearSelection();
      }
    } else if (req.Type == ImGuiSelectionRequestType_SetRange) {
      const auto first  = static_cast<int>(req.RangeFirstItem);
      const auto last   = static_cast<int>(req.RangeLastItem);
      const int  lo     = (std::min)(first, last);
      const int  hi     = (std::max)(first, last);
      const auto anchor = static_cast<int>(ms_io->RangeSrcItem);
      if (req.Selected) {
        // AddSelectionRange merges [lo,hi] into existing selection without clearing.
        // When ImGui precedes this with SetAll(false), the selection is already empty,
        // so the result is the same as SetSelectionRange — but Ctrl+Click arrives with
        // no preceding Clear and must add to the existing selection, not replace it.
        state.selection.AddSelectionRange(lo, hi, anchor, last);
      } else {
        state.selection.RemoveSelectionRange(lo, hi);
      }
    }
  }
}

// Sync selected_row with incremental search batch changes. Only resets the selection
// (single-row, clears multi-select) when CheckBatchNumber actually changed the value —
// e.g. a new search arrived while the incremental filter prompt was visible.
void SyncSelectedRowWithIncrementalSearch(GuiState& state,
                                          IncrementalSearchState& incremental_search) {
  int selected_row = state.selection.GetSelectedRow();
  const int pre_check    = selected_row;
  incremental_search.CheckBatchNumber(state.result_pool_->BatchNumber(), selected_row);
  if (selected_row != pre_check) {
    state.selection.SetSelectedRow(selected_row);
  }
}

// Build the per-frame multi-select flags and open the multi-select scope.
// Must be called inside BeginTable (inside the scroll child) so that ms->FocusScopeId
// matches the scroll child's focus scope → ms->IsFocused = true → Shift+Arrow works.
// ClearOnEscape is suppressed while the incremental search prompt is visible (Gap 2).
[[nodiscard]] ImGuiMultiSelectIO* BeginMultiSelectForTable(
    const IncrementalSearchState& incremental_search,
    const GuiState& state,
    int display_item_count) {
  ImGuiMultiSelectFlags ms_flags =
      ImGuiMultiSelectFlags_ScopeRect |    // NOLINT(hicpp-signed-bitwise) - ImGui flags bitmask
      ImGuiMultiSelectFlags_BoxSelect1d |  // NOLINT(hicpp-signed-bitwise)
      ImGuiMultiSelectFlags_NoAutoSelect;  // NOLINT(hicpp-signed-bitwise) - Down/Up/N/P handled manually; prevents SetKeyboardFocusHere from generating a conflicting auto-select SetRange
  if (!incremental_search.IsPromptVisible()) {
    ms_flags |= ImGuiMultiSelectFlags_ClearOnEscape;  // NOLINT(hicpp-signed-bitwise)
  }
  return ImGui::BeginMultiSelect(
      ms_flags, static_cast<int>(state.selection.GetSelectedRows().size()), display_item_count);
}

}  // namespace

bool ResultsTable::RenderPathColumnWithEllipsis(const SearchResult& result, bool is_selected,
                                                int row, [[maybe_unused]] GuiState& state,
                                                [[maybe_unused]] NativeWindowHandle native_window,
                                                GLFWwindow* glfw_window, float max_width) {
  // max_width is pre-calculated and passed in to avoid repeated GetColumnWidth calls
  // This improves performance when clipping is active (only visible rows are rendered)

  // Extract directory path (no allocation, just string_view)
  // This avoids showing filename in Full Path column since it's already in Filename column
  const std::string_view directory_path = GetDirectoryPath(result);

  // Calculate display path with caching and truncation (extracted to reduce complexity)
  const char* display_path_cstr = CalculateDisplayPath(
      const_cast<SearchResult&>(result), directory_path, max_width);  // NOSONAR(cpp:S859) NOLINT(cppcoreguidelines-pro-type-const-cast) - caching optimization

  ImGui::SetNextItemSelectionUserData(row);
  // Selection handled by ImGui multi-select. Keep double-click → OpenParentFolder below.
  const bool selection_changed =
      ImGui::Selectable(display_path_cstr, is_selected, ImGuiSelectableFlags_AllowDoubleClick);

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

const char* ResultsTable::GetFolderFilesDisplayText(const SearchResult& result) {
  if (!result.isDirectory) {
    return "-";
  }
  if (result.folderFileCount == kFolderFileCountNotLoaded) {
    return "...";
  }
  // Fallback: reformat if display is empty but count is available.
  if (result.folderFileCountDisplay.empty()) {
    result.folderFileCountDisplay = FormatFileCount(result.folderFileCount);
  }
  return result.folderFileCountDisplay.c_str();
}

const char* ResultsTable::GetModTimeDisplayText(const SearchResult& result) {
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
  const size_t marked_count = state.selection.markedFileIds.size();
  const size_t selected_count = state.selection.GetSelectedRows().size();
  if (marked_count == 0 && selected_count <= 1) {
    return;
  }

  // "N selected" badge — shown when 2+ rows are selected.
  if (selected_count > 1) {
    const detail::StyleColorGuard sel_color_guard(ImGuiCol_Text, Theme::Colors::Accent);
    const std::string sel_label = std::to_string(selected_count) + " selected";
    ImGui::TextUnformatted(sel_label.c_str());
    if (marked_count > 0) {
      ImGui::SameLine();
    }
  }

  if (marked_count == 0) {
    return;  // only the selection count badge was needed; no mark buttons
  }

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
    state.selection.showBulkDeletePopup = true;
  }
  ImGui::SameLine();

  if (ImGui::Button(ICON_FA_XMARK " Unmark All")) {
    state.selection.markedFileIds.clear();
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
void RenderResultsTableHeaderArea(const IncrementalSearchState& incremental_search,
                                 GuiState& state,
                                 GLFWwindow* glfw_window,
                                 const FileIndex& file_index,
                                 bool results_shortcuts_active,
                                 bool shift) {
  if (incremental_search.IsPromptVisible()) {
    RenderIncrementalSearchPrompt(incremental_search);
  } else if (incremental_search.IsFilterActive()) {
    RenderIncrementalSearchFilterStatus(incremental_search);
  } else if (!state.selection.markedFileIds.empty() || state.selection.GetSelectedRows().size() > 1) {
    RenderMarkedActionsToolbar(state, glfw_window, file_index, shift);
  } else if (results_shortcuts_active) {
    const detail::StyleColorGuard text_color_guard(ImGuiCol_Text, Theme::Colors::Accent);
    const float available_width = ImGui::GetContentRegionAvail().x;
#ifdef __APPLE__
    constexpr const char* k_full_shortcuts_text =
      "Results table active - arrows/N/P navigate; '/' starts Filter in results; Esc "
      "clears filters; Cmd+G cancels the filter.";
    constexpr const char* k_short_shortcuts_text =
      "Results table active - arrows/N/P navigate; '/' starts Filter in results; Esc "
      "clears filters; Cmd+G cancels the filter.";
    constexpr const char* k_compact_shortcuts_label =
      "Results table active - shortcuts";
#else
    constexpr const char* k_full_shortcuts_text =
      "Results table active - arrows/N/P navigate; '/' starts Filter in results; Esc "
      "clears filters; Ctrl+G cancels the filter.";
    constexpr const char* k_short_shortcuts_text =
      "Results table active - arrows/N/P navigate; '/' starts Filter in results; Esc "
      "clears filters; Ctrl+G cancels the filter.";
    constexpr const char* k_compact_shortcuts_label =
      "Results table active - shortcuts";
#endif  // __APPLE__

    const ImVec2 full_size = ImGui::CalcTextSize(k_full_shortcuts_text);
    if (full_size.x <= available_width) {
      ImGui::TextUnformatted(k_full_shortcuts_text);
    } else {
      const ImVec2 short_size = ImGui::CalcTextSize(k_short_shortcuts_text);
      if (short_size.x <= available_width) {
        ImGui::TextUnformatted(k_short_shortcuts_text);
      } else {
        ImGui::TextUnformatted(k_compact_shortcuts_label);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
      }
    }
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

  // When results shortcuts are active and the compact help label is shown,
  // attach a tooltip with the full shortcuts text without increasing nesting depth.
  if (results_shortcuts_active) {
#ifdef __APPLE__
    constexpr const char* k_full_shortcuts_text_tooltip =
      "Results table active - arrows/N/P navigate; '/' starts Filter in results; Esc "
      "clears filters; Cmd+G cancels the filter.";
#else
    constexpr const char* k_full_shortcuts_text_tooltip =
      "Results table active - arrows/N/P navigate; '/' starts Filter in results; Esc "
      "clears filters; Ctrl+G cancels the filter.";
#endif  // __APPLE__

    const ImVec2 full_size_tooltip = ImGui::CalcTextSize(k_full_shortcuts_text_tooltip);
    if (full_size_tooltip.x > ImGui::GetContentRegionAvail().x &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
      constexpr float kTooltipWrapWidthInEm = 40.0F;
      ImGui::BeginTooltip();
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * kTooltipWrapWidthInEm);
      ImGui::TextUnformatted(k_full_shortcuts_text_tooltip);
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }
  }

  ImGui::Spacing();
}

// Pre-sort flush: write aggregator-computed folder stats into searchResults and trigger
// re-sort when all directory stats are confirmed. Keeps nesting in Render at most 3 levels.
// Activated for both Size and FolderFiles columns since both depend on aggregator results.
void FlushAggregatorSizesAndTriggerSortIfReady(GuiState& state,
                                               FolderSizeAggregator* aggregator) {
  if (aggregator == nullptr ||
      (state.lastSortColumn != ResultColumn::Size &&
       state.lastSortColumn != ResultColumn::FolderFiles)) {
    return;
  }
  // For FolderFiles sort, track directories missing their file count; for Size, those missing
  // their size. Both fields are written together by the aggregator, so they are normally in
  // sync, but checking the field that the active sort actually needs makes the intent explicit
  // and keeps correctness if either field is ever populated by a separate code path.
  const bool sort_by_folder_files = (state.lastSortColumn == ResultColumn::FolderFiles);
  const auto kIsPendingDir = [sort_by_folder_files](const SearchResult& r) {
    if (!r.isDirectory) { return false; }
    return sort_by_folder_files ? (r.folderFileCount == kFolderFileCountNotLoaded)
                                : (r.fileSize == kFileSizeNotLoaded);
  };
  const auto pending_before = std::count_if(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
      state.result_pool_->Results().begin(), state.result_pool_->Results().end(), kIsPendingDir);

  // Pass 1: enqueue all pending directories in a single lock acquisition.
  std::vector<std::pair<uint64_t, std::string_view>> batch;
  for (const auto& result : state.result_pool_->Results()) {
    if (kIsPendingDir(result)) {
      batch.emplace_back(result.fileId, std::string_view{result.fullPath});
    }
  }
  aggregator->RequestBatch(batch);

  // Pass 2: collect any results that are already computed.
  for (auto& result : state.result_pool_->Results()) {
    if (!kIsPendingDir(result)) {
      continue;
    }
    const auto stats = aggregator->GetResult(result.fileId);
    if (!stats.has_value()) {
      continue;
    }
    result.fileSize = stats->total_size;
    result.fileSizeDisplay = FormatFileSize(result.fileSize);
    result.folderFileCount = stats->file_count;
    result.folderFileCountDisplay = FormatFileCount(result.folderFileCount);
  }

  const auto pending_after = std::count_if(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
      state.result_pool_->Results().begin(), state.result_pool_->Results().end(), kIsPendingDir);
  if (pending_before > pending_after && pending_after == 0) {
    assert(std::none_of(state.result_pool_->Results().begin(), state.result_pool_->Results().end(),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
                        kIsPendingDir) &&
           "ResultsTable: sort triggered before all directory stats are confirmed");
    state.resultsUpdated = true;
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Complex but cohesive UI rendering; splitting further would harm locality and clarity
void ResultsTable::Render(
  GuiState& state,
  NativeWindowHandle
    native_window,
  GLFWwindow* glfw_window, ThreadPool& thread_pool, FileIndex& file_index,
  FolderSizeAggregator* aggregator,
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

  // Show results table if search is active OR if we have results to display.
  if (state.searchActive || !state.result_pool_->Results().empty()) {
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

    if (constexpr int kResultColumnCount = 7;
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
      // Hidden by default: secondary info, enabled via right-click column header menu.
      ImGui::TableSetupColumn("Descendant Files*",
                              ImGuiTableColumnFlags_DefaultHide |           // NOLINT(hicpp-signed-bitwise) - ImGui flags bitmask
                                ImGuiTableColumnFlags_PreferSortDescending);
      RenderTableHeadersRow();
      // Last header item submitted by TableHeadersRow is the final column ("Descendant Files*").
      // Show staleness semantics only when that header is hovered.
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip(
          "* Recursive non-directory file count; computed from cached index state and may lag recent file changes.");
      }

      // Handle sorting (only for complete results). Must run BEFORE GetDisplayResults:
      // HandleTableSorting may rebuild filter caches (filteredResults/sizeFilteredResults).
      // If we took display_results before this, that pointer would be invalidated after rebuild.
      if (state.resultsComplete) {
        FlushAggregatorSizesAndTriggerSortIfReady(state, aggregator);
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

      SyncSelectedRowWithIncrementalSearch(state, incremental_search);

      const std::vector<SearchResult>* display_results_ptr = &base_results;
      if (incremental_search.IsFilterActive()) {
        display_results_ptr = &incremental_search.FilteredResults();
      }
      const std::vector<SearchResult>& display_results = *display_results_ptr;

      // Clamp all selection indices so none are out of bounds for the current display.
      state.selection.ClampSelectionToRowCount(display_results.size());

      // display_results is now resolved; open the multi-select scope inside the scroll child.
      const auto display_item_count = static_cast<int>(display_results.size());
      const ImGuiMultiSelectIO* ms_io =
          BeginMultiSelectForTable(incremental_search, state, display_item_count);
      ApplyMultiSelectRequests(ms_io, state, display_item_count);

      // Early check: if pendingDeletions is empty, skip the lookup for all rows
      const bool has_pending_deletions = !state.selection.pendingDeletions.empty();

      // Render all visible rows using ImGui clipper (extracted to reduce nesting depth)
      const RenderVisibleRowsParams visible_rows_params{display_results,
                                                        state,
                                                        file_index,
                                                        native_window,
                                                        glfw_window,
                                                        has_pending_deletions,
                                                        show_path_hierarchy_indentation,
                                                        aggregator,
                                                        drag_candidate_active,
                                                        drag_candidate_row,
                                                        drag_start_pos,
                                                        drag_started,
                                                        static_cast<int>(ms_io->RangeSrcItem)};
      RenderVisibleTableRows(visible_rows_params);

      // Update status bar flag AFTER row rendering: per-row RenderResultsTableRow calls
      // aggregator->Request() for each visible directory, so HasPendingWork() is only
      // meaningful after all rows have been processed. Placed outside the resultsComplete
      // guard so the flag clears immediately when CancelPending() is called on a new search.
      state.computingFolderSizes = aggregator != nullptr && aggregator->HasPendingWork();

      // Evaluate drag gesture and start shell drag-drop if threshold exceeded
      HandleDragAndDrop(drag_candidate_active, drag_candidate_row, drag_start_pos, drag_started,
                        drag_threshold, display_results, state);

      // Apply requests generated by this frame's interactions (clicks, keyboard, box-select).
      ms_io = ImGui::EndMultiSelect();
      ApplyMultiSelectRequests(ms_io, state, display_item_count);

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

      // Delete confirmation modal (paths come from state set by keyboard shortcuts)
      HandleDeleteKeyAndPopup(state);
      HandleBulkDeletePopup(state, file_index);
    }
  }
}

void CopyMarkedPathsToClipboard(const GuiState& state,
                                GLFWwindow* glfw_window,
                                const FileIndex& file_index) {
  if (state.selection.markedFileIds.empty() || glfw_window == nullptr) {
    return;
  }

  std::string all_paths;  // NOLINT(misc-const-correctness) - modified by append/push_back in loop
  ForEachMarkedPath(state, file_index, [&all_paths](std::string_view path) {
    if (!path.empty()) {
      all_paths.append(path.data(), path.size());
      all_paths.push_back('\n');
    }
  });

  if (!all_paths.empty()) {
    clipboard_utils::SetClipboardText(glfw_window, all_paths);
  }
}

void CopyMarkedFilenamesToClipboard(const GuiState& state,
                                    GLFWwindow* glfw_window,
                                    const FileIndex& file_index) {
  if (state.selection.markedFileIds.empty() || glfw_window == nullptr) {
    return;
  }

  std::string all_filenames;  // NOLINT(misc-const-correctness) - modified by append/push_back in loop
  ForEachMarkedPath(state, file_index, [&all_filenames](std::string_view path) {
    if (path.empty()) {
      return;
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
      all_filenames.append(filename.data(), filename.size());  // NOLINT(bugprone-suspicious-stringview-data-usage) - append(ptr,len) does not require null termination
      all_filenames.push_back('\n');
    }
  });

  if (!all_filenames.empty()) {
    clipboard_utils::SetClipboardText(glfw_window, all_filenames);
  }
}

}  // namespace ui
