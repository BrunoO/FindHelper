#pragma once

/**
 * @file ui/LayoutConstants.h
 * @brief Centralized UI layout and spacing constants.
 *
 * Use these in UIRenderer, EmptyState, StatusBar, and FilterPanel so "breathing room"
 * and rhythm tweaks are consistent and one-place.
 */

namespace ui {

struct LayoutConstants {
  /** Vertical gap between major sections (e.g. after toolbar, between Manual/AI search). */
  static constexpr float kSectionSpacing = 4.0F;
  /** Vertical padding above and below the top toolbar (UI mode, Settings, Metrics, Help). */
  static constexpr float kToolbarVerticalPadding = 2.0F;
  /** Vertical padding inside blocks (e.g. before/after quick filter rows). */
  static constexpr float kBlockPadding = 2.0F;
  /** Number of ImGui::Spacing() calls for empty state hero area above main message. */
  static constexpr int kEmptyStateHeroSpacingCount = 3;
  /** Number of vertical spacing units before the empty state split panel (examples | recent). */
  static constexpr int kEmptyStatePanelSpacingCount = 1;
  /** Height in pixels of the status bar top border (1 px line above status bar). */
  static constexpr float kStatusBarTopBorderHeight = 1.0F;
  /** Height in pixels of the animated busy progress bar shown when indexing/searching/loading. */
  static constexpr float kStatusBarBusyBarHeight = 4.0F;
  /** Horizontal gap between example search pills in empty state. */
  static constexpr float kExampleButtonSpacing = 6.0F;
  /** Horizontal gap between empty state left panel (examples) and right panel (recent). */
  static constexpr float kEmptyStateSplitPanelGutter = 8.0F;
  /** Footer reserved height = frame_height * this (minimalistic: status bar only). */
  static constexpr float kFooterHeightMultiplierStatusOnly = 1.5F;
  /** Footer reserved height = frame_height * this (full UI: saved searches + status bar). */
  static constexpr float kFooterHeightMultiplierWithSavedSearches = 3.0F;
  /** Width in pixels for secondary modal/tool-window buttons (Close, Cancel, Delete). Keeps sizing consistent across popups. */
  static constexpr float kSecondaryButtonWidth = 120.0F;
};

}  // namespace ui
