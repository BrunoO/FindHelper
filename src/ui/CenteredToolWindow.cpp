/**
 * @file ui/CenteredToolWindow.cpp
 * @brief Implementation of centered tool window helpers
 */

#include "ui/CenteredToolWindow.h"

#include "imgui.h"

#include "gui/ImGuiUtils.h"
#include "ui/IconsFontAwesome.h"
#include "ui/LayoutConstants.h"

namespace ui::detail {

void SetupCenteredToolWindow(float width, float height) {
  CenterNextWindowInMainWindow(ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
}

void RenderToolWindowFooterSeparator() {
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
}

void AlignToolWindowFooterButtons(int button_count, float button_width) {
  if (button_count <= 0) {
    return;
  }
  const ImGuiStyle& style = ImGui::GetStyle();
  const float spacing = style.ItemSpacing.x;
  const float total_width = (static_cast<float>(button_count) * button_width) +
                            (static_cast<float>(button_count - 1) * spacing);
  const float start_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - total_width;
  ImGui::SetCursorPosX(start_x);
}

void RenderToolWindowCloseButton(bool* p_open) {
  RenderToolWindowFooterSeparator();
  if (ImGui::Button(ICON_FA_XMARK " Close", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
    *p_open = false;
  }
}

}  // namespace ui::detail
