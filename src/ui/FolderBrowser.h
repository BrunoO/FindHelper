#pragma once

/**
 * @file ui/FolderBrowser.h
 * @brief C++17-compatible ImGui folder browser widget
 *
 * A simple folder selection dialog built with ImGui widgets.
 * Inspired by imgui-file-browser but compatible with C++17.
 */

#include <filesystem>
#include <string>
#include <vector>

namespace ImGui {  // NOLINT(readability-identifier-naming) - external ImGui API namespace
  // Forward declaration
  struct ImGuiContext;
}

namespace ui {

/**
 * @class FolderBrowser
 * @brief Simple folder browser dialog for ImGui
 *
 * Provides a modal dialog for selecting a folder using ImGui widgets.
 * C++17 compatible, no external dependencies beyond ImGui and std::filesystem.
 */
class FolderBrowser {
public:
  /**
   * @brief Construct a new FolderBrowser
   *
   * @param title Window title for the browser dialog
   */
  explicit FolderBrowser(const char* title = "Select Folder");

  /**
   * @brief Open the folder browser dialog
   *
   * @param initial_path Optional initial path to start browsing from
   */
  void Open(std::string_view initial_path = "");

  /**
   * @brief Close the folder browser dialog
   */
  void Close();

  /**
   * @brief Check if the dialog is currently open
   *
   * @return true if dialog is open, false otherwise
   */
  [[nodiscard]] bool IsOpen() const { return is_open_; }

  /**
   * @brief Check if a folder has been selected
   *
   * @return true if a folder was selected, false otherwise
   */
  [[nodiscard]] bool HasSelected() const { return has_selected_; }

  /**
   * @brief Get the selected folder path
   *
   * @return Selected folder path, or empty string if none selected
   */
  [[nodiscard]] std::string GetSelected() const { return selected_path_; }

  /**
   * @brief Clear the selection state
   */
  void ClearSelected();

  /**
   * @brief Render the folder browser dialog
   *
   * Call this every frame when the dialog should be visible.
   * Returns true if a folder was selected this frame.
   */
  bool Render();

  /**
   * @brief Refresh the directory listing
   */
  void RefreshDirectory();

  /**
   * @brief Navigate to a specific path
   *
   * @param path Path to navigate to
   */
  void NavigateTo(const std::filesystem::path& path);

  /**
   * @brief Navigate up one directory level
   */
  void NavigateUp();

private:
  std::string title_;  // NOLINT(readability-identifier-naming) - project uses snake_case_ for members
  bool is_open_ = false;  // NOLINT(readability-identifier-naming)
  bool has_selected_ = false;  // NOLINT(readability-identifier-naming)
  std::string selected_path_;  // NOLINT(readability-identifier-naming)
  std::string current_path_;  // NOLINT(readability-identifier-naming)
  std::vector<std::filesystem::path> current_directory_entries_;  // NOLINT(readability-identifier-naming)
  std::string filter_text_;  // NOLINT(readability-identifier-naming)
  int selected_index_ = -1;  // NOLINT(readability-identifier-naming)
};

} // namespace ui
