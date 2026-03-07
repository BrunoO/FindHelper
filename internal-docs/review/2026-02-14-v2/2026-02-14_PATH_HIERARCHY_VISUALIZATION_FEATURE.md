# Path Hierarchy Visualization Feature - 2026-02-14

## Executive Summary

**Feature**: Visualize the folder hierarchy of search results in the Results Table.

**User Goal**: When searching for files (e.g., all `.md` files in `USN_WINDOWS`), see the folder structure of matching files—not just a flat list—so the spatial/organizational context is visible.

**Example**: Search `*.md` in `USN_WINDOWS` → Instead of only a flat list of paths, the user sees something like:

```
USN_WINDOWS/
├── docs/
│   ├── review/
│   │   └── 2026-02-14-v2/
│   │       └── FEATURE_EXPLORATION_REVIEW_2026-02-14.md
│   └── README.md
├── specs/
│   └── plans/
│       └── features/
│           └── LARGE_RESULT_SET_DISPLAY_PLAN.md
└── AGENTS.md
```

---

## Problem Statement

### Current Behavior

- Results are displayed in a **flat table** with columns: Filename, Size, Last Modified, Full Path, Extension.
- Each row is a single file; paths are shown as truncated strings.
- **No indication of folder structure**—users cannot easily see:
  - Which folders contain the most matches
  - How results are distributed across subfolders
  - Parent-child relationships between paths

### User Pain Points

1. **Context loss**: Long path strings (especially truncated) hide the folder hierarchy.
2. **Bulk understanding**: Hard to answer "where are most of my .md files?" without mentally parsing paths.
3. **Navigation**: No quick way to collapse/expand by folder to focus on a subset.
4. **Comparison**: Difficult to compare result density across sibling folders.

---

## Proposed Solutions (Options)

### Option A: Tree View Mode (Alternative View)

**Description**: Add a toggle (e.g., "Table" vs "Tree") to switch between the current flat table and a tree view.

**Tree structure**:
- Root = search root (e.g., `USN_WINDOWS`)
- Nodes = folders (expandable)
- Leaves = matching files
- Each folder node shows a count badge: `docs/ (12)`

**Pros**:
- Clear hierarchy
- Collapse/expand by folder
- Natural fit for "browse by structure"

**Cons**:
- Different interaction model (no direct column sorting in tree)
- May need a second panel for file metadata (size, date) when a file is selected
- More complex to implement (tree data structure, virtual scrolling for trees)

**Complexity**: Medium–High

---

### Option B: Grouped Table (Folder Groups)

**Description**: Keep the table, but group rows by parent folder. Each group has a collapsible header showing the folder path and match count.

**Example**:
```
▼ docs/review/2026-02-14-v2/  (1 file)
  FEATURE_EXPLORATION_REVIEW_2026-02-14.md  | 12 KB | ...
▼ docs/  (1 file)
  README.md  | 2 KB | ...
▼ specs/plans/features/  (1 file)
  LARGE_RESULT_SET_DISPLAY_PLAN.md  | 8 KB | ...
```

**Pros**:
- Preserves table columns (sorting, selection, drag-drop)
- Minimal UX change
- Folder context without a separate view

**Cons**:
- Groups can be many (one per unique parent path)
- Collapsing many groups may feel cluttered
- Need to define "group by" semantics (full path vs. top-level folder)

**Complexity**: Medium

---

### Option C: Side Panel Tree (Companion View)

**Description**: Add an optional side panel (left or right) showing a **folder tree** of matching paths. Selecting a folder in the tree filters the main table to that folder’s files.

**Pros**:
- Table stays as-is
- Tree acts as a filter/navigation aid
- Familiar pattern (e.g., IDE project explorer + file list)

**Cons**:
- Uses horizontal space
- Two views to keep in sync
- May be overkill for small result sets

**Complexity**: Medium

---

### Option D: Inline Indentation (Visual Hierarchy Only)

**Description**: Keep the flat table but **indent** rows based on path depth. Sibling files in the same folder share the same indent level. Optional: subtle separators or background tint per "folder group."

**Example**:
```
  docs/review/2026-02-14-v2/FEATURE_EXPLORATION_REVIEW_2026-02-14.md
  docs/README.md
  specs/plans/features/LARGE_RESULT_SET_DISPLAY_PLAN.md
  AGENTS.md
```

**Pros**:
- Easiest to implement
- No new UI components
- Preserves all table behavior

**Cons**:
- No collapse/expand
- Indentation alone may be subtle; depth can be ambiguous for similar paths

**Complexity**: Low

---

## Recommendation (Preliminary)

| Option | Fit for "see folder structure" | Implementation effort | UX impact |
|--------|--------------------------------|------------------------|-----------|
| A. Tree View Mode | ⭐⭐⭐ | High | High |
| B. Grouped Table | ⭐⭐⭐ | Medium | Medium |
| C. Side Panel Tree | ⭐⭐ | Medium | Medium |
| D. Inline Indentation | ⭐ | Low | Low |

**Suggested path**:
1. **Phase 1**: Option D (inline indentation) — **IMPLEMENTED** (2026-02-14). Filename column indented by path depth (14px per level). **Optional**: Toggle in Settings > Appearance > "Indent results by folder depth" (persisted in settings.json). **Keyboard shortcut**: Ctrl+Shift+H (Windows/Linux) or Cmd+Shift+H (macOS) toggles the option and persists immediately.
2. **Phase 2**: Option B (grouped table) if users want collapse/expand and stronger grouping.
3. **Future**: Option A or C if demand justifies a dedicated tree view.

---

## Technical Considerations

### Data Model

- **Current**: `SearchResult` has `fullPath` (string_view), `filename_offset`, `GetDirectoryPath()`.
- **Needed for hierarchy**:
  - Parse `fullPath` into segments (split by `/` or `\`).
  - Build a tree: `map<folder_path, vector<SearchResult>>` or a proper tree structure.
  - Handle path separator differences (Windows `\` vs. Unix `/`).

### Integration Points

- **ResultsTable.cpp**: Main rendering; would need group/tree rendering logic.
- **GuiState**: May need `viewMode` (table vs. tree) or `groupByFolder` flag.
- **SearchResultUtils / PathBuilder**: Reuse for path parsing and normalization.
- **ImGui**: `ImGui::TreeNode` / `ImGui::CollapsingHeader` for expandable nodes; `ImGuiListClipper` for virtual scrolling in tree (if needed).

### Performance

- Building the tree from N results: O(N) with a single pass if using a hash map for parent lookup.
- Virtual scrolling: Tree view may need custom clipping (only render visible nodes) for large result sets.
- Memory: Tree structure adds overhead (pointers, node objects); estimate ~50–100 bytes per unique folder path.

### Platform

- Path separators: Use `PathBuilder` or existing utilities for cross-platform path handling (Windows, macOS, Linux).

---

## Open Questions

1. **Default behavior**: Should hierarchy be on by default, or opt-in (e.g., "Group by folder" checkbox)?
2. **Sorting**: When grouped, should groups be sorted by path, by match count, or user choice?
3. **Empty folders**: If a folder has no direct matches but contains matching files in subfolders, do we show it? (Likely yes, as a parent node.)
4. **Root display**: Show full search root path, or a shortened "project root" label?
5. **Multiple roots**: If the user searches multiple paths, how do we represent multiple roots in the tree?

---

## Success Criteria

- User can see the folder structure of matching files without manually parsing path strings.
- For result sets of 100–10,000 files, hierarchy view is responsive (< 100 ms to build and render).
- Behavior is consistent across Windows, macOS, and Linux.
- Existing table features (selection, double-click, drag-drop, delete) continue to work in the chosen option.

---

## References

- **ResultsTable**: `src/ui/ResultsTable.cpp`, `src/ui/ResultsTable.h`
- **SearchResult**: `src/search/SearchTypes.h`
- **Path utilities**: `PathBuilder`, `ResultsTable::GetDirectoryPath()`
- **ImGui tree**: `ImGui::TreeNode`, `ImGui::CollapsingHeader`
- **Similar feature**: Large result set display plan (result display strategies; see project maintainer for doc)
