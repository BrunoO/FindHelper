/**
 * @file ui/FolderBrowser.cpp
 * @brief Implementation of C++17-compatible ImGui folder browser widget
 */

#include "ui/FolderBrowser.h"

#include "imgui.h"
#include "ui/IconsFontAwesome.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <stdexcept>

namespace ui {

namespace {
constexpr int kFilterBufferSize = 256;
constexpr float kFolderListBottomPadding = -60.0F;
constexpr float kBrowserWindowWidth = 600.0F;
constexpr float kBrowserWindowHeight = 400.0F;
constexpr float kActionButtonWidth = 120.0F;
constexpr float kSelectButtonRightOffset = -250.0F;
constexpr float kModalPivotHalf = 0.5F;

// Helper function to check if display name matches filter (reduces nesting)
[[nodiscard]] bool MatchesFilter(std::string_view display_name, std::string_view filter_text) {
  if (filter_text.empty()) {
    return true;
  }
  std::string lower_display(display_name);
  std::string lower_filter(filter_text);
  std::transform(lower_display.begin(), lower_display.end(), lower_display.begin(),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
                 [](unsigned char c) { return std::tolower(c); });
  std::transform(lower_filter.begin(), lower_filter.end(), lower_filter.begin(),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
                 [](unsigned char c) { return std::tolower(c); });
  return lower_display.find(lower_filter) != std::string::npos;
}

// Helper function to handle double-click navigation (reduces nesting)
void HandleDoubleClickNavigation(std::string_view display_name,  // NOSONAR(cpp:S6009) - display_name is compared with ".." string literal, string_view is appropriate
                                  const std::filesystem::path& entry,
                                  FolderBrowser* browser) {
  if (display_name == "..") {
    browser->NavigateUp();
  } else {
    browser->NavigateTo(entry);
  }
}

void NavigateToRootFallback(FolderBrowser* browser) {
#ifdef _WIN32
  browser->NavigateTo(std::filesystem::path("C:\\Users"));
#else
  if (const char* home = std::getenv("HOME"); home != nullptr) {  // NOLINT(concurrency-mt-unsafe) - home set in init-statement; fallback, UI thread only
    browser->NavigateTo(std::filesystem::path(home));
  } else {
    browser->NavigateTo(std::filesystem::path("/"));
  }
#endif  // _WIN32
}

// Returns true if navigated to initial_path (caller should return); false if should try cwd.
[[nodiscard]] bool TryNavigateToInitialPath(std::string_view initial_path, FolderBrowser* browser) {
  if (initial_path.empty()) {
    return false;
  }
  try {
    const std::string path_str(initial_path);
    const std::filesystem::path path(path_str);
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
      browser->NavigateTo(path);
      return true;
    }
  } catch (const std::filesystem::filesystem_error& e) {
    LOG_ERROR_BUILD("FolderBrowser::Open: invalid initial_path, falling back to cwd: " << e.what());
  } catch (...) {  // NOSONAR(cpp:S1181,cpp:S2738) - Catch-all for any other exception from filesystem/path (e.g. bad_alloc); specific type is filesystem_error above
    LOG_ERROR_BUILD("FolderBrowser::Open: unknown error on initial_path, falling back to cwd");
  }
  return false;
}

// Navigate to current_path(); on exception, log and call NavigateToRootFallback.
void TryNavigateToCurrentPath(FolderBrowser* browser) {
  try {
    browser->NavigateTo(std::filesystem::current_path());
  } catch (const std::filesystem::filesystem_error& e) {
    LOG_ERROR_BUILD("FolderBrowser::Open: current_path failed, falling back to root: " << e.what());
    NavigateToRootFallback(browser);
  } catch (...) {  // NOSONAR(cpp:S1181,cpp:S2738) - Catch-all for any other exception from current_path(); specific type is filesystem_error above
    LOG_ERROR_BUILD("FolderBrowser::Open: unknown error on current_path, falling back to root");
    NavigateToRootFallback(browser);
  }
}

// Helper function to render header section (reduces cognitive complexity)
void RenderHeader(const std::filesystem::path& current_path, FolderBrowser* browser) {  // NOSONAR(cpp:S6010) - current_path stored as string in class, but path type is more appropriate for filesystem operations
  // Current path display
  ImGui::Text("Current Path:");
  ImGui::SameLine();
  ImGui::TextWrapped("%s", current_path.string().c_str());

  ImGui::Spacing();

  // Navigation buttons
  if (ImGui::Button(ICON_FA_ARROW_UP " Up")) {
    browser->NavigateUp();
  }
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_REFRESH " Refresh")) {
    browser->RefreshDirectory();
  }
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_HOME " Home")) {
    NavigateToRootFallback(browser);
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
}

// Helper function to render filter input (reduces cognitive complexity)
void RenderFilterInput(std::string& filter_text, int& selected_index) {
  ImGui::Text("Filter:");
  ImGui::SameLine();
  std::array<char, kFilterBufferSize> filter_buf{};
  strcpy_safe(filter_buf.data(), filter_buf.size(), filter_text.c_str());
  if (ImGui::InputText("##filter", filter_buf.data(), filter_buf.size())) {
    filter_text = filter_buf.data();
    selected_index = -1;
  }

  ImGui::Spacing();
}

// Helper function to render directory list (reduces cognitive complexity)
void RenderDirectoryList(const std::vector<std::filesystem::path>& entries,
                         std::string_view current_path,  // NOSONAR(cpp:S6009) - current_path is only used for path operations, string_view is appropriate
                         std::string_view filter_text,
                         int& selected_index,
                         FolderBrowser* browser) {
  ImGui::BeginChild("##folder_list", ImVec2(0.0F, kFolderListBottomPadding), true, ImGuiWindowFlags_HorizontalScrollbar);  // NOLINT(readability-implicit-bool-conversion) - ImGui API expects int for border

  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - loop-guarded: i < entries.size(); index needed for i==0 check below
    std::string display_name;

    // Special handling for parent directory
    if (i == 0 && std::filesystem::path(current_path).has_parent_path() &&
        entry == std::filesystem::path(current_path).parent_path()) {
      display_name = "..";
    } else {
      display_name = entry.filename().string();
    }

    // Apply filter (extracted to helper function to reduce nesting)
    if (!MatchesFilter(display_name, filter_text)) {
      continue;
    }

    // Selectable item
    if (const bool is_selected = (selected_index == static_cast<int>(i)); ImGui::Selectable(display_name.c_str(), is_selected, 0, ImVec2(0, 0))) {
      selected_index = static_cast<int>(i);
    }

    // Double-click to navigate (extracted to helper function to reduce nesting)
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
      HandleDoubleClickNavigation(display_name, entry, browser);
      selected_index = -1;
    }
  }

  ImGui::EndChild();
}

// Helper function to render action buttons (reduces cognitive complexity)
bool RenderActionButtons(std::string_view current_path, FolderBrowser* browser) {  // NOSONAR(cpp:S6009) - current_path is only checked for empty, string_view is appropriate
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Action buttons
  if (ImGui::Button("Cancel", ImVec2(kActionButtonWidth, 0.0F))) {
    browser->Close();
    ImGui::CloseCurrentPopup();
    return false;
  }
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() + kSelectButtonRightOffset);

  const bool can_select = !current_path.empty();
  if (!can_select) {
    ImGui::BeginDisabled();
  }
  bool result = false;
  if (ImGui::Button("Select", ImVec2(kActionButtonWidth, 0.0F))) {
    result = true;
    ImGui::CloseCurrentPopup();
  }
  if (!can_select) {
    ImGui::EndDisabled();
  }

  return result;
}
}  // namespace

FolderBrowser::FolderBrowser(const char* title)  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - title_ initialized below; other members use in-class initializers
    : title_(title)
{
}

void FolderBrowser::Open(std::string_view initial_path) {
  is_open_ = true;
  has_selected_ = false;
  selected_path_.clear();
  filter_text_.clear();
  selected_index_ = -1;

  if (TryNavigateToInitialPath(initial_path, this)) {
    return;
  }
  TryNavigateToCurrentPath(this);
}

void FolderBrowser::Close() {
  is_open_ = false;
  has_selected_ = false;
  selected_path_.clear();
}

void FolderBrowser::ClearSelected() {
  has_selected_ = false;
  selected_path_.clear();
}

void FolderBrowser::NavigateTo(const std::filesystem::path& path) {
  try {
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
      current_path_ = path.string();
      RefreshDirectory();
    }
  } catch (const std::filesystem::filesystem_error& e) {
    LOG_ERROR_BUILD("FolderBrowser: Failed to navigate to " << path.string() << ": " << e.what());
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all safety net after specific exception type (filesystem_error)
    LOG_ERROR_BUILD("FolderBrowser: Failed to navigate to " << path.string() << ": " << e.what());
  }
}

void FolderBrowser::NavigateUp() {
  try {
    const std::filesystem::path path(current_path_);
    if (path.has_parent_path()) {
      NavigateTo(path.parent_path());
    }
  } catch (const std::filesystem::filesystem_error& e) {
    LOG_ERROR_BUILD("FolderBrowser: Failed to navigate up: " << e.what());
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all safety net after specific exception type (filesystem_error)
    LOG_ERROR_BUILD("FolderBrowser: Failed to navigate up: " << e.what());
  }
}

void FolderBrowser::RefreshDirectory() {
  current_directory_entries_.clear();
  selected_index_ = -1;

  try {
    const std::filesystem::path path(current_path_);
    if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
      return;
    }

    // Add parent directory entry (for navigation)
    if (path.has_parent_path()) {
      current_directory_entries_.push_back(path.parent_path());
    }

    // Add subdirectories
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
      if (entry.is_directory()) {
        current_directory_entries_.push_back(entry.path());
      }
    }

    // Sort entries (parent first, then alphabetically)
    std::sort(current_directory_entries_.begin() + (path.has_parent_path() ? 1 : 0),
              current_directory_entries_.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) {
                return a.filename().string() < b.filename().string();
              });

  } catch (const std::filesystem::filesystem_error& e) {
    LOG_ERROR_BUILD("FolderBrowser: Failed to refresh directory: " << e.what());
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all safety net after specific exception type (filesystem_error)
    LOG_ERROR_BUILD("FolderBrowser: Failed to refresh directory: " << e.what());
  }
}

bool FolderBrowser::Render() {
  if (!is_open_) {
    return false;
  }

  bool result = false;

  // Center the modal
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(kModalPivotHalf, kModalPivotHalf));
  ImGui::SetNextWindowSize(ImVec2(kBrowserWindowWidth, kBrowserWindowHeight), ImGuiCond_Appearing);

  // Open popup if not already open (when Open() was called)
  if (is_open_ && !ImGui::IsPopupOpen(title_.c_str())) {
    ImGui::OpenPopup(title_.c_str());
  }

  if (ImGui::BeginPopupModal(title_.c_str(), &is_open_, ImGuiWindowFlags_NoResize)) {
    // Render header (path display and navigation buttons)
    RenderHeader(std::filesystem::path(current_path_), this);  // NOSONAR(cpp:S6010) - Convert string to path for type consistency

    // Render filter input
    RenderFilterInput(filter_text_, selected_index_);

    // Render directory list
    RenderDirectoryList(current_directory_entries_, current_path_, filter_text_,
                        selected_index_, this);

    // Render action buttons
    if (RenderActionButtons(current_path_, this)) {
      selected_path_ = current_path_;
      has_selected_ = true;
      result = true;
      is_open_ = false; // Mark as closed so popup doesn't reopen
    }

    ImGui::EndPopup();
  } else {
    // Popup was closed
    if (!is_open_) {
      Close();
    }
  }

  return result;
}

} // namespace ui
