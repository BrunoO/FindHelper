/**
 * @file ui/Popups.cpp
 * @brief Implementation of popup dialogs rendering component
 */

#include "ui/Popups.h"

#include <array>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "imgui.h"

#include "core/Settings.h"
#include "gui/GuiState.h"
#include "gui/ImGuiUtils.h"
#include "gui/UIActions.h"
#include "platform/FileOperations.h"
#include "search/SearchHistory.h"
#include "ui/IconsFontAwesome.h"
#include "ui/LayoutConstants.h"
#include "ui/Theme.h"
#include "utils/Logger.h"
#include "utils/RegexAliases.h"
#include "utils/RegexGeneratorUtils.h"
#include "utils/StdRegexUtils.h"
#include "utils/StringUtils.h"

#include <algorithm>
#include <chrono>

namespace ui {

// Forward declaration for RegexGeneratorState (defined below)
struct RegexGeneratorState;

namespace {

/**
 * @brief Insert pattern into target buffer
 *
 * Copies the generated pattern (with rs: prefix) into the target buffer.
 *
 * @param target_buffer Buffer to copy pattern into (modified)
 * @param buffer_size Size of target buffer
 * @param pattern Pattern to insert (without prefix)
 */
void InsertPatternIntoBuffer(char* target_buffer, size_t buffer_size, std::string_view pattern) {
  if (target_buffer == nullptr || buffer_size == 0) {
    return;
  }

  // Use snprintf to avoid string allocation (convert to string for c_str())
  const std::string pattern_str(pattern);
  std::array<char, 512> pattern_with_prefix{};
  const int written = std::snprintf(pattern_with_prefix.data(), pattern_with_prefix.size(), "rs:%s",
                              pattern_str.c_str());
  size_t copy_size = 0;
  if (written > 0) {
    if (static_cast<size_t>(written) < pattern_with_prefix.size()) {
      copy_size = static_cast<size_t>(written);
    } else {
      copy_size = pattern_with_prefix.size() - 1;  // Truncation occurred
    }
  }
  copy_size = (std::min)(copy_size, buffer_size - 1);
  if (copy_size > 0) {
    std::memcpy(target_buffer, pattern_with_prefix.data(), copy_size);

    target_buffer[copy_size] = '\0';
  } else {

    target_buffer[0] = '\0';
  }
}

/**
 * @brief Render save and cancel buttons with name buffer clearing
 *
 * Common pattern for save/cancel button pairs that clear a name buffer.
 * Used to eliminate duplication in popup rendering.
 *
 * @param save_button_label Label for save button (e.g., ICON_FA_SAVE " Save")
 * @param name_buffer Buffer to clear on save/cancel (modified)
 * @param on_save Callback to execute when save is clicked (returns true if popup should close)
 * @return true if popup should close (save was clicked and on_save returned true)
 */
template <typename Callable>
bool RenderSaveCancelButtons(const char* save_button_label, char* name_buffer,
                             const Callable& on_save) {
  bool should_close = false;
  if (ImGui::Button(save_button_label, ImVec2(LayoutConstants::kSecondaryButtonWidth, 0)) && on_save()) {
    if (name_buffer != nullptr) {

      name_buffer[0] = '\0';  // Clear name after saving
    }
    should_close = true;
  }
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_XMARK " Cancel", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
    if (name_buffer != nullptr) {

      name_buffer[0] = '\0';  // Clear name on cancel
    }
    should_close = true;
  }
  return should_close;
}

}  // namespace

// ============================================================================
// Regex Generator Types and State
// ============================================================================

/**
 * @brief Template types for regex pattern generation
 *
 * Used by the regex generator popup to provide common pattern templates
 * that users can customize with parameters.
 */
// Re-export the enum name for this file's regex popup templates (plain using-declaration,
// the preferred form per readability-redundant-qualified-alias).
using regex_generator_utils::RegexTemplateType;

/**
 * @brief State structure for regex generator popup
 *
 * Maintains state for a single regex generator popup instance, allowing
 * multiple popups (e.g., for path and filename inputs) to have independent
 * state.
 */

struct RegexGeneratorState {

  RegexTemplateType selected_template = RegexTemplateType::StartsWith;  // NOLINT(readability-identifier-naming) - ImGui popup state POD

  std::array<char, 256> param1 =  // NOLINT(readability-identifier-naming) - ImGui popup state POD
    {};  // ImGui::InputText requires char* buffer (performance-critical, called every frame)

  std::array<char, 256> param2 =  // NOLINT(readability-identifier-naming) - ImGui popup state POD
    {};  // ImGui::InputText requires char* buffer (performance-critical, called every frame)

  std::array<char, 256> test_text =  // NOLINT(readability-identifier-naming) - ImGui popup state POD
    {};  // NOSONAR(cpp:S125) - Regular comment, not commented-out code. ImGui::InputText requires char* buffer (performance-critical, called every frame)

  bool case_sensitive = true;  // NOLINT(readability-identifier-naming) - ImGui popup state POD
  // NOLINTNEXTLINE(readability-redundant-member-init) - POD-like struct, public members intentional; Empty string init is explicit for clarity
  std::string generated_pattern{};  // NOLINT(readability-identifier-naming) - ImGui popup state POD
  // NOLINTNEXTLINE(readability-redundant-member-init) - POD-like struct, public members intentional; Empty string init is explicit for clarity
  std::string last_error{};  // NOLINT(readability-identifier-naming) - ImGui popup state POD

  bool pattern_valid = false;  // NOLINT(readability-identifier-naming) - ImGui popup state POD

  // NOLINTNEXTLINE(hicpp-use-equals-default,modernize-use-equals-default) - Constructor initializes array members explicitly for clarity
  RegexGeneratorState() {
    param1[0] = '\0';    // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - fixed 256-element array; index 0 is always valid
    param2[0] = '\0';    // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - fixed 256-element array; index 0 is always valid
    test_text[0] = '\0'; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - fixed 256-element array; index 0 is always valid
  }
};

// Implementation of ValidateAndSetPattern (now that RegexGeneratorState is defined)
namespace {
void ValidateAndSetPattern(RegexGeneratorState& state, std::string_view pattern) {
  state.generated_pattern = std::string(pattern);
  if (pattern.empty()) {
    state.pattern_valid = false;
    state.last_error = "Empty pattern";
    return;
  }

  // Validate the pattern (convert to string for std::regex constructor)
  try {
    const std::string pattern_str(pattern);
    [[maybe_unused]] const regex_t test_regex(
      pattern_str, regex_constants::ECMAScript |
                     (state.case_sensitive ? regex_constants::optimize  // NOLINT(hicpp-signed-bitwise) - regex_constants flags are designed to be combined with bitwise OR
                                           : regex_constants::optimize | regex_constants::icase));
    state.pattern_valid = true;
    state.last_error = "";
  } catch (const regex_error_t& e) {
    state.pattern_valid = false;
    // Use snprintf to avoid string allocation
    std::array<char, 512> error_msg{};
    // NOLINTNEXTLINE(cert-err33-c) - snprintf return value not explicitly checked; error_msg.data() is used directly
    std::snprintf(error_msg.data(), error_msg.size(), "Invalid pattern: %s", e.what());
    state.last_error = error_msg.data();
  }
}
}  // namespace

// ============================================================================
// Helper Functions
// ============================================================================

namespace {
// Helper functions for RenderRegexGeneratorPopupContent to reduce cognitive complexity

void RenderTemplateSelection(
  RegexGeneratorState& state) {  // NOSONAR(cpp:S995) - state must be non-const to modify selected_template and reset fields
  ImGui::Text("Template:");
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays,misc-const-correctness) - ImGui::Combo requires C-style array (const char*[])
  const char* const template_names[] = {  // NOSONAR(cpp:S5945) - ImGui::Combo requires C-style array (const char*[])
    "Starts with",    "Ends with",       "Contains",     "Does not contain",
    "File extension", "Numeric pattern", "Date pattern", "Custom",
  };

  auto current_template =
    static_cast<int>(state.selected_template);  // NOSONAR(cpp:S6004) - Variable modified by reference in ImGui, then used after if
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay) - ImGui Combo uses array-to-pointer decay intentionally
  if (ImGui::Combo("##template", &current_template, template_names, IM_ARRAYSIZE(template_names))) {
    state.selected_template = static_cast<RegexTemplateType>(current_template);
    state.generated_pattern = "";
    state.last_error = "";
    state.pattern_valid = false;
  }
}

void RenderParameterInput(RegexGeneratorState& state,
                          const char* popup_id) {  // NOSONAR(cpp:S995) - state must be non-const to modify param1 and reset fields
  if (const bool needs_param1 = state.selected_template != RegexTemplateType::NumericPattern &&
                          state.selected_template != RegexTemplateType::DatePattern;
      needs_param1) {
    const char* param_label =
      (state.selected_template == RegexTemplateType::Custom) ? "Regex pattern:" : "Text:";
    ImGui::Text("%s", param_label);
    // Use snprintf to avoid string allocation per frame
    std::array<char, 256> param_id{};
    // NOLINTNEXTLINE(cert-err33-c) - snprintf return value not explicitly checked; param_id.data() is used directly
    std::snprintf(param_id.data(), param_id.size(), "##param1%s", popup_id);
    if (ImGui::InputText(param_id.data(), state.param1.data(), state.param1.size())) {
      state.generated_pattern = "";
      state.last_error = "";
      state.pattern_valid = false;
    }
    if (state.selected_template == RegexTemplateType::FileExtension) {
      ImGui::TextDisabled("(e.g., cpp|h|hpp for multiple extensions)");
    }
  } else {
    ImGui::TextDisabled("No parameters needed for this template");
  }
}

void RenderGeneratedPattern(const RegexGeneratorState& state) {
  if (state.generated_pattern.empty()) {
    return;
  }

  ImGui::Text("Generated Pattern:");
  ImGui::SameLine();
  ImGui::TextColored(
    state.pattern_valid ? Theme::Colors::Success : Theme::Colors::Error, "rs:%s",
    state.generated_pattern.c_str());

  if (!state.last_error.empty()) {
    ImGui::TextColored(Theme::Colors::Error, "%s", state.last_error.c_str());
  }
}

void RenderTestPreview(RegexGeneratorState& state,
                       const char* popup_id) {  // NOSONAR(cpp:S995) - state must be non-const to modify test_text via ImGui::InputText
  ImGui::Spacing();
  ImGui::Text("Test Preview:");
  std::array<char, 256> test_id{};
  if (const int written = std::snprintf(test_id.data(), test_id.size(), "##test%s", popup_id);
      written < 0) {
    test_id.front() = '\0';
  } else if (static_cast<size_t>(written) >= test_id.size()) {
    const size_t popup_hash = std::hash<std::string_view>{}(popup_id);
    const int hash_written =
      std::snprintf(test_id.data(), test_id.size(), "##test_%zx", popup_hash);
    if (hash_written < 0) {
      test_id.front() = '\0';
    }
  }
  ImGui::InputText(test_id.data(), state.test_text.data(), state.test_text.size());

  if (state.pattern_valid && !state.generated_pattern.empty() && state.test_text.front() != '\0') {
    const bool matches = std_regex_utils::RegexMatch(
      state.generated_pattern, std::string_view(state.test_text.data()), state.case_sensitive);
    if (matches) {
      // NOLINTNEXTLINE(readability-magic-numbers) - ImGui color values (RGBA) are self-explanatory
      ImGui::TextColored(Theme::Colors::Success, "[OK] Matches");
    } else {
      ImGui::TextColored(Theme::Colors::TextDim, "[NO] No match");
    }
  }
}

void RenderActionButtons(
  const RegexGeneratorState& state,
  char* target_buffer,  // target_buffer modified via InsertPatternIntoBuffer
  size_t buffer_size, const char* popup_id,
  GuiState* gui_state) {
  if (state.pattern_valid) {
    // NOLINTNEXTLINE(readability-magic-numbers) - Button width in pixels is self-explanatory
    if (ImGui::Button("Insert into Search", ImVec2(180, 0))) {
      InsertPatternIntoBuffer(target_buffer, buffer_size, state.generated_pattern);

      // If this popup is bound to the main Path Search field, mark input as
      // changed so SearchController can trigger a search immediately (or on
      // the next debounce tick). This keeps behavior consistent with typing
      // in the field and pressing Enter.
      if (std::strcmp(popup_id, "path") == 0 && (gui_state != nullptr)) {
        gui_state->MarkInputChanged();
      }

      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
  }

  if (ImGui::Button(ICON_FA_XMARK " Close", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
    ImGui::CloseCurrentPopup();
  }
}

}  // anonymous namespace

// ============================================================================
// Public Rendering Methods
// ============================================================================


void Popups::RenderRegexGeneratorPopup(char* target_buffer, size_t buffer_size, GuiState& state) {
  if (state.ui_visibility.open_regex_generator_popup) {
    ImGui::OpenPopup("RegexGeneratorPopup");
    state.ui_visibility.open_regex_generator_popup = false;
  }

  // Center popup in main window every time it appears
  CenterNextWindowInMainWindow();
  // NOLINTNEXTLINE(readability-magic-numbers) - Window width in pixels is self-explanatory
  ImGui::SetNextWindowSize(ImVec2(450, 0), ImGuiCond_FirstUseEver);
  if (ImGui::BeginPopupModal("RegexGeneratorPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Regex Generator");
    ImGui::Separator();
    bool is_open = true;  // Dummy for function signature compatibility with existing internal logic
                          // if kept, but better to simplify
    RenderRegexGeneratorPopupContent(target_buffer, buffer_size, "path", is_open,
                                     "RegexGeneratorPopup", &state);
    ImGui::EndPopup();
  }
}

void Popups::RenderRegexGeneratorPopupFilename(char* target_buffer, size_t buffer_size,
                                               GuiState& state) {
  if (state.ui_visibility.open_regex_generator_popup_filename) {
    ImGui::OpenPopup("RegexGeneratorPopupFilename");
    state.ui_visibility.open_regex_generator_popup_filename = false;
  }

  // Center popup in main window every time it appears
  CenterNextWindowInMainWindow();
  // NOLINTNEXTLINE(readability-magic-numbers) - Window width in pixels is self-explanatory
  ImGui::SetNextWindowSize(ImVec2(450, 0), ImGuiCond_FirstUseEver);
  if (ImGui::BeginPopupModal("RegexGeneratorPopupFilename", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Regex Generator");
    ImGui::Separator();
    bool is_open = true;  // Dummy
    RenderRegexGeneratorPopupContent(target_buffer, buffer_size, "filename", is_open,
                                     "RegexGeneratorPopupFilename", &state);
    ImGui::EndPopup();
  }
}

void Popups::RenderRegexGeneratorPopupContent(char* target_buffer, size_t buffer_size,
                                              const char* popup_id, [[maybe_unused]] bool& is_open,
                                              [[maybe_unused]] const char* popup_name,
                                              GuiState* gui_state) {
  // Static state for the generator (per popup instance)
  // Bounded: Only "path" and "filename" popup IDs are used, so map will have
  // at most 2 entries
  static std::unordered_map<std::string, RegexGeneratorState> popup_states;

  auto& state = popup_states[popup_id];

  // Template selection
  RenderTemplateSelection(state);
  ImGui::Spacing();

  // Parameter input based on template
  RenderParameterInput(state, popup_id);
  ImGui::Spacing();

  // Case sensitive checkbox
  ImGui::Checkbox("Case sensitive", &state.case_sensitive);
  SeparatorWithSpacing();

  // Generate pattern button
  // NOLINTNEXTLINE(readability-magic-numbers) - Button width in pixels is self-explanatory
  if (ImGui::Button("Generate Pattern", ImVec2(180, 0))) {
    const std::string pattern = regex_generator_utils::GenerateRegexPattern(
      state.selected_template, std::string_view(state.param1.data()),
      std::string_view(state.param2.data()));
    ValidateAndSetPattern(state, pattern);
  }
  ImGui::Spacing();

  // Display generated pattern
  RenderGeneratedPattern(state);

  // Test preview and action buttons (only shown if pattern was generated)
  if (!state.generated_pattern.empty()) {
    RenderTestPreview(state, popup_id);
    SeparatorWithSpacing();
    RenderActionButtons(state, target_buffer, buffer_size, popup_id, gui_state);
  } else {
    // Close button when no pattern generated
    if (ImGui::Button(ICON_FA_XMARK " Close", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
      ImGui::CloseCurrentPopup();
    }
  }
}


void Popups::RenderHistoryRenamePopup(GuiState& state, AppSettings& settings,
                                      UIActions* actions) {
  if (!state.history.pending_rename_id.empty()) {
    ImGui::OpenPopup("HistoryRenamePopup");
  }
  CenterNextWindowInMainWindow();

  if (ImGui::BeginPopupModal("HistoryRenamePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Rename search");
    ImGui::Separator();

    // One-time pre-fill: when the popup just opened, seed the buffer with the current name.
    static std::array<char, 128> rename_buffer = {};
    static std::string last_seeded_id;
    if (last_seeded_id != state.history.pending_rename_id) {
      last_seeded_id = state.history.pending_rename_id;
      rename_buffer[0] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      const auto it = std::find_if(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
          settings.searchHistory.begin(), settings.searchHistory.end(),
          [&state](const SearchHistoryEntry& e) { return e.id == state.history.pending_rename_id; });
      if (it != settings.searchHistory.end() && !it->custom_name.empty()) {
        strcpy_safe(rename_buffer.data(), rename_buffer.size(), it->custom_name.c_str());
      }
    }

    ImGui::InputText("Name", rename_buffer.data(), rename_buffer.size());
    ImGui::Spacing();

    if (ImGui::Button(ICON_FA_SAVE " Save", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
      RenameHistoryEntry(state.history.pending_rename_id, rename_buffer.data(), settings);
      if (actions == nullptr || !actions->PersistSettings()) {
        LOG_ERROR("Failed to persist search history rename");
      }
      state.history.pending_rename_id.clear();
      last_seeded_id.clear();
      rename_buffer[0] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK " Cancel", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
      state.history.pending_rename_id.clear();
      last_seeded_id.clear();
      rename_buffer[0] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}

void Popups::RenderHistoryDeletePopup(GuiState& state, AppSettings& settings,
                                      UIActions* actions) {
  if (!state.history.pending_delete_id.empty()) {
    ImGui::OpenPopup("HistoryDeletePopup");
  }
  CenterNextWindowInMainWindow();

  if (ImGui::BeginPopupModal("HistoryDeletePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete search history entry?");
    ImGui::Separator();

    if (const auto it = std::find_if(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
            settings.searchHistory.begin(), settings.searchHistory.end(),
            [&state](const SearchHistoryEntry& e) { return e.id == state.history.pending_delete_id; });
        it != settings.searchHistory.end()) {
      const HistoryEntryDisplayInfo info = BuildHistoryEntryDisplayInfo(*it);
      ImGui::TextUnformatted(info.headline.c_str());
    }

    ImGui::Spacing();

    if (ImGui::Button(ICON_FA_TRASH " Delete", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
      DeleteHistoryEntry(state.history.pending_delete_id, settings);
      if (actions == nullptr || !actions->PersistSettings()) {
        LOG_ERROR("Failed to persist search history deletion");
      }
      state.history.pending_delete_id.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK " Cancel", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
      state.history.pending_delete_id.clear();
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}

namespace {

void RenderExportSuccessContent(const GuiState& state) {
  ImGui::TextColored(Theme::Colors::Success, "%s", ICON_FA_CHECK " Export Successful");
  ImGui::Spacing();
  if (state.export_workflow.result_count == 1) {
    ImGui::Text("Exported 1 result to:");
  } else {
    ImGui::Text("Exported %zu results to:", state.export_workflow.result_count);
  }
  ImGui::Spacing();
  ImGui::TextWrapped("%s", state.export_workflow.file_path.c_str());
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (!state.export_workflow.file_path.empty()) {
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " Show in Folder")) {
      file_operations::OpenParentFolder(state.export_workflow.file_path);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Open containing folder and select the exported file");
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_COPY " Copy Path")) {
      ImGui::SetClipboardText(state.export_workflow.file_path.c_str());
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Copy file path to clipboard");
    }

    ImGui::SameLine();
  }

  if (ImGui::Button(ICON_FA_CHECK " OK", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0)) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::SetItemDefaultFocus();
}

void RenderExportFailureContent(const GuiState& state) {
  ImGui::TextColored(Theme::Colors::Error, "%s", ICON_FA_BAN " Export Failed");
  ImGui::Spacing();
  const std::string_view error_msg = !state.export_workflow.error_message.empty()
                                         ? std::string_view{state.export_workflow.error_message}
                                         : std::string_view{"Unknown error occurred while exporting CSV."};
  ImGui::TextWrapped("%.*s", static_cast<int>(error_msg.size()), error_msg.data());

  if (!state.export_workflow.file_path.empty()) {
    ImGui::Spacing();
    ImGui::TextDisabled("Target path:");
    ImGui::TextWrapped("%s", state.export_workflow.file_path.c_str());
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button(ICON_FA_XMARK " Close", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0)) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::SetItemDefaultFocus();
}

}  // namespace

void Popups::RenderExportResultPopup(GuiState& state) {
  constexpr const char* kExportPopupId = "Export to CSV";

  if (state.export_workflow.show_popup) {
    ImGui::OpenPopup(kExportPopupId);
    state.export_workflow.show_popup = false;
  }

  CenterNextWindowInMainWindow();
  constexpr float kMinWidth = 480.0F;
  constexpr float kMaxWidth = 750.0F;
  constexpr float kMaxHeight = 600.0F;
  ImGui::SetNextWindowSizeConstraints(ImVec2(kMinWidth, 0.0F), ImVec2(kMaxWidth, kMaxHeight));

  if (ImGui::BeginPopupModal(kExportPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (state.export_workflow.success) {
      RenderExportSuccessContent(state);
    } else {
      RenderExportFailureContent(state);
    }

    ImGui::EndPopup();
  }
}

}  // namespace ui // NOSONAR(cpp:S125) - Standard namespace closing comment, not commented-out code
