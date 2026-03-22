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

void RenderToolWindowCloseButton(bool* p_open) {
  ImGui::Separator();
  if (ImGui::Button(ICON_FA_XMARK " Close", ImVec2(LayoutConstants::kSecondaryButtonWidth, 0))) {
    *p_open = false;
  }
}

}  // namespace ui::detail
