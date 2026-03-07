#pragma once

#include <initializer_list>
#include <utility>

#include "imgui.h"

namespace ui::detail {

/** RAII guard for ImGui::PushStyleColor / PopStyleColor to prevent stack imbalance. */
struct StyleColorGuard {
  explicit StyleColorGuard(ImGuiCol idx, const ImVec4& col) { ImGui::PushStyleColor(idx, col); }
  ~StyleColorGuard() { ImGui::PopStyleColor(); }
  StyleColorGuard(const StyleColorGuard&) = delete;
  StyleColorGuard& operator=(const StyleColorGuard&) = delete;
};

/** RAII guard for multiple ImGui::PushStyleColor / PopStyleColor (e.g. text + background). */
struct MultiStyleColorGuard {
  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions,hicpp-named-parameter,readability-named-parameter) -- initializer_list ctor non-explicit for brace-init; param named 'items'
  MultiStyleColorGuard(std::initializer_list<std::pair<ImGuiCol, ImVec4>> items)
      : count_(static_cast<int>(items.size())) {
    for (const auto& [col_idx, col_val] : items) {
      ImGui::PushStyleColor(col_idx, col_val);
    }
  }
  ~MultiStyleColorGuard() { ImGui::PopStyleColor(count_); }
  MultiStyleColorGuard(const MultiStyleColorGuard&) = delete;
  MultiStyleColorGuard& operator=(const MultiStyleColorGuard&) = delete;

 private:
  int count_ = 0;  // NOLINT(readability-identifier-naming) - Member follows snake_case_ convention
};

/**
 * RAII guard for ImGui::Begin / ImGui::End.
 *
 * ImGui requires that every Begin() call has a matching End() call, even if Begin()
 * returns false (which happens when the window is collapsed or clipped). This guard
 * ensures the stack is always balanced.
 */
struct WindowGuard {
  explicit WindowGuard(const char* name, bool* p_open = nullptr, ImGuiWindowFlags flags = 0)
      : show_content_(ImGui::Begin(name, p_open, flags)) {}
  ~WindowGuard() { ImGui::End(); }
  WindowGuard(const WindowGuard&) = delete;
  WindowGuard& operator=(const WindowGuard&) = delete;

  /** Returns true if the window content should be rendered (not collapsed/clipped). */
  [[nodiscard]] bool ShowContent() const { return show_content_; }

 private:
  bool show_content_;  // NOLINT(readability-identifier-naming) - Member follows snake_case_ convention
};

}  // namespace ui::detail
