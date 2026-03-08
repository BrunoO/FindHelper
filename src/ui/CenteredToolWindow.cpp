/**
 * @file ui/CenteredToolWindow.cpp
 * @brief Implementation of centered tool window helpers
 */

#include "ui/CenteredToolWindow.h"

#include "imgui.h"

#include "ui/IconsFontAwesome.h"

namespace ui::detail {

void SetupCenteredToolWindow(float width, float height) {
  const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  constexpr float kPivot = 0.5F;
  const ImVec2 center(main_viewport->WorkPos.x + (main_viewport->WorkSize.x * kPivot),
                      main_viewport->WorkPos.y + (main_viewport->WorkSize.y * kPivot));
  ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(kPivot, kPivot));
  ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
}

void RenderToolWindowCloseButton(bool* p_open) {
  ImGui::Separator();
  constexpr float kCloseButtonWidth = 120.0F;
  if (ImGui::Button(ICON_FA_XMARK " Close", ImVec2(kCloseButtonWidth, 0))) {
    *p_open = false;
  }
}

}  // namespace ui::detail
