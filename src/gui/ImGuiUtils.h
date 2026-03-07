#pragma once

#include "imgui.h"

// Small ImGui layout and input helpers used across the UI.

// Primary shortcut modifier: Ctrl (Windows/Linux) or Cmd (macOS). Use for Ctrl+F, Ctrl+Enter, etc.
inline bool IsPrimaryShortcutModifierDown(const ImGuiIO& io) {
  return io.KeyCtrl || io.KeySuper;
}

// Copy shortcut: Ctrl+C (Windows/Linux) or Cmd+C (macOS). Use with IsItemHovered() for table copy.
inline bool IsCopyShortcutPressed() {
  const ImGuiIO& io = ImGui::GetIO();
  return IsPrimaryShortcutModifierDown(io) && ImGui::IsKeyPressed(ImGuiKey_C);
}

// Compute the width of a standard ImGui button with the given text label.
inline float ComputeButtonWidth(const char *label) {
  const ImGuiStyle &style = ImGui::GetStyle();
  return ImGui::CalcTextSize(label).x + (style.FramePadding.x * 2.0F);  // NOLINT(readability-math-missing-parentheses) - Parentheses added for clarity
}

// Right-align a group of widgets whose total width is known.
// Call this BEFORE drawing the group:
//
//   float total_width = ...; // sum of buttons + spacing
//   ImGui::SameLine();
//   AlignGroupRight(total_width);
//   // draw buttons...
//
inline void AlignGroupRight(float group_width) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const float window_right_edge = ImGui::GetWindowContentRegionMax().x;
  const float right_margin = style.WindowPadding.x;
  const float target_x = window_right_edge - group_width - right_margin;
  const float current_x = ImGui::GetCursorPosX();
  
  // Only move cursor if we have space and target is to the right of current position
  if (target_x > current_x && (target_x + group_width) <= (window_right_edge - right_margin)) {
    ImGui::SetCursorPosX(target_x);
  }
}

