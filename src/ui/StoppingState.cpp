/**
 * @file ui/StoppingState.cpp
 * @brief Implementation of stopping state rendering component
 */

#include "ui/StoppingState.h"

#include "imgui.h"

#include "ui/Theme.h"

namespace ui {

namespace {
  constexpr int kStoppingVerticalSpacingCount = 5;

  // Helper function to add multiple vertical spacing elements
  inline void AddVerticalSpacing(int count) {
    for (int i = 0; i < count; ++i) {
      ImGui::Spacing();
    }
  }
}  // anonymous namespace

void StoppingState::Render() {
  // Show prominent vertical space
  AddVerticalSpacing(kStoppingVerticalSpacingCount);

  // Center the text horizontally
  const float window_width = ImGui::GetContentRegionAvail().x;

  // Display "Stopping..." message
  const char* stopping_text = "Stopping...";
  const float text_width = ImGui::CalcTextSize(stopping_text).x;
  ImGui::SetCursorPosX((window_width - text_width) * 0.5F);

  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::Text);
  ImGui::Text("%s", stopping_text);  // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg) - ImGui API
  ImGui::PopStyleColor();

  AddVerticalSpacing(kStoppingVerticalSpacingCount);
}

} // namespace ui
