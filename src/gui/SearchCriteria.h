#pragma once

/**
 * @file SearchCriteria.h
 * @brief Value-type snapshot of the user-directed search inputs and toggles.
 *
 * God-object decomposition Phase 2 (see
 * internal-docs/plans/2026-09-11_GUISTATE_GOD_OBJECT_DECOMPOSITION_PLAN.md):
 * `GuiState` owns a single `SearchCriteria searchCriteria` member; widget code
 * and presets still mutate it through the same field names
 * (`state.searchCriteria.filename_input`), while read-only consumers (search
 * trigger paths, filter caches) receive the struct by value or `const&`.
 *
 * Immutability contract: SearchController copies the criteria (snapshot) at
 * search-trigger time and builds SearchParams from the snapshot, so criteria
 * edits arriving later in the same frame can no longer alter an in-flight
 * search's inputs. BuildParams() is a pure conversion — it must not mutate
 * anything and must stay free of I/O or locking.
 */

#include <string>

#include "filters/SizeFilter.h"
#include "filters/TimeFilter.h"
#include "search/SearchInputField.h"
#include "search/SearchTypes.h"

// User-visible search criteria: the three input fields plus the filter
// toggles. Single writer: the user-input widgets and the presets
// (GuiState::ApplySearchConfig / ApplyShowAllPreset); readers everywhere else.
struct SearchCriteria {
  // Input fields (encapsulated value type, see SearchInputField).
  SearchInputField extension_input;
  SearchInputField filename_input;
  SearchInputField path_input;

  bool folders_only = false;   // Match folders instead of files then filter out files
  bool case_sensitive = false; // Case-sensitive matching (false, case-insensitive default)
  TimeFilter time_filter = TimeFilter::None;   // Time-based filter for "Last Modified"
  SizeFilter size_filter = SizeFilter::None;   // Size-based filter for file size
  bool instant_search = false; // Search as you type (with debounce)
  bool auto_refresh = false;   // Re-run search when index changes

  // True when any text field carries a query. Pure: reads only this struct.
  // Used by auto-refresh to arm on empty results (a typed query is worth
  // watching even before its first hit); filter-only states stay unarmed.
  [[nodiscard]] bool HasSearchText() const {
    return !filename_input.IsEmpty() || !extension_input.IsEmpty() ||
           !path_input.IsEmpty();
  }

  // Build the SearchParams for the SearchWorker (filename, path, extensions,
  // folders_only, case_sensitive). Pure: reads only this struct's fields.
  [[nodiscard]] SearchParams BuildParams() const {
    SearchParams params;
    params.filenameInput = filename_input;
    params.extensionInput = extension_input;
    params.pathInput = path_input;
    params.foldersOnly = folders_only;
    params.caseSensitive = case_sensitive;

    return params;
  }
};
