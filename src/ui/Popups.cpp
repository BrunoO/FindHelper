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
#include "filters/SizeFilterUtils.h"
#include "filters/TimeFilterUtils.h"
#include "gui/GuiState.h"
#include "gui/ImGuiUtils.h"
#include "ui/IconsFontAwesome.h"
#include "ui/LayoutConstants.h"
#include "ui/Theme.h"
#include "utils/RegexAliases.h"
#include "utils/RegexGeneratorUtils.h"
#include "utils/StdRegexUtils.h"
#include "utils/StringUtils.h"

namespace ui {

// Forward declaration for RegexGeneratorState (defined below)
struct RegexGeneratorState;

namespace {

/**
 * @brief Create SavedSearch from current GUI state
 *
 * Extracts current search parameters from GUI state and creates a SavedSearch structure.
 *
 * @param state GUI state containing current search parameters
 * @param name Name for the saved search
 * @return SavedSearch structure with current search parameters
 */
SavedSearch CreateSavedSearchFromState(
  const GuiState& state,
  std::string_view name) {
  SavedSearch saved;
  saved.name = std::string(name);
  saved.path = state.pathInput;
  saved.extensions = state.extensionInput;
  saved.filename = state.filenameInput;
  saved.foldersOnly = state.foldersOnly;
  saved.caseSensitive = state.caseSensitive;
  saved.timeFilter = TimeFilterToString(state.timeFilter);
  saved.sizeFilter = SizeFilterToString(state.sizeFilter);
  saved.aiSearchDescription = std::string(state.gemini_description_input_.data());
  return saved;
}

/**
 * @brief Save or update a saved search in settings
 *
 * Updates existing saved search if name matches, otherwise adds new one.
 *
 * @param settings Application settings (modified)
 * @param saved Saved search to save or update
 */
void SaveOrUpdateSearch(AppSettings& settings, const SavedSearch& saved) {
  bool updated_existing = false;  // NOLINT(misc-const-correctness) - Variable is modified in loop, cannot be const
  for (auto& existing : settings.savedSearches) {
    if (existing.name == saved.name) {
      existing = saved;
      updated_existing = true;
      break;
    }
  }
  if (!updated_existing) {
    settings.savedSearches.push_back(saved);
  }
  SaveSettings(settings);
}

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
// NOLINTNEXTLINE(performance-enum-size) - Enum size optimization not needed for this small enum (8 values)
using RegexTemplateType = regex_generator_utils::RegexTemplateType;

/**
 * @brief State structure for regex generator popup
 *
 * Maintains state for a single regex generator popup instance, allowing
 * multiple popups (e.g., for path and filename inputs) to have independent
 * state.
 */

struct RegexGeneratorState {

  RegexTemplateType selected_template = RegexTemplateType::StartsWith;

  std::array<char, 256> param1 =
    {};  // ImGui::InputText requires char* buffer (performance-critical, called every frame)

  std::array<char, 256> param2 =
    {};  // ImGui::InputText requires char* buffer (performance-critical, called every frame)

  std::array<char, 256> test_text =
    {};  // NOSONAR(cpp:S125) - Regular comment, not commented-out code. ImGui::InputText requires char* buffer (performance-critical, called every frame)

  bool case_sensitive = true;
  // NOLINTNEXTLINE(readability-redundant-member-init) - POD-like struct, public members intentional; Empty string init is explicit for clarity
  std::string generated_pattern{};
  // NOLINTNEXTLINE(readability-redundant-member-init) - POD-like struct, public members intentional; Empty string init is explicit for clarity
  std::string last_error{};

  bool pattern_valid = false;

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
    "File extension", "Numeric pattern", "Date pattern", "Custom"};

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
  // Build ImGui ID string (not performance-critical, called once per popup open)
  const std::string test_id = "##test" + std::string(popup_id);
  ImGui::InputText(test_id.c_str(), state.test_text.data(), state.test_text.size());

  if (state.pattern_valid && !state.generated_pattern.empty() && state.test_text[0] != '\0') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - fixed 256-element array; index 0 is always valid
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
  if (state.openRegexGeneratorPopup) {
    ImGui::OpenPopup("RegexGeneratorPopup");
    state.openRegexGeneratorPopup = false;
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
  if (state.openRegexGeneratorPopupFilename) {
    ImGui::OpenPopup("RegexGeneratorPopupFilename");
    state.openRegexGeneratorPopupFilename = false;
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
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

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
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    RenderActionButtons(state, target_buffer, buffer_size, popup_id, gui_state);
  } else {
    // Close button when no pattern generated
    if (ImGui::Button(ICON_FA_XMARK " Close", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
      ImGui::CloseCurrentPopup();
    }
  }
}

void Popups::RenderSavedSearchPopups(GuiState& state, AppSettings& settings) {
  // Static state for popups (persists across frames)
  static std::array<char, 128> save_search_name =
    {};  // ImGui::InputText requires char* buffer (performance-critical, called every frame)

  // Save Search Popup (opened by button or Ctrl+S / Cmd+S)
  if (state.openSaveSearchPopup) {
    ImGui::OpenPopup("SaveSearchPopup");
    state.openSaveSearchPopup = false;
  }
  CenterNextWindowInMainWindow();

  if (ImGui::BeginPopupModal("SaveSearchPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Save Current Search");
    ImGui::Separator();
    ImGui::InputText("Name", save_search_name.data(), save_search_name.size());

    ImGui::Spacing();

    if (RenderSaveCancelButtons(ICON_FA_SAVE " Save", save_search_name.data(),
                                [&state, &settings]() {
                                  // Note: save_search_name is static, so it's accessible without
                                  // capturing
                                  if (const std::string name(save_search_name.data()); !name.empty()) {
                                    const SavedSearch saved = CreateSavedSearchFromState(state, name);
                                    SaveOrUpdateSearch(settings, saved);
                                    return true;
                                  }
                                  return false;
                                })) {
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  // Delete Saved Search Popup
  // Center popup in main window every time it appears
  CenterNextWindowInMainWindow();

  if (ImGui::BeginPopupModal("DeleteSavedSearchPopup", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete saved search?");
    ImGui::Separator();

    if (!settings.savedSearches.empty()) {
      // Use the index from state (set when Delete button was clicked)
      // Validate and clamp index to valid range
      int delete_index = state.deleteSavedSearchIndex;
      if (delete_index < 0 || delete_index >= static_cast<int>(settings.savedSearches.size())) {
        // Fallback to last item if index is invalid
        delete_index = static_cast<int>(settings.savedSearches.size()) - 1;
      }

      const SavedSearch& saved_search = settings.savedSearches[static_cast<std::size_t>(delete_index)];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - bounds validated and clamped by the if/else block above
      ImGui::Text("\"%s\"", saved_search.name.c_str());  // NOSONAR(cpp:S5945) - ImGui::Text requires C-style string
    }

    ImGui::Spacing();

    if (ImGui::Button(
          ICON_FA_TRASH " Delete",
          ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {  // NOSONAR(cpp:S134) - Nested if acceptable: ImGui immediate mode pattern requires modal->button->validation nesting
      const int delete_index = state.deleteSavedSearchIndex;
      if (const bool can_delete = !settings.savedSearches.empty() && delete_index >= 0 &&
                                  delete_index < static_cast<int>(settings.savedSearches.size());
          can_delete) {
        // NOLINTNEXTLINE(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions) - delete_index is validated to be in range, narrowing is safe
        settings.savedSearches.erase(settings.savedSearches.begin() +
                                     static_cast<std::size_t>(delete_index));  // NOLINT(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions) - delete_index is validated to be in range, narrowing is safe
        SaveSettings(settings);
      }
      state.deleteSavedSearchIndex = -1;  // Reset index after deletion
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK " Cancel", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
      state.deleteSavedSearchIndex = -1;  // Reset index on cancel
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}

}  // namespace ui // NOSONAR(cpp:S125) - Standard namespace closing comment, not commented-out code
