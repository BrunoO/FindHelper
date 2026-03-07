/**
 * @file ui/SearchControls.cpp
 * @brief Implementation of search controls rendering component
 */

#include "ui/SearchControls.h"

#include "gui/GuiState.h"
#include "gui/UIActions.h"
#include "imgui.h"

namespace ui {

void SearchControls::Render(const GuiState &state,
                            [[maybe_unused]] UIActions* actions,  // NOSONAR(cpp:S995) - UIActions interface has non-const methods, cannot be const pointer
                            bool is_index_building) {
  // Search button removed - functionality moved to "Help Me Search" button
  // "Help Me Search" now triggers manual search when AI description is empty
  // Note: Clear All is rendered in SearchInputs::RenderAISearch(); Help is in the application bar
  // Note: Options checkboxes (Folders Only, Case-Insensitive, Auto-refresh) 
  // are rendered in SearchInputs::Render() on the same line as Extensions/Filename
  (void)state;
  (void)actions;
  (void)is_index_building;
}

} // namespace ui

