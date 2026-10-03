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
#include "utils/ClipboardUtils.h"
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
inline void RenderAISearchInputField(GuiState& state, float input_width, float input_height,  // NOLINT(misc-const-correctness) - state.gemini.description_input is passed to ImGui::InputTextMultiline which mutates buffer
                                      bool api_key_set) {
  const char* placeholder_text = api_key_set
    ? "Describe what you're looking for (e.g., 'PDF files from last week')"
    : "One-click AI search requires API key. Use Generate/Paste Prompt buttons to use Copilot/ChatGPT/etc. \n or set GEMINI_API_KEY or OPENCODE_API_KEY";
  const bool is_input_empty = state.gemini.description_input.at(0) == '\0';

  ImGui::SetNextItemWidth(input_width);
  ImGui::InputTextMultiline("##gemini_description", state.gemini.description_input.data(),
                            state.gemini.description_input.size(),
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
  if (api_key_set) {  // NOLINT(bugprone-branch-clone) - false positive; branches render completely different help texts
    ImGui::Text("Examples:");
    ImGui::BulletText("'PDF files from last week'");
    ImGui::BulletText("'Large video files on desktop'");
    ImGui::BulletText("'Documents modified today'");
    ImGui::BulletText("'Images larger than 5MB'");
  } else {
    ImGui::Text(ICON_FA_LIGHTBULB " Use 'Generate Prompt' button to create a search prompt.");
    ImGui::Text("Then paste it into your AI assistant (Copilot, ChatGPT, etc.)");
    ImGui::Text("and paste the JSON response back using 'Paste Prompt from Clipboard'.");
    ImGui::Spacing();
    ImGui::TextDisabled("(Optional: Set GEMINI_API_KEY or OPENCODE_API_KEY for one-click AI search)");
  }
  ImGui::EndTooltip();
}

// Render buttons for AI search (Help Me Search or Generate/Paste buttons)
inline void RenderAISearchButtons(GuiState& state, const AISearchArgs& args, bool has_description,
                                  bool is_api_call_in_progress) {
  ImGui::SameLine();

  if (args.is_index_building || is_api_call_in_progress) {
    ImGui::BeginDisabled();
  }

  if (args.api_key_set) {
    if (ImGui::Button("Help Me Search")) {
      ui::HandleHelpMeSearchButton(state, args.actions, args.is_index_building, is_api_call_in_progress,
                                   has_description, args.provider_config);
    }
  } else {
    ImGui::BeginGroup();
    if (has_description && args.window == nullptr) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Generate Prompt")) {
      ui::HandleGeneratePromptButton(state, args.window, args.actions, args.is_index_building, is_api_call_in_progress, has_description);
    }
    if (has_description && args.window == nullptr) {
      ImGui::EndDisabled();
    }
    if (ImGui::Button("Paste Prompt from Clipboard")) {
      ui::HandlePasteFromClipboardButton(state, args.window);
    }
    ImGui::EndGroup();
  }

  if (args.is_index_building || is_api_call_in_progress) {
    ImGui::EndDisabled();
  }
}

// AI message panel limits: one-line summary cap, full-text dump cap (huge
// error bodies must not blow up the section layout), Copied! notice time.
constexpr size_t kAiMessageSummaryMaxChars = 200;
constexpr size_t kAiDetailsMaxChars = 8000;
constexpr int kCopiedNoticeDisplaySeconds = 2;

// One-line summary of an AI message: text before the first newline, capped.
// The full text stays available via the Details node and the Copy button.
inline std::string SummarizeAiMessage(const std::string& message) {
  std::string summary = message;
  if (const size_t newline = summary.find('\n'); newline != std::string::npos) {
    summary.resize(newline);
  }
  if (summary.size() > kAiMessageSummaryMaxChars) {
    summary.resize(kAiMessageSummaryMaxChars);
    summary += "...";
  }
  return summary;
}

// Sticky Warning/Error panel: summary + Copy/Dismiss buttons + "Copied!"
// feedback + collapsible full text. Success renders plainly (auto-expires).
inline void RenderAiMessagePanel(GuiState& state, GLFWwindow* window) {
  const std::string& message = state.gemini.error_message;
  const AiMessageSeverity severity = state.gemini.message_severity;

  ImGui::Spacing();
  ImVec4 color = Theme::Colors::TextDim;
  if (severity == AiMessageSeverity::Success) {
    color = Theme::Colors::Success;
  } else if (severity == AiMessageSeverity::Warning) {
    color = Theme::Colors::Warning;
  } else if (severity == AiMessageSeverity::Error) {
    color = Theme::Colors::Error;
  }

  const std::string summary = SummarizeAiMessage(message);
  if (severity == AiMessageSeverity::None) {
    ImGui::TextWrapped("%s", summary.c_str());
  } else {
    ImGui::TextColored(color, "%s", summary.c_str());
  }

  if (const bool is_sticky = severity == AiMessageSeverity::Warning || severity == AiMessageSeverity::Error;
      !is_sticky) {
    return;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Copy") && window != nullptr &&
      clipboard_utils::SetClipboardText(window, message)) {
    state.gemini.copied_notice_time = std::chrono::steady_clock::now() +
                                      std::chrono::seconds(kCopiedNoticeDisplaySeconds);
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Dismiss")) {
    state.gemini.error_message.clear();
    state.gemini.message_severity = AiMessageSeverity::None;
  }
  if (const auto now = std::chrono::steady_clock::now(); now < state.gemini.copied_notice_time) {
    ImGui::SameLine();
    ImGui::TextDisabled("Copied!");
  }
  if (summary.size() < message.size() && ImGui::TreeNode("Details##ai_message_details")) {
    const std::string visible = message.substr(0, kAiDetailsMaxChars);
    ImGui::TextWrapped("%s", visible.c_str());
    if (message.size() > kAiDetailsMaxChars) {
      ImGui::TextDisabled("[... truncated ...]");
    }
    ImGui::TreePop();
  }
}

// Render status/error messages for AI search
inline void RenderAISearchStatusMessages(GuiState& state, bool api_key_set, bool is_api_call_in_progress,
                                         GLFWwindow* window) {
  if (!api_key_set) {
    ImGui::TextColored(Theme::Colors::TextDim,
                        ICON_FA_LIGHTBULB " Set GEMINI_API_KEY or OPENCODE_API_KEY for one-click AI search (or use Generate Prompt below).");
    ImGui::Spacing();
  }

  // Show loading indicator
  if (api_key_set && is_api_call_in_progress) {
    ImGui::TextDisabled("Generating...");
  }

  // Check if async call is complete
  if (state.gemini.api_call_in_progress && state.gemini.api_future.valid()) {
    if (const auto status = state.gemini.api_future.wait_for(std::chrono::seconds(0));
        status == std::future_status::ready) {
      ui::ProcessGeminiApiResult(state);
    }
  }

  // Display status/error message if present. Success auto-expires via
  // error_display_time; Warning/Error stay until dismissed (Copy support).
  if (!state.gemini.error_message.empty()) {
    const bool is_sticky = state.gemini.message_severity == AiMessageSeverity::Warning ||
                           state.gemini.message_severity == AiMessageSeverity::Error;
    if (const auto now = std::chrono::steady_clock::now();
        is_sticky || now < state.gemini.error_display_time) {
      RenderAiMessagePanel(state, window);
    } else {
      state.gemini.error_message.clear();
      state.gemini.message_severity = AiMessageSeverity::None;
    }
  }
}

// Helper functions for Render to reduce cognitive complexity
// Inlined for performance (called every frame during UI rendering)

// Calculate maximum button width for uniform column alignment
inline float CalculateMaxButtonWidth(const GuiState& state) {
  const float search_now_width = ComputeButtonWidth(ICON_FA_MAGNIFYING_GLASS " Search now");
  const float clear_all_width = ComputeButtonWidth(ICON_FA_ERASER " Clear All");
  const char* quick_filters_label = state.ui_visibility.show_quick_filters ? ICON_FA_FILTER " Hide Quick Filters" : ICON_FA_FILTER " Show Quick Filters";
  const float quick_filters_width = ComputeButtonWidth(quick_filters_label);

  float max_width = (std::max)(search_now_width, clear_all_width);
  max_width = (std::max)(max_width, quick_filters_width);
  return max_width;
}

// Render path search input field
inline bool RenderPathSearchInput(GuiState& state, float input_start_x, float input_width) {
  constexpr const char* path_placeholder =
#ifdef _WIN32
      "e.g., C:\\projects\\src or **/folder/** or **pattern**"
#else
      "e.g., /projects/src or **/folder/** or **pattern**"
#endif  // _WIN32
      ;

  constexpr const char* path_tooltip =
#ifdef _WIN32
      "Path Search Syntax:\n"
      "• Plain path: e.g. C:\\projects\\src (substring search)\n"
      "• Whole-path pattern: uses ** to cross folders\n"
      "  - **name matches paths ENDING with 'name'\n"
      "  - **name** matches paths CONTAINING 'name' anywhere\n"
      "  - **/prefix* matches files starting with 'prefix' in subfolders\n"
      "• Prefixes: pp: (PathPattern), rs: (Regex), fz: (Fuzzy)"
#else
      "Path Search Syntax:\n"
      "• Plain path: e.g. /projects/src (substring search)\n"
      "• Whole-path pattern: uses ** to cross folders\n"
      "  - **name matches paths ENDING with 'name'\n"
      "  - **name** matches paths CONTAINING 'name' anywhere\n"
      "  - **/prefix* matches files starting with 'prefix' in subfolders\n"
      "• Prefixes: pp: (PathPattern), rs: (Regex), fz: (Fuzzy)"
#endif  // _WIN32
      ;

  bool enter_pressed = false;
  InputFieldOptions options;
  options.width = input_width;
  options.show_help = true;
  options.show_generator = true;
  options.placeholder = path_placeholder;
  options.is_optional = false;
  options.tooltip = path_tooltip;
  options.input_start_x = input_start_x;
  if (SearchInputs::RenderInputFieldWithEnter("Path", "##path", state.searchCriteria.path_input.Data(),
                                SearchInputField::MaxLength(), state, options)) {
    enter_pressed = true;
  }

  return enter_pressed;
}

// Render extensions input field
inline bool RenderExtensionsInput(GuiState& state, float input_width) {
  bool enter_pressed = false;
  InputFieldOptions options;
  options.width = input_width;
  options.placeholder = "e.g., txt;pdf";
  options.is_optional = false;
  if (SearchInputs::RenderInputFieldWithEnter(
          "Ext", "##extensions", state.searchCriteria.extension_input.Data(),
          SearchInputField::MaxLength(), state, options)) {
    enter_pressed = true;
  }

  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("Enter file extensions separated by semicolons (;).");
    ImGui::Text("Example: txt;pdf;doc;docx");
    ImGui::Text("Leading dots and spaces are automatically removed.");
    ImGui::Text("Extensions are case-insensitive.");
    ImGui::EndTooltip();
  }

  return enter_pressed;
}

// Render name input field
inline bool RenderFilenameInput(GuiState& state, float input_start_x, float input_width) {
  const bool should_focus = state.input_debounce.focus_filename_input;
  if (should_focus) {
    state.input_debounce.focus_filename_input = false;
  }

  bool enter_pressed = false;
  InputFieldOptions options;
  options.width = input_width;
  options.show_help = true;
  options.show_generator = true;
  options.request_focus = should_focus;
  options.placeholder = "e.g., report* or *.log";
  options.is_optional = false;
  options.input_start_x = input_start_x;
  if (SearchInputs::RenderInputFieldWithEnter(
          "Name", "##filename", state.searchCriteria.filename_input.Data(),
          SearchInputField::MaxLength(), state, options)) {
    enter_pressed = true;
  }

  return enter_pressed;
}

// Render search options checkboxes
inline void RenderSearchOptions(GuiState& state, bool is_index_building) {
  ImGui::AlignTextToFramePadding();
  ImGui::Text(ICON_FA_COG " Options:");
  ImGui::SameLine();
  static constexpr float item_type_combo_width = 125.0F;
  static constexpr std::array<const char*, 3> item_type_labels = {
      "All items",
      "Files only",
      "Folders only",
  };
  auto current_item_type = static_cast<int>(
      state.searchCriteria.GetEffectiveItemTypeFilter());
  if (current_item_type < static_cast<int>(ItemTypeFilter::All) ||
      current_item_type > static_cast<int>(ItemTypeFilter::FoldersOnly)) {
    current_item_type = static_cast<int>(ItemTypeFilter::All);
  }
  ImGui::SetNextItemWidth(item_type_combo_width);
  if (ImGui::Combo("##ItemTypeFilter", &current_item_type, item_type_labels.data(), static_cast<int>(item_type_labels.size()))) {
    state.searchCriteria.SetItemTypeFilter(static_cast<ItemTypeFilter>(current_item_type));
    state.MarkInputChanged();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("Filter by item type:\n- All items: both files and folders\n- Files only: exclude directories\n- Folders only: show directories only");
    ImGui::EndTooltip();
  }

  ImGui::SameLine();
  bool case_insensitive = !state.searchCriteria.case_sensitive;
  ImGui::Checkbox("Case-Insensitive", &case_insensitive);
  state.searchCriteria.case_sensitive = !case_insensitive;
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("When checked (default), search ignores case.\nWhen unchecked, search is case-sensitive.");
    ImGui::EndTooltip();
  }

  ImGui::SameLine();
  if (is_index_building) {
    ImGui::BeginDisabled();
  }
  ImGui::Checkbox("Auto-refresh", &state.searchCriteria.auto_refresh);
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("Re-run search automatically when files are added/deleted.\nKeeps results up-to-date with file system changes.");
    ImGui::EndTooltip();
  }
  if (is_index_building) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  ImGui::Checkbox("Search as you type", &state.searchCriteria.instant_search);
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("When checked, search runs automatically as you type (with debounce).\nWhen unchecked (default), searches only run when you press Enter or click Search.");
    ImGui::EndTooltip();
  }
}

// Help button for an input field: toggles the Search Syntax Guide window.
// Runs inside the caller's PushID scope; push-neutral (style colors only).
void RenderSearchHelpButton(GuiState& state) {
  ImGui::AlignTextToFramePadding();
  PushGhostIconButtonStyle();

  // Use FontAwesome question circle icon
  // ImGui automatically brightens text on hover for better contrast
  // ID is scoped by PushID above, so each button has unique ID
  // Show "Hide Search Help" when window is open, icon only when closed (like Settings/Metrics)
  if (ImGui::SmallButton(state.ui_visibility.show_search_help_window ? ICON_FA_BOOK_OPEN " Hide Search Help##SearchHelpToggle"
                                                    : ICON_FA_BOOK_OPEN " Help##SearchHelpToggle")) {
    // Toggle search help window
    state.ui_visibility.show_search_help_window = !state.ui_visibility.show_search_help_window;
  }

  // Show tooltip with improved, more explicit text
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("Click for help: Search syntax and patterns");
    ImGui::EndTooltip();
  }

  ImGui::PopStyleColor(4);
  ImGui::SameLine();
}

} // namespace

bool SearchInputs::RenderInputFieldWithEnter(const char *label, const char *id,
                                             char *buffer, size_t buffer_size,
                                             GuiState &state,
                                             const InputFieldOptions& options) {
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s:", label);
  if (options.is_optional) {
    ImGui::SameLine(0.0F, 2.0F);  // Small spacing between label and "(optional)"
    ImGui::TextDisabled("(optional)");
  }
  ImGui::SameLine();

  // Push ID scope to ensure unique IDs for help/generator buttons
  // This prevents ID conflicts when multiple input fields are rendered
  ImGui::PushID(id);

  if (options.show_help) {
    RenderSearchHelpButton(state);
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
        state.ui_visibility.open_regex_generator_popup_filename = true;
      } else {
        state.ui_visibility.open_regex_generator_popup = true;
      }
      ImGui::PushID(id); // Restore ID scope
    }

    // Show tooltip
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::Text("Click to open Regex Generator");
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

  float final_width = (std::max)(width_with_margin, min_input_width);

  // Also respect the suggested width if it's smaller (to prevent overflow)
  if (options.width > 0.0F && options.width < final_width) {
    final_width = options.width;
  }

  if (options.input_start_x > 0.0F) {
    ImGui::SetCursorPosX(options.input_start_x);
  }

  ImGui::SetNextItemWidth(final_width);

  // Set focus if requested (must be called right before InputText)
  if (options.request_focus) {
    ImGui::SetKeyboardFocusHere();
  }

  // Use InputTextWithHint if placeholder is provided, otherwise use InputText
  const bool enter_pressed = (options.placeholder != nullptr)
      ? ImGui::InputTextWithHint(id, options.placeholder, buffer, buffer_size,
                                 ImGuiInputTextFlags_EnterReturnsTrue)
      : ImGui::InputText(id, buffer, buffer_size,
                         ImGuiInputTextFlags_EnterReturnsTrue);

  if (options.tooltip != nullptr && ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(options.tooltip);
    ImGui::EndTooltip();
  }

  if (ImGui::IsItemEdited()) {
    state.MarkInputChanged();
  }

  return enter_pressed;
}

void SearchInputs::RenderAISearch(GuiState &state, const AISearchArgs& args) {
  // Calculate available width for layout
  const float window_right_edge = ImGui::GetWindowContentRegionMax().x;
  const float cursor_x = ImGui::GetCursorPosX();
  const float available_width = window_right_edge - cursor_x;

  // api_key_set arrives fresh from the caller each frame (single source of
  // truth in UIRenderer::RenderAISearchSection); never cached here since the
  // key can be set/unset at runtime.
  const bool is_api_call_in_progress = state.gemini.api_call_in_progress;
  const bool has_description = state.gemini.description_input.at(0) != '\0';

  // Calculate layout dimensions
  float button_width = 0.0F;
  float input_width = 0.0F;
  float input_height = 0.0F;
  CalculateAISearchLayout(args.api_key_set, available_width, button_width, input_width, input_height);

  // Render input field with placeholder
  RenderAISearchInputField(state, input_width, input_height, args.api_key_set);

  // Render tooltips
  RenderAISearchTooltips(args.api_key_set);

  // Render buttons
  RenderAISearchButtons(state, args, has_description, is_api_call_in_progress);

  // Render status messages
  RenderAISearchStatusMessages(state, args.api_key_set, is_api_call_in_progress, args.window);
}

void SearchInputs::Render(GuiState &state,
                          UIActions* actions,
                          bool is_index_building,
                          const AppSettings& settings,
                          [[maybe_unused]] GLFWwindow* window) {
  bool enter_pressed = false;

  // Calculate 3 invariant layout anchors
  const float uniform_button_width = CalculateMaxButtonWidth(state);
  const float window_right_edge = ImGui::GetWindowContentRegionMax().x;
  const float kButtonX = window_right_edge - uniform_button_width;
  constexpr float kItemSpacingX = 8.0F;

  const float name_label_width = ImGui::CalcTextSize("Name:").x;
  const float path_label_width = ImGui::CalcTextSize("Path:").x;
  const float helper_buttons_width = (ImGui::GetFrameHeight() * 2.0F) + (kItemSpacingX * 2.0F); // [?][R]
  const float kInputStartX = ImGui::GetCursorPosX() +
                             (std::max)(name_label_width, path_label_width) +
                             helper_buttons_width + kItemSpacingX;

  const float kTotalInputSpan = kButtonX - kInputStartX - kItemSpacingX;

  const float ext_label_width = ImGui::CalcTextSize("Ext:").x + kItemSpacingX;
  const float kExtInputWidth = (std::clamp)(kTotalInputSpan * 0.22F, 140.0F, 180.0F);
  const float kExtGroupWidth = ext_label_width + kExtInputWidth;

  const float kNameInputWidth = (std::max)(kTotalInputSpan - kExtGroupWidth - kItemSpacingX, 200.0F);
  const float kPathInputWidth = kTotalInputSpan;

  // Sync instant_search and auto_refresh when Simplified or Minimalistic UI is enabled
  if (settings.uiMode != AppSettings::UIMode::Full) {
    state.searchCriteria.instant_search = true;
    state.searchCriteria.auto_refresh = true;
  }

  // Line 1: Name + Ext + Search now
  ImGui::PushID("name_input_helpers");
  if (RenderFilenameInput(state, kInputStartX, kNameInputWidth)) {
    enter_pressed = true;
  }
  ImGui::PopID();

  ImGui::SameLine();
  if (RenderExtensionsInput(state, kExtInputWidth)) {
    enter_pressed = true;
  }

  ImGui::SameLine();
  ImGui::SetCursorPosX(kButtonX);
  if (is_index_building) {
    ImGui::BeginDisabled();
  }
  Theme::PushAccentButtonStyle();
  if (ImGui::Button(ICON_FA_MAGNIFYING_GLASS " Search now", ImVec2(uniform_button_width, 0)) && actions != nullptr) {
    actions->TriggerManualSearch(state);
  }
  Theme::PopAccentButtonStyle();
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("Force an immediate search with current parameters.");
    ImGui::Text("Shortcut: Enter in any search input field");
    ImGui::EndTooltip();
  }
  if (is_index_building) {
    ImGui::EndDisabled();
  }

  // Line 2: Path + Clear All
  if (settings.uiMode != AppSettings::UIMode::Minimalistic) {
    ImGui::Spacing();
    ImGui::PushID("path_input_helpers");
    if (RenderPathSearchInput(state, kInputStartX, kPathInputWidth)) {
      enter_pressed = true;
    }
    ImGui::PopID();

    ImGui::SameLine();
    ImGui::SetCursorPosX(kButtonX);
    if (ImGui::Button(ICON_FA_ERASER " Clear All", ImVec2(uniform_button_width, 0))) {
      state.ClearInputs();
      state.searchCriteria.time_filter = TimeFilter::None;
    }
  }

  // Line 3: Options + Quick Filters
  if (settings.uiMode == AppSettings::UIMode::Full) {
    ImGui::Spacing();
    RenderSearchOptions(state, is_index_building);

    ImGui::SameLine();
    ImGui::SetCursorPosX(kButtonX);
    if (const char* quick_filters_label = state.ui_visibility.show_quick_filters
                                              ? ICON_FA_FILTER " Hide Quick Filters"
                                              : ICON_FA_FILTER " Show Quick Filters";
        ImGui::Button(quick_filters_label, ImVec2(uniform_button_width, 0))) {
      state.ui_visibility.show_quick_filters = !state.ui_visibility.show_quick_filters;
    }
  }

  // Handle Enter key press from search input fields only (plain Enter; Cmd+Enter/Ctrl+Enter reserved for result table: reveal in Explorer).
  if (enter_pressed && !is_index_building && actions != nullptr) {
    actions->TriggerManualSearch(state);
  }
}

} // namespace ui // NOSONAR(cpp:S125) - Standard namespace closing comment, not commented-out code


