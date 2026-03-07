# Task Prompt: Add Custom Size Range Filter to UI

**Date:** 2026-02-14  
**Source:** TaskmasterPrompt.md  
**Scope:** Add a custom size range filter (min/max) displayed near existing pre-defined size filters.

---

## Part 1: Summary

This task adds a **custom size range filter** to the Manual Search filter panel. Users will be able to specify a minimum and/or maximum file size (in bytes or human-readable units) to filter results, in addition to the existing pre-defined size buckets (Empty, Tiny, Small, Medium, Large, Huge, Massive).

**Assumptions:**
- The custom range filter is **mutually exclusive** with pre-defined size filters: selecting a pre-defined button clears the custom range, and activating the custom range clears the pre-defined filter.
- Min and max are optional: omitting min means "no minimum", omitting max means "no maximum".
- The custom range UI is placed on the same row as the pre-defined size filter buttons, immediately after the "Massive" button (or on a new line if space is limited).
- The project follows AGENTS.md rules (C++17, naming conventions, ImGui immediate mode, etc.).

**Key reference files analyzed:**
- `src/ui/FilterPanel.cpp` – `RenderSizeQuickFilters()` (lines 387–423)
- `src/gui/GuiState.h` – `sizeFilter`, `sizeFilteredResults`, cache fields
- `src/filters/SizeFilter.h` – `SizeFilter` enum
- `src/filters/SizeFilterUtils.cpp` – `MatchesSizeFilter()`, `SizeFilterDisplayLabel()`
- `src/search/SearchResultUtils.cpp` – `ApplySizeFilter()`, `UpdateSizeFilterCacheIfNeeded()`
- `src/core/Settings.h` – `SavedSearch.sizeFilter` (string for persistence)

---

## Part 2: Final Prompt

```markdown
# Mission Context

Users need to filter search results by arbitrary file size ranges (e.g., "between 5 MB and 50 MB") that do not align with the existing pre-defined buckets (Empty, Tiny, Small, Medium, Large, Huge, Massive). Adding a custom min/max size range filter next to the pre-defined buttons will satisfy this need while reusing the existing filter pipeline.

# Core Objective

Implement a custom size range filter (minimum and maximum file size) in the UI, displayed adjacent to the existing pre-defined size filter buttons, and integrate it into the existing size filter pipeline so results are filtered correctly.

# Desired Outcome

- Users see "Min" and "Max" input fields (or a single "Custom range" control) next to the File Size quick filter buttons.
- Users can enter a minimum size, maximum size, or both. Files matching the range are shown.
- The custom range filter is mutually exclusive with pre-defined filters: selecting a pre-defined button clears the custom range; entering a custom range clears the pre-defined filter.
- The custom filter appears in the Active Filters badges when active (e.g., "Size: 5 MB - 50 MB").
- Saved searches and recent searches persist the custom range (e.g., as a string like "Custom:min=5242880,max=52428800" or equivalent).
- Filter pipeline, status bar counts, and total size computation continue to work correctly when the custom range is active.

# Visual Workflow (Mermaid)

    ```mermaid
    graph TD
        A[User expands Quick Filters] --> B[File Size row visible]
        B --> C{User action?}
        C -->|Clicks pre-defined button| D[Set sizeFilter, clear customMin/customMax]
        C -->|Enters min/max in custom inputs| E[Set customMin/customMax, set sizeFilter=None]
        C -->|Clicks Clear on size badge| F[Clear sizeFilter and custom range]
        D --> G[Invalidate size filter cache]
        E --> G
        F --> G
        G --> H[UpdateSizeFilterCacheIfNeeded]
        H --> I{sizeFilter != None?}
        I -->|Yes| J[Apply pre-defined MatchesSizeFilter]
        I -->|No| K{Custom range active?}
        K -->|Yes| L[Apply custom range: min <= size <= max]
        K -->|No| M[No size filter - show all]
        J --> N[Populate sizeFilteredResults]
        L --> N
        M --> N
    ```

# The Process / Workflow

## Phase 1: State and Data Model

1. Add to `GuiState` (in `src/gui/GuiState.h`):
   - `uint64_t customSizeMinBytes = 0;` (0 = no minimum)
   - `uint64_t customSizeMaxBytes = 0;` (0 = no maximum; use a sentinel like `UINT64_MAX` or `0` with explicit "no max" semantics—document the chosen convention)
   - Ensure custom range is considered "active" when `(customSizeMinBytes > 0) || (customSizeMaxBytes > 0 && customSizeMaxBytes != UINT64_MAX)` (or equivalent based on sentinel choice).

2. Add helper `IsCustomSizeRangeActive(const GuiState&)` (e.g., in `FilterPanel` or a new `SizeFilterUtils` function) that returns true when the custom range has at least one non-default value.

3. Ensure mutual exclusivity: when a pre-defined `SizeFilter` button is clicked, set `customSizeMinBytes = 0` and `customSizeMaxBytes = 0` (or sentinel). When custom inputs are changed, set `sizeFilter = SizeFilter::None`.

## Phase 2: Filter Logic

4. Extend `SizeFilterUtils` (or add a new function) with:
   - `bool MatchesCustomSizeRange(uint64_t file_size, uint64_t min_bytes, uint64_t max_bytes);`
   - Semantics: `(min_bytes == 0 || file_size >= min_bytes) && (max_bytes == 0 || max_bytes == UINT64_MAX || file_size <= max_bytes)` (adjust for chosen sentinel).

5. In `SearchResultUtils.cpp`, modify `ApplySizeFilter` (or add an overload / branch) to handle the custom range:
   - When `state.sizeFilter == SizeFilter::None` and `IsCustomSizeRangeActive(state)` is true, filter using `MatchesCustomSizeRange` with `state.customSizeMinBytes` and `state.customSizeMaxBytes`.
   - Ensure `UpdateSizeFilterCacheIfNeeded` triggers a rebuild when the custom range changes (compare `cachedCustomSizeMinBytes` / `cachedCustomSizeMaxBytes` or invalidate when custom range is active and inputs change).

6. Add cache invalidation for custom range: when `customSizeMinBytes` or `customSizeMaxBytes` changes, set `sizeFilterCacheValid = false` (or equivalent). Add `cachedCustomSizeMinBytes` and `cachedCustomSizeMaxBytes` to `GuiState` if needed for change detection.

## Phase 3: UI

7. In `FilterPanel::RenderSizeQuickFilters` (or a new `RenderCustomSizeRangeInputs` called immediately after):
   - After the "Massive" button, add `ImGui::SameLine()` and two input fields: "Min" and "Max".
   - Use `ImGui::InputScalar` or `ImGui::InputText` with parsing. Consider supporting human-readable input (e.g., "5M", "50 MB") via a small parser in `SizeFilterUtils` or a dedicated helper.
   - On change: set `sizeFilter = SizeFilter::None`, update `customSizeMinBytes` / `customSizeMaxBytes`, and invalidate the size filter cache.
   - Use buffers (e.g., `std::array<char, 64>`) for ImGui input; follow existing patterns in the codebase (e.g., `gemini_description_input_`).

8. In `RenderActiveFilterIndicators`, when the custom size range is active (and `sizeFilter == SizeFilter::None`), add a filter badge with a label like "Size: X - Y" (format min/max in human-readable form, e.g., "5 MB - 50 MB"). Reuse `SizeFilterDisplayLabel`-style formatting or add `FormatCustomSizeRangeLabel(min, max)`.

9. Ensure the clear action for the custom size badge sets `customSizeMinBytes = 0`, `customSizeMaxBytes = 0` (or sentinel), and invalidates the cache.

## Phase 4: Persistence and Integration

10. Extend `SavedSearch` and persistence logic:
    - Add `std::string customSizeRange{}` (or encode in existing `sizeFilter` when custom: e.g., `"Custom:min=5242880,max=52428800"`).
    - In `Settings.cpp` (load/save) and `TimeFilterUtils.cpp` (recent search serialization), read/write the custom range.
    - In `GuiState::ApplySearchConfig` or equivalent (e.g., when loading a saved/recent search), parse and apply `customSizeMinBytes` and `customSizeMaxBytes`, and set `sizeFilter = SizeFilter::None`.

11. Update `CountActiveFilters` in `UIRenderer.cpp` to count the custom size range as an active filter when `IsCustomSizeRangeActive(state)` is true.

12. Update `GuiState::ClearInputs` to clear `customSizeMinBytes` and `customSizeMaxBytes` when clearing filters.

## Phase 5: Verification

13. Run `scripts/build_tests_macos.sh` and fix any compilation or test failures.
14. Manually verify: pre-defined buttons work; custom range works; mutual exclusivity; Active Filters badge; saved/recent search persistence; status bar counts and total size.

# Anticipated Pitfalls

- **Sentinel for "no max"**: Using `0` for "no max" can conflict with "max = 0 bytes" (only empty files). Prefer `UINT64_MAX` for "no maximum" and document clearly.
- **Human-readable input**: Parsing "5M", "5 MB", "5.5M" requires a small parser. Consider starting with bytes-only input and adding human-readable parsing in a follow-up if time permits.
- **ImGui buffer lifetime**: Input buffers must persist across frames. Store in `GuiState` (e.g., `customSizeMinInput_`, `customSizeMaxInput_`) and sync with `customSizeMinBytes`/`customSizeMaxBytes` on change.
- **Cache invalidation**: Ensure `deferFilterCacheRebuild` and `sizeFilterCacheValid` are correctly updated when the custom range changes.
- **Directories**: Size filter (including custom) should skip directories (no meaningful size), consistent with `ApplySizeFilter` today.

# Acceptance Criteria / Verification Steps

- [ ] Custom min/max inputs appear next to the File Size quick filter buttons.
- [ ] Entering a min and/or max filters results correctly (min <= size <= max, with "no limit" when min/max is default).
- [ ] Clicking a pre-defined size button clears the custom range and applies the pre-defined filter.
- [ ] Entering a custom range clears the pre-defined filter.
- [ ] Active Filters badge shows "Size: X - Y" (or equivalent) when custom range is active.
- [ ] Clearing the custom size badge resets the custom range and shows all results.
- [ ] Saved searches persist and restore the custom range.
- [ ] Recent searches persist and restore the custom range.
- [ ] Status bar shows correct filtered count when custom range is active.
- [ ] Total size of displayed results is correct when custom range is active.
- [ ] `scripts/build_tests_macos.sh` passes on macOS.
- [ ] No regression: pre-defined size filters still work as before.

# Strict Constraints / Rules to Follow

- Follow AGENTS.md: C++17 only, naming conventions, `(std::min)`/`(std::max)` on Windows, explicit lambda captures, no implicit captures in templates.
- Do not modify platform-specific `#ifdef` blocks to make code cross-platform; use platform-agnostic abstractions if needed.
- Do not change the behavior of pre-defined size filters.
- Use `std::string_view` for read-only string parameters where possible.
- Use `F` suffix for float literals (e.g., `3.14F`).
- Comment `#endif` directives (e.g., `#endif // _WIN32`).
- Run `scripts/build_tests_macos.sh` on macOS after changes.

# Context and Reference Files

- `src/gui/GuiState.h` – Add custom size state; `ClearInputs`, `BuildCurrentSearchParams`
- `src/gui/GuiState.cpp` – `ClearInputs`, `ApplySearchConfig` (if custom range in SearchConfig)
- `src/filters/SizeFilter.h` – SizeFilter enum (no change if custom is not an enum value)
- `src/filters/SizeFilterUtils.h` / `SizeFilterUtils.cpp` – Add `MatchesCustomSizeRange`, optional `FormatCustomSizeRangeLabel`, optional human-readable parser
- `src/ui/FilterPanel.h` / `FilterPanel.cpp` – `RenderSizeQuickFilters`, `RenderActiveFilterIndicators`, `IsCustomSizeRangeActive`
- `src/search/SearchResultUtils.h` / `SearchResultUtils.cpp` – `ApplySizeFilter`, `UpdateSizeFilterCacheIfNeeded`
- `src/ui/UIRenderer.cpp` – `CountActiveFilters`
- `src/core/Settings.h` / `Settings.cpp` – `SavedSearch`, load/save custom range
- `src/filters/TimeFilterUtils.cpp` – Recent search serialization (`SavedSearch` comparison, `CaptureRecentSearch`)
- `docs/design/IMGUI_IMMEDIATE_MODE_PARADIGM.md` – ImGui patterns
- `AGENTS.md` – Project rules

# Concluding Statement

Proceed with the task. Implement the custom size range filter following the phases above, ensure all acceptance criteria are met, and run the macOS test script before considering the task complete.
```
