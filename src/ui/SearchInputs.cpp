/**
 * @file ui/SearchInputs.cpp
 * @brief Implementation of search input fields rendering component
 */

#include "ui/SearchInputs.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include <GLFW/glfw3.h>

#include "imgui.h"

#include "core/Settings.h"
#include "filters/TimeFilterUtils.h"
#include "gui/GuiState.h"
#include "gui/ImGuiUtils.h"
#include "gui/UIActions.h"
#include "search/SearchResultsService.h"
#include "ui/IconsFontAwesome.h"
#include "ui/SearchInputsGeminiHelpers.h"
#include "ui/Theme.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

namespace ui {

namespace {

/** Pushes theme-aware ghost style for icon-only buttons (transparent bg, dim text, themed hover/active). */
void PushGhostIconButtonStyle() {
  ImVec4 ghost_bg = Theme::Colors::Surface;
  ghost_bg.w = 0.0F;
  ImVec4 ghost_hover = Theme::Colors::SurfaceHover;
  ghost_hover.w = 0.5F;
  ImVec4 ghost_active = Theme::Colors::SurfaceActive;
  ghost_active.w = 0.7F;
  ImGui::PushStyleColor(ImGuiCol_Button, ghost_bg);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ghost_hover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ghost_active);
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
}

/**
 * @brief Calculate full-width input field width with minimum constraint
 *
 * Calculates the width for an input field that should use the full available width,
 * respecting a minimum width constraint.
 *
 * @param available_width Available width in pixels
 * @param min_width Minimum width constraint
 * @return Calculated width (full available width, or minimum if available is too small)
 */
float CalculateFullWidthInputWidth(float available_width, float min_width) {
  return (std::max)(available_width, min_width);
}

// Helper functions for RenderAISearch to reduce cognitive complexity
// Inlined for performance (called every frame during UI rendering)

// Calculate layout dimensions for AI search input field
inline void CalculateAISearchLayout(bool api_key_set, float available_width,
                                           float& out_button_width, float& out_input_width, float& out_input_height) {
  // Calculate button widths to reserve space on the same line
  if (api_key_set) {
    out_button_width = ComputeButtonWidth("Help Me Search");
  } else {
    const float generate_prompt_width = ComputeButtonWidth("Generate Prompt");
    const float paste_clipboard_width = ComputeButtonWidth("Paste Prompt from Clipboard");
    out_button_width = (std::max)(generate_prompt_width, paste_clipboard_width);
  }
  
  // Calculate width for multi-line input
  constexpr float input_right_margin = 8.0F;
  constexpr float button_input_spacing = 8.0F;
  constexpr float min_ai_input_width = 300.0F;
  out_input_width = (std::max)(available_width - out_button_width - button_input_spacing - input_right_margin,
                               min_ai_input_width);
  
  // Calculate height to match 2 stacked buttons
  const float button_height = ImGui::GetFrameHeight();
  const ImGuiStyle& style = ImGui::GetStyle();
  out_input_height = (button_height * 2.0F) + style.ItemSpacing.y;
}

// Render AI search input field with placeholder
inline void RenderAISearchInputField(GuiState& state, float input_width, float input_height,
                                      bool api_key_set) {
  const char* placeholder_text = api_key_set
    ? "Describe what you're looking for (e.g., 'PDF files from last week')"
    : "One-click AI search requires API key. Use Generate/Paste Prompt buttons to use Copilot/ChatGPT/etc. \n or set GEMINI_API_KEY";
  const bool is_input_empty = state.gemini_description_input_[0] == '\0';
  
  ImGui::SetNextItemWidth(input_width);
  ImGui::InputTextMultiline("##gemini_description", state.gemini_description_input_.data(),
                            state.gemini_description_input_.size(),
                            ImVec2(input_width, input_height),
                            ImGuiInputTextFlags_None);
  
  // Draw placeholder text when input is empty
  if (is_input_empty) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 text_pos = ImGui::GetItemRectMin();
    text_pos.x += style.FramePadding.x;
    text_pos.y += style.FramePadding.y;
    ImGui::GetWindowDrawList()->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), placeholder_text);
  }
}

// Render tooltips for AI search input field
inline void RenderAISearchTooltips(bool api_key_set) {
  if (!ImGui::IsItemHovered()) {
    return;
  }
  
  ImGui::BeginTooltip();
  if (api_key_set) {
    ImGui::Text("Examples:");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::BulletText("'PDF files from last week'");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::BulletText("'Large video files on desktop'");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::BulletText("'Documents modified today'");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::BulletText("'Images larger than 5MB'");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
  } else {
    ImGui::Text(ICON_FA_LIGHTBULB " Use 'Generate Prompt' button to create a search prompt.");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::Text("Then paste it into your AI assistant (Copilot, ChatGPT, etc.)");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::Text("and paste the JSON response back using 'Paste Prompt from Clipboard'.");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::Spacing();
    ImGui::TextDisabled("(Optional: Set GEMINI_API_KEY for one-click AI search)");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
  }
  ImGui::EndTooltip();
}

// Render buttons for AI search (Help Me Search or Generate/Paste buttons)
inline void RenderAISearchButtons(GuiState& state, GLFWwindow* window, UIActions* actions,
                                          bool api_key_set, bool has_description,
                                          bool is_index_building, bool is_api_call_in_progress) {
  ImGui::SameLine();
  
  if (is_index_building || is_api_call_in_progress) {
    ImGui::BeginDisabled();
  }
  
  if (api_key_set) {
    if (ImGui::Button("Help Me Search")) {
      ui::HandleHelpMeSearchButton(state, actions, is_index_building, is_api_call_in_progress, has_description);
    }
  } else {
    ImGui::BeginGroup();
    if (has_description && window == nullptr) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Generate Prompt")) {
      ui::HandleGeneratePromptButton(state, window, actions, is_index_building, is_api_call_in_progress, has_description);
    }
    if (has_description && window == nullptr) {
      ImGui::EndDisabled();
    }
    if (ImGui::Button("Paste Prompt from Clipboard")) {
      ui::HandlePasteFromClipboardButton(state, window);
    }
    ImGui::EndGroup();
  }
  
  if (is_index_building || is_api_call_in_progress) {
    ImGui::EndDisabled();
  }
}

// Render status/error messages for AI search
inline void RenderAISearchStatusMessages(GuiState& state, bool api_key_set, bool is_api_call_in_progress) {
  // Show loading indicator
  if (api_key_set && is_api_call_in_progress) {
    ImGui::TextDisabled("Generating...");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
  }
  
  // Check if async call is complete
  if (state.gemini_api_call_in_progress_ && state.gemini_api_future_.valid()) {
    auto status = state.gemini_api_future_.wait_for(std::chrono::seconds(0));
    if (status == std::future_status::ready) {
      ui::ProcessGeminiApiResult(state);
    }
  }
  
  // Display status/error message if present and not expired
  if (!state.gemini_error_message_.empty()) {
    auto now = std::chrono::steady_clock::now();
    if (now < state.gemini_error_display_time_) {
      ImGui::Spacing();
      const bool is_success = state.gemini_error_message_.find("✅") == 0;
      const bool is_error = state.gemini_error_message_.find("Error:") == 0 || 
                            state.gemini_error_message_.find("Failed") == 0 ||
                            state.gemini_error_message_.find("Warning:") == 0;
      
      if (is_success) {
        ImGui::TextColored(Theme::Colors::Success, "%s", state.gemini_error_message_.c_str());  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
      } else if (is_error) {
        ImGui::TextColored(Theme::Colors::Error, "%s", state.gemini_error_message_.c_str());  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
      } else {
        ImGui::TextWrapped("%s", state.gemini_error_message_.c_str());  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
      }
    } else {
      state.gemini_error_message_ = "";
    }
  }
}

// Helper functions for Render to reduce cognitive complexity
// Inlined for performance (called every frame during UI rendering)

// Calculate maximum button width for uniform column alignment
inline float CalculateMaxButtonWidth(const GuiState& state) {
  const float search_now_width = ComputeButtonWidth(ICON_FA_MAGNIFYING_GLASS " Search now");
  const float clear_all_width = ComputeButtonWidth(ICON_FA_ERASER " Clear All");
  const float export_csv_width = ComputeButtonWidth(ICON_FA_FILE_EXPORT " Export CSV");
  const char* quick_filters_label = state.showQuickFilters ? ICON_FA_FILTER " Hide Quick Filters" : ICON_FA_FILTER " Show Quick Filters";
  const float quick_filters_width = ComputeButtonWidth(quick_filters_label);
  // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - initialized by (std::max) below
  float max_width = (std::max)(search_now_width, clear_all_width);
  max_width = (std::max)(max_width, export_csv_width);
  max_width = (std::max)(max_width, quick_filters_width);
  return max_width;
}

// Render path search input field with search button
inline bool RenderPathSearchInput(GuiState& state, float available_width, float button_area_width,
                                          float uniform_button_width, bool is_index_building, UIActions* actions) {
  constexpr float input_right_margin = 8.0F;
  constexpr float path_search_min_width = 250.0F;
  const float path_available = available_width - input_right_margin - button_area_width;
  const float path_width = CalculateFullWidthInputWidth(path_available, path_search_min_width);
  
  constexpr const char* path_placeholder =
#ifdef _WIN32
      "e.g., C:\\projects\\src or regex pattern or path pattern"
#else
      "e.g., /projects/src or regex pattern or path pattern"
#endif  // _WIN32
      ;

  bool enter_pressed = false;
  if (SearchInputs::RenderInputFieldWithEnter("Path Search", "##path", state.pathInput.Data(),
                                SearchInputField::MaxLength(), state,
                                {path_width, true, true, false, button_area_width, path_placeholder})) {
    enter_pressed = true;
  }
  
  ImGui::SameLine();
  if (is_index_building) {
    ImGui::BeginDisabled();
  }
  
  ImGui::PushStyleColor(ImGuiCol_Button, Theme::Colors::Accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Colors::AccentHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Colors::AccentActive);
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextOnAccent);

  if (ImGui::Button(ICON_FA_MAGNIFYING_GLASS " Search now", ImVec2(uniform_button_width, 0)) && actions != nullptr) {
    actions->TriggerManualSearch(state);
  }
  
  ImGui::PopStyleColor(4);
  
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("Force an immediate search with current parameters.");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::Text("Shortcut: Enter in any search input field");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::EndTooltip();
  }
  if (is_index_building) {
    ImGui::EndDisabled();
  }
  
  return enter_pressed;
}

// Render extensions input field with clear button
inline bool RenderExtensionsInput(GuiState& state, float button_area_width, float uniform_button_width) {
  bool enter_pressed = false;
  if (SearchInputs::RenderInputFieldWithEnter(
          "Extensions", "##extensions", state.extensionInput.Data(),
          SearchInputField::MaxLength(), state,
          {0.0F, false, false, false, button_area_width, "e.g., txt;pdf;docx", true})) {
    enter_pressed = true;
  }
  
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("Enter file extensions separated by semicolons (;).");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::Text("Example: txt;pdf;doc;docx");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::Text("Leading dots and spaces are automatically removed.");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::Text("Extensions are case-insensitive.");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::EndTooltip();
  }
  
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_ERASER " Clear All", ImVec2(uniform_button_width, 0))) {
    state.ClearInputs();
    state.timeFilter = TimeFilter::None;
  }
  
  return enter_pressed;
}

// Render name input field with Export CSV button (vacant space left by Help move to application bar)
inline bool RenderFilenameInput(GuiState& state, float available_width, float button_area_width,
                                float uniform_button_width, UIActions* actions) {
  constexpr float input_right_margin = 8.0F;
  constexpr float filename_min_width = 250.0F;
  const float filename_available = available_width - input_right_margin - button_area_width;
  const float filename_width = CalculateFullWidthInputWidth(filename_available, filename_min_width);

  const bool should_focus = state.focusFilenameInput;
  if (should_focus) {
    state.focusFilenameInput = false;
  }

  bool enter_pressed = false;
  if (const float final_filename_width = filename_width;
      SearchInputs::RenderInputFieldWithEnter(
          "Name", "##filename", state.filenameInput.Data(),
          SearchInputField::MaxLength(), state,
          {final_filename_width, true, true, should_focus, button_area_width,
           "e.g., report* or *.log", true})) {
    enter_pressed = true;
  }

  ImGui::SameLine();
  const auto* results = search::SearchResultsService::GetDisplayResults(state);
  const bool can_export = results != nullptr && !results->empty();
  if (!can_export) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button(ICON_FA_FILE_EXPORT " Export CSV", ImVec2(uniform_button_width, 0)) &&
      actions != nullptr) {
    actions->ExportToCsv(state);
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("Export currently displayed results to CSV file (Downloads, or Desktop if unavailable)");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
  }
  if (!can_export) {
    ImGui::EndDisabled();
  }

  return enter_pressed;
}

// Render search options checkboxes
inline void RenderSearchOptions(GuiState& state, bool is_index_building, float uniform_button_width) {
  ImGui::Spacing();
  ImGui::AlignTextToFramePadding();
  ImGui::Text(ICON_FA_COG " Search Options:");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
  ImGui::SameLine();
  ImGui::Checkbox("Folders Only", &state.foldersOnly);

  ImGui::SameLine();
  bool case_insensitive = !state.caseSensitive;
  ImGui::Checkbox("Case-Insensitive", &case_insensitive);
  state.caseSensitive = !case_insensitive;
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("When checked (default), search ignores case.\nWhen unchecked, search is case-sensitive.");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::EndTooltip();
  }

  ImGui::SameLine();
  if (is_index_building) {
    ImGui::BeginDisabled();
  }
  ImGui::Checkbox("Auto-refresh", &state.autoRefresh);
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("Re-run search automatically when files are added/deleted.\nKeeps results up-to-date with file system changes.");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::EndTooltip();
  }
  if (is_index_building) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  ImGui::Checkbox("Search as you type", &state.instantSearch);
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("When checked, search runs automatically as you type (with debounce).\nWhen unchecked (default), searches only run when you press Enter or click Search.");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
    ImGui::EndTooltip();
  }

  ImGui::SameLine();
  AlignGroupRight(uniform_button_width);
  const char* quick_filters_label = state.showQuickFilters ? ICON_FA_FILTER " Hide Quick Filters" : ICON_FA_FILTER " Show Quick Filters";
  if (ImGui::Button(quick_filters_label, ImVec2(uniform_button_width, 0))) {
    state.showQuickFilters = !state.showQuickFilters;
  }
}

} // namespace

bool SearchInputs::RenderInputFieldWithEnter(const char *label, const char *id,
                                             char *buffer, size_t buffer_size,
                                             GuiState &state,
                                             const InputFieldOptions& options) {
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s:", label);  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API uses varargs
  if (options.is_optional) {
    ImGui::SameLine(0.0F, 2.0F);  // Small spacing between label and "(optional)"
    ImGui::TextDisabled("(optional)");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API uses varargs
  }
  ImGui::SameLine();

  // Push ID scope to ensure unique IDs for help/generator buttons
  // This prevents ID conflicts when multiple input fields are rendered
  ImGui::PushID(id);

  if (options.show_help) {
    ImGui::AlignTextToFramePadding();
    PushGhostIconButtonStyle();

    // Use FontAwesome question circle icon
    // ImGui automatically brightens text on hover for better contrast
    // ID is scoped by PushID above, so each button has unique ID
    // Show "Hide Search Help" when window is open, icon only when closed (like Settings/Metrics)
    if (ImGui::SmallButton(state.showSearchHelpWindow ? ICON_FA_CIRCLE_QUESTION " Hide Search Help##SearchHelpToggle"
                                                      : ICON_FA_CIRCLE_QUESTION "##SearchHelpToggle")) {
      // Toggle search help window
      state.showSearchHelpWindow = !state.showSearchHelpWindow;
    }
    
    // Show tooltip with improved, more explicit text
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::Text("Click for help: Search syntax and patterns");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API uses varargs
      ImGui::EndTooltip();
    }
    
    ImGui::PopStyleColor(4);
    ImGui::SameLine();
  }

  if (options.show_generator) {
    ImGui::AlignTextToFramePadding();
    PushGhostIconButtonStyle();

    // Use FontAwesome code icon for regex generator
    // ImGui automatically brightens text on hover for better contrast
    // ID is scoped by PushID above, so each button has unique ID
    if (ImGui::SmallButton(ICON_FA_CODE)) {
      // Pop ID temporarily to set flag
      ImGui::PopID();
      // Defer popup opening to parent window level (after EndChild)
      // This ensures BeginPopupModal() can find the popup when called at parent window level
      if (std::strcmp(id, "##filename") == 0) {
        state.openRegexGeneratorPopupFilename = true;
      } else {
        state.openRegexGeneratorPopup = true;
      }
      ImGui::PushID(id); // Restore ID scope
    }
    
    // Show tooltip
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::Text("Click to open Regex Generator");  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API uses varargs
      ImGui::EndTooltip();
    }
    
    ImGui::PopStyleColor(4);
    ImGui::SameLine();
  }

  // Pop ID scope
  ImGui::PopID();

  // Recalculate available width after rendering label and buttons to ensure proper margin
  // This ensures the input field respects the right margin even after label/buttons are rendered
  const float window_right_edge = ImGui::GetWindowContentRegionMax().x;
  const float current_cursor_x = ImGui::GetCursorPosX();
  const float current_available_width = window_right_edge - current_cursor_x;
  constexpr float input_right_margin = 8.0F;  // Small margin between input field and window border

  // Account for reserved space on the right (for action buttons)
  const float width_with_margin = current_available_width - input_right_margin - options.reserved_right_space;

  // Use the recalculated width with margin (after label/buttons are rendered). Ensure minimum width.
  constexpr float min_input_width = 100.0F;
  // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - initialized by (std::max) below
  float final_width = (std::max)(width_with_margin, min_input_width);
  
  // Also respect the suggested width if it's smaller (to prevent overflow)
  if (options.width > 0.0F && options.width < final_width) {
    final_width = options.width;
  }

  ImGui::SetNextItemWidth(final_width);
  
  // Set focus if requested (must be called right before InputText)
  if (options.request_focus) {
    ImGui::SetKeyboardFocusHere();
  }
  
  // Use InputTextWithHint if placeholder is provided, otherwise use InputText
  bool enter_pressed = false;
  if (options.placeholder != nullptr) {
    enter_pressed = ImGui::InputTextWithHint(id, options.placeholder, buffer, buffer_size,
                                              ImGuiInputTextFlags_EnterReturnsTrue);
  } else {
    enter_pressed = ImGui::InputText(id, buffer, buffer_size,
                                      ImGuiInputTextFlags_EnterReturnsTrue);
  }

  if (ImGui::IsItemEdited()) {
    state.MarkInputChanged();
  }

  return enter_pressed;
}

void SearchInputs::RenderAISearch(GuiState &state, GLFWwindow* window,
                                  UIActions* actions,
                                  bool is_index_building,
                                  [[maybe_unused]] bool *show_settings, [[maybe_unused]] bool *show_metrics,
                                  [[maybe_unused]] bool metrics_available) {
  // Calculate available width for layout
  const float window_right_edge = ImGui::GetWindowContentRegionMax().x;
  const float cursor_x = ImGui::GetCursorPosX();
  const float available_width = window_right_edge - cursor_x;

  // Check if API key is set to determine workflow
  const std::string api_key = gemini_api_utils::GetGeminiApiKeyFromEnv();
  const bool api_key_set = !api_key.empty();
  const bool is_api_call_in_progress = state.gemini_api_call_in_progress_;
  const bool has_description = state.gemini_description_input_[0] != '\0';

  // Calculate layout dimensions
  float button_width = 0.0F;
  float input_width = 0.0F;
  float input_height = 0.0F;
  CalculateAISearchLayout(api_key_set, available_width, button_width, input_width, input_height);

  // Render input field with placeholder
  RenderAISearchInputField(state, input_width, input_height, api_key_set);
  
  // Render tooltips
  RenderAISearchTooltips(api_key_set);
  
  // Render buttons
  RenderAISearchButtons(state, window, actions, api_key_set, has_description,
                        is_index_building, is_api_call_in_progress);
  
  // Render status messages
  RenderAISearchStatusMessages(state, api_key_set, is_api_call_in_progress);
}

void SearchInputs::Render(GuiState &state,
                          UIActions* actions,
                          bool is_index_building,
                          const AppSettings& settings,
                          [[maybe_unused]] GLFWwindow* window) {
  bool enter_pressed = false;

  // Get available width for consistent width calculations
  // Use window content region max to get the absolute right edge, accounting for window padding
  const float window_right_edge = ImGui::GetWindowContentRegionMax().x;
  const float cursor_x = ImGui::GetCursorPosX();
  const float available_width = window_right_edge - cursor_x;
  
  // Width calculation constants
  constexpr float button_input_spacing = 8.0F;  // Spacing between input field and button

  // Calculate button widths for right-aligned buttons
  const float uniform_button_width = CalculateMaxButtonWidth(state);
  const float button_area_width = uniform_button_width + button_input_spacing;

  // Render Path Search input with search button
  if (RenderPathSearchInput(state, available_width, button_area_width, uniform_button_width, is_index_building, actions)) {
    enter_pressed = true;
  }

  // Sync instantSearch and autoRefresh when Simplified or Minimalistic UI is enabled
  if (settings.uiMode != AppSettings::UIMode::Full) {
    state.instantSearch = true;
    state.autoRefresh = true;
  }

  // Hide advanced options for Simplified and Minimalistic modes
  if (settings.uiMode == AppSettings::UIMode::Full) {
    ImGui::Spacing();
    
    // Render Extensions input with clear button
    if (RenderExtensionsInput(state, button_area_width, uniform_button_width)) {
      enter_pressed = true;
    }

    ImGui::Spacing();

    // Render Filename input with Export CSV button
    if (RenderFilenameInput(state, available_width, button_area_width, uniform_button_width, actions)) {
      enter_pressed = true;
    }

    // Render search options checkboxes
    RenderSearchOptions(state, is_index_building, uniform_button_width);
  }

  // Handle Enter key press from search input fields only (plain Enter; Cmd+Enter/Ctrl+Enter reserved for result table: reveal in Explorer).
  if (enter_pressed && !is_index_building && actions != nullptr) {
    actions->TriggerManualSearch(state);
  }
}

} // namespace ui // NOSONAR(cpp:S125) - Standard namespace closing comment, not commented-out code


