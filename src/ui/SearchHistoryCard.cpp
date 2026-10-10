/**
 * @file ui/SearchHistoryCard.cpp
 * @brief Implementation of reusable search history card component
 */

#include "ui/SearchHistoryCard.h"

#include "core/Settings.h"
#include "gui/GuiState.h"
#include "gui/UIActions.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "search/SearchHistory.h"
#include "ui/IconsFontAwesome.h"
#include "ui/Theme.h"
#include "utils/Logger.h"

#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>

namespace ui {

namespace {

constexpr float kHistoryRowSubtitleFontScale = 0.88F;
constexpr float kHistoryTooltipWrapWidthChars = 40.0F;
constexpr float kCardRounding = 6.0F;
constexpr float kAccentBarWidth = 4.0F;
constexpr float kCardPaddingX = 8.0F;
constexpr float kCardPaddingY = 6.0F;
constexpr float kBadgeSpacing = 6.0F;
constexpr float kSubtitleOffsetY = 4.0F;
constexpr float kActionZoneMarginY = 4.0F;
constexpr float kDeleteButtonHoverAlpha = 0.75F;
constexpr float kDeleteButtonActiveAlpha = 1.0F;
constexpr float kBgTintSelected = 0.22F;
constexpr float kBgTintHovered = 0.12F;
constexpr float kBgTintDefault = 0.08F;

[[nodiscard]] ImVec4 GetCardBgColor(bool selected, bool hovered) {
  if (selected) {
    return Theme::AccentTint(kBgTintSelected);
  }
  if (hovered) {
    return Theme::AccentTint(kBgTintHovered);
  }
  return Theme::AccentTint(kBgTintDefault);
}

[[nodiscard]] ImVec4 GetCardBorderColor(bool selected, bool hovered) {
  if (selected) {
    return Theme::Colors::Accent;
  }
  if (hovered) {
    return Theme::AccentTint(0.4F);
  }
  return Theme::Colors::Border;
}

void ToggleHistoryPinWithFeedback(const SearchHistoryEntry& entry, bool pin,
                                  AppSettings& settings, std::int64_t now_ms,
                                  UIActions* actions, GuiState& state) {
  // Headline captured BEFORE the toggle: unpin trims the vector, invalidating
  // entry references on erase/shift.
  const std::string headline = BuildHistoryEntryDisplayInfo(entry).headline;
  if (pin) {
    PinHistoryEntry(entry.id, now_ms, settings);
  } else {
    UnpinHistoryEntry(entry.id, now_ms, settings);
  }
  const bool persist_ok = (actions != nullptr) && actions->PersistSettings();
  if (!persist_ok) {
    LOG_ERROR("Failed to persist search history pin/unpin change");
  }
  NotifyPinAction(state.export_workflow, headline, pin, persist_ok);
}

void RenderCardContextMenu(const SearchHistoryEntry& entry,
                           AppSettings& settings,
                           UIActions* actions,
                           std::int64_t now_ms,
                           bool is_pinned,
                           GuiState& state) {
  if (!ImGui::BeginPopupContextItem("##card_ctx")) {
    return;
  }
  if (is_pinned) {
    if (ImGui::MenuItem(ICON_FA_XMARK " Unpin")) {
      ToggleHistoryPinWithFeedback(entry, false, settings, now_ms, actions, state);
    }
  } else {
    if (ImGui::MenuItem(ICON_FA_PIN " Pin to Quick Access")) {
      ToggleHistoryPinWithFeedback(entry, true, settings, now_ms, actions, state);
    }
  }
  if (ImGui::MenuItem(ICON_FA_PEN " Rename")) {
    state.history.pending_rename_id = entry.id;
  }
  ImGui::Separator();
  if (ImGui::MenuItem(ICON_FA_TRASH " Delete")) {
    state.history.pending_delete_id = entry.id;
  }
  ImGui::EndPopup();
}

void RenderCardSurface(const ImVec2& card_screen_pos,
                       const ImVec2& card_screen_max,
                       bool selected,
                       bool hovered) {
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  const ImVec4 bg_color = GetCardBgColor(selected, hovered);
  const ImVec4 border_color = GetCardBorderColor(selected, hovered);

  draw_list->AddRectFilled(card_screen_pos, card_screen_max,
                           ImGui::ColorConvertFloat4ToU32(bg_color), kCardRounding);
  draw_list->AddRect(card_screen_pos, card_screen_max,
                     ImGui::ColorConvertFloat4ToU32(border_color), kCardRounding, 0, 1.0F);

  const ImU32 accent_col = ImGui::ColorConvertFloat4ToU32(Theme::Colors::Accent);
  draw_list->AddRectFilled(
    card_screen_pos, ImVec2(card_screen_pos.x + kAccentBarWidth, card_screen_max.y),
    accent_col, kCardRounding, ImDrawFlags_RoundCornersLeft);
}

struct CardActionButtonsParams {
  AppSettings& settings;
  UIActions* actions;
  std::int64_t now_ms;
  bool is_pinned;
  float total_buttons_w;
  float spacing;
};

void RenderCardButtons(const SearchHistoryEntry& entry,
                       GuiState& state,
                       const ImVec2& card_pos,
                       float config_width,
                       const CardActionButtonsParams& params) {
  ImGui::SetCursorPos(ImVec2(card_pos.x + config_width - params.total_buttons_w - kCardPaddingX,
                             card_pos.y + kCardPaddingY));

  if (params.is_pinned) {
    if (RenderIconButtonWithTooltip(ICON_FA_XMARK, "Unpin")) {
      ToggleHistoryPinWithFeedback(entry, false, params.settings, params.now_ms, params.actions,
                                   state);
    }
  } else {
    if (RenderIconButtonWithTooltip(ICON_FA_PIN, "Pin to Quick Access")) {
      ToggleHistoryPinWithFeedback(entry, true, params.settings, params.now_ms, params.actions,
                                   state);
    }
  }

  ImGui::SameLine(0.0F, params.spacing);
  if (RenderIconButtonWithTooltip(ICON_FA_PEN, "Rename")) {
    state.history.pending_rename_id = entry.id;
  }

  ImGui::SameLine(0.0F, params.spacing);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(Theme::Colors::Error.x, Theme::Colors::Error.y,
                               Theme::Colors::Error.z, kDeleteButtonHoverAlpha));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(Theme::Colors::Error.x, Theme::Colors::Error.y,
                               Theme::Colors::Error.z, kDeleteButtonActiveAlpha));
  if (RenderIconButtonWithTooltip(ICON_FA_TRASH, "Delete")) {
    state.history.pending_delete_id = entry.id;
  }
  ImGui::PopStyleColor(2);
}

void RenderCardTooltip(const SearchHistoryEntry& entry, bool hovered, bool in_action_zone) {
  if (hovered && !in_action_zone) {
    const std::string tooltip = BuildHistoryEntryTooltip(entry);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * kHistoryTooltipWrapWidthChars);
    ImGui::TextUnformatted(tooltip.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

}  // anonymous namespace

void TextEllipsisSingleLine(const char* text_begin, const char* text_end, float max_width) {
  const float line_h = ImGui::GetTextLineHeight();
  const ImGuiWindow* window = ImGui::GetCurrentWindow();
  const float layout_w = max_width > 0.0F ? max_width : 1.0F;
  if (window == nullptr) {
    ImGui::Dummy(ImVec2(layout_w, line_h));
    return;
  }
  const ImVec2 pos_min = ImGui::GetCursorScreenPos();
  const ImVec2 pos_max(pos_min.x + layout_w, pos_min.y + line_h);
  ImGui::RenderTextEllipsis(window->DrawList, pos_min, pos_max, pos_max.x, text_begin, text_end,
                            nullptr);
  ImGui::Dummy(ImVec2(layout_w, line_h));
}

bool RenderIconButtonWithTooltip(const char* icon_text, std::string_view tooltip,
                                 bool* out_hovered_or_active) {
  const bool clicked = ImGui::SmallButton(icon_text);
  const bool is_hovered = ImGui::IsItemHovered();
  if (const bool is_active = ImGui::IsItemActive();
      out_hovered_or_active != nullptr && (is_hovered || is_active)) {
    *out_hovered_or_active = true;
  }
  if (is_hovered && !tooltip.empty()) {
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(tooltip.data(), tooltip.data() + tooltip.size());
    ImGui::EndTooltip();
  }
  return clicked;
}

float MaxSmallIconButtonWidth(std::initializer_list<const char*> icons) {
  const ImGuiStyle& style = ImGui::GetStyle();
  float widest = 0.0F;
  for (const char* icon : icons) {
    widest = (std::max)(widest, ImGui::CalcTextSize(icon).x + (style.FramePadding.x * 2.0F));
  }
  return widest;
}

bool SearchHistoryCard::Render(const SearchHistoryEntry& entry,
                               GuiState& state,
                               AppSettings& settings,
                               UIActions* actions,
                               std::int64_t now_ms,
                               const SearchHistoryCardConfig& config) {
  ImGui::PushID(entry.id.c_str());

  const ImVec2 card_pos = ImGui::GetCursorPos();
  const ImVec2 card_screen_pos = ImGui::GetCursorScreenPos();
  const ImVec2 card_size(config.width, config.height);
  const ImVec2 card_screen_max(card_screen_pos.x + config.width,
                               card_screen_pos.y + config.height);

  const bool selected = (state.history.selected_id == entry.id);
  const bool hovered = ImGui::IsMouseHoveringRect(card_screen_pos, card_screen_max);
  const bool show_actions = hovered || state.history.pending_rename_id == entry.id ||
                            state.history.pending_delete_id == entry.id;

  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float button_w =
    MaxSmallIconButtonWidth({ICON_FA_XMARK, ICON_FA_PIN, ICON_FA_PEN, ICON_FA_TRASH});
  const float total_buttons_w = (button_w * 3.0F) + (spacing * 2.0F);

  // Geometric action zone on the top-right of the card to avoid search click hijacking
  const float action_zone_x =
    card_screen_pos.x + config.width - total_buttons_w - kCardPaddingX - spacing;
  const float action_zone_y =
    card_screen_pos.y + ImGui::GetFrameHeight() + kCardPaddingY + kActionZoneMarginY;
  const ImVec2 mouse_pos = ImGui::GetIO().MousePos;
  const bool in_action_zone =
    mouse_pos.x >= action_zone_x && mouse_pos.x <= card_screen_max.x &&
    mouse_pos.y >= card_screen_pos.y && mouse_pos.y <= action_zone_y;

  // 1. Submit Selectable background hit target (covers entire card area)
  bool triggered_search = false;
  if (ImGui::Selectable("##card_hit", selected, ImGuiSelectableFlags_AllowOverlap, card_size) &&
      !in_action_zone && actions != nullptr) {
    state.history.selected_id = entry.id;
    ApplyAndRunHistoryEntry(entry, state, actions);
    triggered_search = true;
  }

  // Right-click context menu
  RenderCardContextMenu(entry, settings, actions, now_ms, config.is_pinned, state);

  // 2. Draw card container surface and borders
  RenderCardSurface(card_screen_pos, card_screen_max, selected, hovered);

  // 3. Line 1: Pin Badge + Headline Title
  const HistoryEntryDisplayInfo info = BuildHistoryEntryDisplayInfo(entry);

  ImGui::SetCursorPos(ImVec2(card_pos.x + kCardPaddingX, card_pos.y + kCardPaddingY));
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::Accent);
  ImGui::TextUnformatted(config.is_pinned ? ICON_FA_PIN : ICON_FA_HISTORY);
  ImGui::PopStyleColor();
  ImGui::SameLine(0.0F, kBadgeSpacing);

  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextStrong);
  const float headline_max_w = (std::max)(
    10.0F,
    config.width - (ImGui::GetCursorPosX() - card_pos.x) - total_buttons_w - kCardPaddingX);
  TextEllipsisSingleLine(info.headline.c_str(), info.headline.c_str() + info.headline.size(),
                         headline_max_w);
  ImGui::PopStyleColor();

  // 4. Action Buttons (Top Right)
  if (show_actions) {
    const CardActionButtonsParams button_params{
      settings, actions, now_ms, config.is_pinned, total_buttons_w, spacing,
    };
    RenderCardButtons(entry, state, card_pos, config.width, button_params);
  }

  // 5. Line 2: Subtitle / Preset description
  ImGui::SetCursorPos(ImVec2(card_pos.x + kCardPaddingX,
                             card_pos.y + kCardPaddingY + ImGui::GetTextLineHeight() + kSubtitleOffsetY));
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::TextDim);
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kHistoryRowSubtitleFontScale);
  if (!info.subtitle.empty()) {
    TextEllipsisSingleLine(info.subtitle.c_str(), info.subtitle.c_str() + info.subtitle.size(),
                           config.width - (kCardPaddingX * 2.0F));
  } else {
    ImGui::TextUnformatted(config.is_pinned ? "Quick Search Preset" : "Recent Search");
  }
  ImGui::PopFont();
  ImGui::PopStyleColor();

  // 6. Tooltip (when hovering over card body outside action buttons)
  RenderCardTooltip(entry, hovered, in_action_zone);

  ImGui::PopID();
  return triggered_search;
}

}  // namespace ui
