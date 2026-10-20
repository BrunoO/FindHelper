#include "ui/AiProviderWindow.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "api/AiProvider/ProviderCatalog.h"
#include "core/Settings.h"
#include "gui/ImGuiUtils.h"
#include "gui/UIActions.h"
#include "imgui.h"
#include "ui/CenteredToolWindow.h"
#include "ui/IconsFontAwesome.h"
#include "ui/LayoutConstants.h"
#include "ui/Theme.h"
#include "ui/UiStyleGuards.h"
#include "utils/ClipboardUtils.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"

namespace ui::ai_provider_window {
namespace {

namespace ai_provider_ui_constants {
// Window size: wide enough for the provider list and the connection fields.
constexpr int kWindowWidth = 640;   // NOLINT(readability-identifier-naming) - kPascalCase for constants
constexpr int kWindowHeight = 560;  // NOLINT(readability-identifier-naming) - kPascalCase for constants
// Size of the fixed input buffers (endpoint, model, key variable).
constexpr size_t kFieldBufferSize = 512;  // NOLINT(readability-identifier-naming) - kPascalCase for constants
// Timeout lower bound in the editor: 0 means "provider default"; the upper
// bound comes from ai_provider_defaults (the provider validates 1-300).
constexpr int kDefaultTimeoutSentinel = 0;  // NOLINT(readability-identifier-naming) - kPascalCase for constants
// Step and fast-step of the timeout spinner (seconds).
constexpr int kTimeoutStepSeconds = 5;     // NOLINT(readability-identifier-naming) - kPascalCase for constants
constexpr int kTimeoutFastStepSeconds = 15;  // NOLINT(readability-identifier-naming) - kPascalCase for constants
}  // namespace ai_provider_ui_constants

// Pure timeout-editor logic, kept free of ImGui so it can be unit-tested:
// the range comes from gemini_api_utils (the values the provider enforces) and
// the sentinel means "use the provider default".
namespace timeout_bounds {
constexpr int kDefaultSentinel = ai_provider_ui_constants::kDefaultTimeoutSentinel;
constexpr int kMinSeconds = gemini_api_utils::kMinTimeoutSeconds;
constexpr int kMaxSeconds = gemini_api_utils::kMaxTimeoutSeconds;
static_assert(kDefaultSentinel < kMinSeconds,
              "The 'provider default' sentinel must stay below the accepted range");

// Clamps an editor value into {0} U [kMinSeconds, kMaxSeconds]; 0 is preserved
// so the user can still select the provider default.
[[nodiscard]] int ClampTimeoutSeconds(int seconds) {
  if (seconds == kDefaultSentinel) {
    return kDefaultSentinel;
  }
  return (std::max)(kMinSeconds, (std::min)(seconds, kMaxSeconds));
}

// Tooltip text for the timeout field, derived from the enforced bounds.
[[nodiscard]] std::string TooltipText() {
  return std::string("Per-attempt timeout. ") + std::to_string(kDefaultSentinel) +
         " = provider default (" + std::to_string(gemini_api_utils::kDefaultTimeoutSeconds) +
         "s).\nValid range: " + std::to_string(kMinSeconds) + "-" + std::to_string(kMaxSeconds) + ".";
}
}  // namespace timeout_bounds

// The window's working copy lives in ui::ai_provider_window::AiProviderEditorState
// (see the header): UI-thread-only editor scratch state, deliberately NOT part
// of GuiState (the FIELD OWNERSHIP MAP there covers search/results state). This
// accessor is its only writer, and the type is never touched by search or index
// code.
AiProviderEditorState& GetEditorState() {
  static AiProviderEditorState state;
  return state;
}

// Text shown for a key status.
const char* KeyStatusLabel(ai_provider::ApiKeyStatus status) {
  switch (status) {
    case ai_provider::ApiKeyStatus::NotRequired:
      return "no key needed";
    case ai_provider::ApiKeyStatus::Found:
      return "key detected";
    case ai_provider::ApiKeyStatus::Missing:
      return "no key";
  }
  return "";
}

// Colour used for a key status.
ImVec4 KeyStatusColor(ai_provider::ApiKeyStatus status) {
  switch (status) {
    case ai_provider::ApiKeyStatus::NotRequired:
      return Theme::Colors::TextDim;
    case ai_provider::ApiKeyStatus::Found:
      return Theme::Colors::Success;
    case ai_provider::ApiKeyStatus::Missing:
      return Theme::Colors::Warning;
  }
  return Theme::Colors::TextDim;
}

// Builds the provider configuration described by a settings group.
ai_provider::ProviderConfig ConfigFromSelection(const AiProviderSettings& selection) {
  ai_provider::ProviderConfig config;
  config.provider_id = selection.providerId;
  config.base_url = selection.baseUrl;
  config.model = selection.model;
  config.api_key_env = selection.apiKeyEnv;
  config.preset_id = selection.presetId;
  return config;
}

// Index of the catalog entry the given selection resolves to. The stored
// preset id wins; without one the service is derived from the endpoint, so
// the radio list always highlights the provider that is actually in use.
int SelectedPresetIndex(const AiProviderSettings& selection) {
  const std::string_view resolved_id =
      ai_provider::ResolvePreset(ConfigFromSelection(selection)).id;
  int index = 0;
  for (const auto& preset : ai_provider::PresetCatalog()) {
    if (resolved_id == preset.id) {
      return index;
    }
    ++index;
  }
  return 0;
}

// Renders the provider catalog as a radio list with a key status per entry.
void RenderPresetList(AiProviderSettings& selection) {
  int selected_index = SelectedPresetIndex(selection);
  int entry_index = 0;
  for (const auto& preset : ai_provider::PresetCatalog()) {
    ImGui::PushID(preset.id);
    if (ImGui::RadioButton(preset.label, &selected_index, entry_index)) {
      ai_provider::ApplyPreset(selection, preset);
    }
    ImGui::PopID();

    ImGui::SameLine();
    const ai_provider::ApiKeyStatus status = ai_provider::ResolvePresetApiKeyStatus(preset);
    ImGui::TextColored(KeyStatusColor(status), "%s", KeyStatusLabel(status));

    ImGui::TextDisabled("%s", preset.description);
    ++entry_index;
  }
}

// Renders one editable text field backed by a fixed buffer.
void RenderTextField(const char* label, std::string& value, const char* hint = nullptr) {
  std::array<char, ai_provider_ui_constants::kFieldBufferSize> buffer = {0};
  strcpy_safe(buffer.data(), buffer.size(), value.c_str());
  if (ImGui::InputText(label, buffer.data(), buffer.size())) {
    value = buffer.data();
  }
  if (hint != nullptr && ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", hint);
  }
}

// Renders the connection details of the selected catalog entry.
void RenderConnectionFields(AiProviderSettings& selection) {
  // Gemini has a built-in endpoint, so only the model is editable there.
  if (const ai_provider::ProviderPreset& preset =
          ai_provider::ResolvePreset(ConfigFromSelection(selection));
      std::string_view(preset.id) != ai_provider::preset_defaults::kPresetGemini) {
    RenderTextField("Endpoint", selection.baseUrl, "Base URL of an OpenAI-compatible server");
  }
  RenderTextField("Model", selection.model, "Model id sent to the provider");
  RenderTextField("API key variable", selection.apiKeyEnv,
                  "Name of the environment variable holding the API key");

  if (int timeout = selection.timeoutSeconds;
      ImGui::InputInt("Timeout (seconds)", &timeout, ai_provider_ui_constants::kTimeoutStepSeconds,
                      ai_provider_ui_constants::kTimeoutFastStepSeconds)) {  // NOSONAR(cpp:S6004) - ImGui modifies by reference, then used after if
    // A positive value below the minimum would be rejected by the provider at
    // call time, so clamp it up here instead of accepting a setting that can
    // never produce a request.
    selection.timeoutSeconds = timeout_bounds::ClampTimeoutSeconds(timeout);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", timeout_bounds::TooltipText().c_str());
  }
}

// Renders the hint shown when the selected provider needs a key that is not set.
void RenderKeyHint(const AiProviderSettings& selection, GLFWwindow* glfw_window) {
  const ai_provider::ProviderConfig config = ConfigFromSelection(selection);
  if (const ai_provider::ApiKeyStatus status = ai_provider::ResolveApiKeyStatus(config);
      status != ai_provider::ApiKeyStatus::Missing) {
    return;
  }

  const std::string variable(ai_provider::KeyVariableHint(config));
  if (variable.empty()) {
    return;
  }

  ImGui::Spacing();
  ImGui::TextColored(Theme::Colors::Warning, "%s %s", ICON_FA_LIGHTBULB,
                     "No API key found in the environment.");
  ImGui::Text("Set it in the shell that starts FindHelper, then restart the app:");
  ImGui::Text("  export %s=...", variable.c_str());
  if (glfw_window != nullptr) {
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_COPY " Copy command")) {
      clipboard_utils::SetClipboardText(glfw_window, "export " + variable + "=");
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Copy the export command to the clipboard");
    }
  }
}

// Footer actions: Save applies the working copy, Close discards it.
void RenderFooter(bool* p_open, AppSettings& settings, ui::UIActions* actions) {
  detail::RenderToolWindowFooterSeparator();
  detail::AlignToolWindowFooterButtons(2, LayoutConstants::kSecondaryButtonWidth);
  if (ImGui::Button(ICON_FA_SAVE " Save", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
    GetEditorState().ApplyTo(settings);
    if (actions != nullptr && !actions->PersistSettings()) {
      LOG_WARNING_BUILD("Failed to persist the AI provider settings");
    }
    *p_open = false;
    GetEditorState().MarkClosed();
  }
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_XMARK " Close", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
    *p_open = false;
    GetEditorState().MarkClosed();
  }
}

}  // namespace

void Render(bool* p_open, AppSettings& settings, ui::UIActions* actions, GLFWwindow* glfw_window) {
  if (p_open == nullptr || !*p_open) {
    GetEditorState().MarkClosed();
    return;
  }

  AiProviderEditorState& editor = GetEditorState();
  editor.SyncForFrame(settings);

  const detail::StyleColorGuard window_bg_guard(ImGuiCol_WindowBg, Theme::Colors::WindowBg);
  CenterNextWindowInMainWindow(ImGuiCond_Appearing);
  ImGui::SetNextWindowSize(ImVec2(static_cast<float>(ai_provider_ui_constants::kWindowWidth),
                                  static_cast<float>(ai_provider_ui_constants::kWindowHeight)),
                           ImGuiCond_FirstUseEver);

  if (const detail::WindowGuard window_guard("AI Settings", p_open, ImGuiWindowFlags_None);
      window_guard.ShowContent()) {
    ImGui::TextUnformatted("Provider");
    ImGui::Separator();
    ImGui::Spacing();
    RenderPresetList(editor.working);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Connection");
    ImGui::Separator();
    ImGui::Spacing();
    RenderConnectionFields(editor.working);

    RenderKeyHint(editor.working, glfw_window);

    ImGui::Spacing();
    ImGui::TextDisabled("The API key is read from the environment and never stored in the settings file.");
    ImGui::Spacing();

    RenderFooter(p_open, settings, actions);
  }

  // Reset the editor state when the window was closed through its title-bar X
  // button: the working copy must be re-synced on the next open.
  if (!*p_open) {
    editor.MarkClosed();
  }
}

}  // namespace ui::ai_provider_window
