# Results Table Feature Opportunities Research - 2026-02-14

## Executive Summary

Research into features that could add extra value at the results table level, inspired by the path hierarchy indentation toggle we just implemented. Sources: current codebase, UX reviews, competitive analysis (Everything, Alfred, FileSeek), and power-user workflows.

---

## Current Results Table Capabilities

| Feature | Status |
|---------|--------|
| 5 columns (Filename, Size, Last Modified, Full Path, Extension) | ✅ |
| Column sorting (persists across searches) | ✅ |
| Single-row selection | ✅ |
| Double-click to open / reveal in Explorer | ✅ |
| Right-click context menu (Windows) | ✅ |
| Drag-and-drop (Windows, macOS) | ✅ |
| Delete key + confirmation popup | ✅ |
| Ctrl/Cmd+C to copy (filename or full path on hover) | ✅ |
| Virtual scrolling (ImGuiListClipper) | ✅ |
| Lazy loading of size/mod time | ✅ |
| Path hierarchy indentation (optional toggle) | ✅ (new) |
| Export to CSV | ✅ |
| Full path tooltip on hover | ✅ |

---

## Feature Opportunities (Prioritized)

### High Value, Low–Medium Effort

#### 1. **Arrow Key Navigation**
- **What**: Up/Down to move selection, Enter to open selected file
- **Why**: UX Review 2026-02-14 flags "Keyboard navigation through the Results Table" as an accessibility gap. Everything and Alfred both support this.
- **User value**: Power users can browse results without touching the mouse
- **Effort**: Medium (2–4 hours) – ImGui table focus and key handling
- **Reference**: UI_IMPROVEMENT_IDEAS_SUMMARY.md Issue #7

#### 2. **Copy Selected Paths to Clipboard (Bulk)**
- **What**: Ctrl+Shift+C or context menu "Copy paths" to copy all selected items (when multi-select exists) or selected item's full path
- **Why**: Export exists, but quick copy of paths for scripts/terminals is common. Alfred has "File Buffer" for batch actions.
- **User value**: Paste paths into terminal, scripts, or other apps without exporting
- **Effort**: Low if single-select only (already have path); Medium if multi-select added
- **Note**: Currently single-select only – could start with "Copy full path of selected" shortcut (may already exist via hover+Ctrl+C; verify discoverability)

#### 3. **Column Visibility Toggle**
- **What**: Option to show/hide columns (e.g., hide Extension or Size for a cleaner view)
- **Why**: Everything allows right-click on column headers to add/remove columns. Users have different needs (some care about size, others don't).
- **User value**: Reduce clutter, focus on what matters
- **Effort**: Medium (3–5 hours) – persist column visibility in settings, conditional rendering
- **Pattern**: Same as path hierarchy – Settings checkbox + optional keyboard shortcut

#### 4. **Row Density / Compact Mode**
- **What**: Toggle for "Compact" vs "Comfortable" row height (reduce padding)
- **Why**: Power users with many results want to see more rows without scrolling
- **User value**: More results visible at once
- **Effort**: Low–Medium (1–3 hours) – ImGui table row height or font scale per table
- **Pattern**: Settings > Appearance > "Compact results table"

#### 5. **Truncation Mode Toggle**
- **What**: Option to truncate path at beginning vs middle vs end (or "full path, no truncation" with horizontal scroll)
- **Why**: UX Review 2026-02-14: "Truncated Path Readability – middle/beginning truncation can hide the most important part (parent folders)"
- **User value**: User chooses which part of the path is most important to see
- **Effort**: Medium (2–4 hours) – add truncation strategy to settings, implement alternatives
- **Note**: Full path tooltip already exists; this is about the visible column content

---

### Medium Value, Medium Effort

#### 6. **Multi-Select (Shift+Click, Ctrl+Click)**
- **What**: Extend selection with Shift+Up/Down, Shift+Click; add to selection with Ctrl+Click
- **Why**: Everything supports Shift+Arrow, Shift+Page Up/Down, Ctrl+A. Alfred has "File Buffer" for batch actions.
- **User value**: Delete multiple files, copy multiple paths, drag multiple files
- **Effort**: Medium–High (4–8 hours) – change `selectedRow` to `selectedRows` (set/vector), update all selection logic, drag-drop for multiple files
- **Dependency**: Enables bulk copy, bulk delete, bulk open

#### 7. **"Copy Paths" for Selected (Single or Multi)**
- **What**: One action to copy full paths of selected items (newline-separated)
- **Why**: Common need when feeding paths to scripts, `xargs`, etc.
- **User value**: Paste into terminal: `cat $(pbpaste)` or similar
- **Effort**: Low once multi-select exists; Low now for single-select (ensure discoverable)
- **Reference**: Export to CSV exists; this is lighter-weight

#### 8. **Grouped Table (Phase 2 of Path Hierarchy)**
- **What**: Collapsible folder groups instead of flat list (see PATH_HIERARCHY_VISUALIZATION_FEATURE.md Option B)
- **Why**: Already documented as Phase 2
- **User value**: Collapse folders to focus on a subset, see match counts per folder
- **Effort**: Medium (4–6 hours)

#### 9. **Loading State Feedback**
- **What**: Spinner or progress for "..." in Size/Last Modified columns (Issue #11 in UI_IMPROVEMENT_IDEAS_SUMMARY)
- **Why**: "No indication that data is being loaded asynchronously"
- **User value**: Clear feedback that attributes are loading
- **Effort**: Small (1–2 hours)

---

### Lower Priority / Higher Effort

#### 10. **Tree View Mode**
- **What**: Toggle between Table and Tree view (PATH_HIERARCHY doc Option A)
- **Why**: Alternative way to browse by structure
- **Effort**: High (1–2 weeks)

#### 11. **Column Reordering Persistence**
- **What**: ImGui tables support Reorderable; persist column order in settings
- **Why**: Users may want Filename first, or Path first, etc.
- **Effort**: Low if ImGui already supports it; verify persistence

#### 12. **Find Duplicates (by Size, Name, etc.)**
- **What**: Right-click column header → "Find Duplicates" (Everything has this)
- **Why**: Power users often search for duplicate files
- **Effort**: High (requires grouping/dedup logic)

#### 13. **Search Within Results**
- **What**: Filter the current result set by a text query (e.g., "only show .cpp files containing 'main'")
- **Why**: Narrow down large result sets without re-running search
- **Effort**: Medium–High (filter UI + in-memory filter)

---

## Competitive Snapshot

| Feature | FindHelper | Everything | Alfred |
|---------|------------|------------|--------|
| Path hierarchy indentation | ✅ | ❌ | ❌ |
| Arrow key navigation | ❌ | ✅ | ✅ |
| Multi-select (Shift/Ctrl) | ❌ | ✅ | ✅ (File Buffer) |
| Column show/hide | ❌ | ✅ | N/A |
| Copy paths (bulk) | Partial | ✅ | ✅ |
| Compact/row density | ❌ | ✅ | N/A |
| Export results | ✅ CSV | ✅ EFU/CSV | ❌ |
| Tree/grouped view | ❌ | ❌ | ❌ |

---

## Recommended Quick Wins (Similar to Path Hierarchy Toggle)

1. **Row density / compact mode** – Settings toggle, low effort, immediate value
2. **Column visibility** – Hide Extension or Size; persists in settings
3. **Arrow key navigation** – High impact for accessibility and power users
4. **Copy selected path shortcut** – Ensure Ctrl+C on selected row (without hover) copies full path; document in Help

---

## References

- `docs/review/2026-02-14-v2/2026-02-14_PATH_HIERARCHY_VISUALIZATION_FEATURE.md`
- `docs/review/2026-02-14-v2/UX_REVIEW_2026-02-14.md`
- UI/UX improvement ideas and reviews (see project maintainer for internal docs)
- Everything: https://www.voidtools.com/support/everything/
- Alfred: https://www.alfredapp.com/help/features/file-search/
