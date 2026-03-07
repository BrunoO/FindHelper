# Research: High-Value Keyboard Shortcuts (Similar to Pin to Quick Access)

**Date:** 2026-02-21  
**Purpose:** Identify shortcuts that could provide high user value for FindHelper, based on current UX, competitor patterns (Everything, Windows Explorer, VS Code), and workflow impact.  
**Context:** Pin to Quick Access (Ctrl+Shift+P) was added as a Windows-only shortcut; this doc explores similar high-value additions.

---

## 1. Current shortcut inventory (FindHelper)

| Category | Shortcut | Action |
|----------|----------|--------|
| Global | Ctrl+F / Cmd+F | Focus filename search |
| Global | F5 | Refresh search (re-run query) |
| Global | Escape | Clear all filters |
| Global | Ctrl+Shift+H | Toggle path hierarchy indentation |
| Search | Enter / Ctrl+Enter | Trigger search |
| Search | Shift+F | (Removed) |
| Saved | Ctrl+S / Cmd+S | Save current search (dialog) |
| File ops | Ctrl+C | Copy filename (hover filename) or path (hover path) |
| File ops | Ctrl+E | Export results to CSV |
| File ops | Delete | Delete selected file (confirmation) |
| File ops | Ctrl+Shift+P (Win) | Pin selected to Quick Access |
| File ops | Double-click filename | Open file with default app |
| File ops | Double-click path | Open parent folder in Explorer |
| Mark | n / Down, p / Up | Next/previous row |
| Mark | m, t, u | Mark, toggle, unmark + move |
| Mark | Shift+M, T, U | Mark all, invert, unmark all |
| Mark | W, Shift+W | Copy marked paths / filenames |
| Mark | Shift+D | Bulk delete marked |

**Gaps:** No keyboard way to “open selected file” or “reveal selected in Explorer” or “copy path of selected” without hovering a specific column. No Enter/Ctrl+Enter in result list.

---

## 2. Competitor patterns (high value)

### 2.1 Everything (voidtools)

| Shortcut | Action | FindHelper equivalent / gap |
|----------|--------|-----------------------------|
| **Enter** | Open selected items | **Gap:** Open selected file with default app (we have double-click only). |
| **Ctrl+Enter** | Open path of selected item (Explorer, file selected) | **Gap:** Reveal in Explorer / open parent folder with file selected (we have double-click path only). |
| **Ctrl+Shift+C** | Copy full path and name of selection | **Gap:** Copy path of **selected row** regardless of focus; we have Ctrl+C only when hovering path/filename. |
| F2 | Rename focused item | Not implemented; would need rename UI or shell invoke. |
| Alt+Enter | Properties of selected items | **Gap (Win):** Could invoke shell Properties. |
| Delete / Shift+Delete | Recycle bin / permanent delete | We have Delete (recycle); no permanent-delete shortcut. |

### 2.2 Windows Explorer

| Shortcut | Action | Relevance to FindHelper |
|----------|--------|--------------------------|
| Ctrl+Shift+C | Copy as path (Windows 11 22H2+) | Same as Everything; “copy path of selection” is a strong convention. |
| Enter | Open selected | Same as Everything. |
| Alt+Enter | Properties | Same as Everything. |

### 2.3 VS Code (file / explorer)

| Shortcut | Action | Relevance |
|----------|--------|------------|
| Ctrl+Shift+E (Win) | Focus Explorer / reveal active file | We don’t have “reveal in Explorer” as a dedicated shortcut; Ctrl+Enter on selected row would match. |
| Enter | Open file | Same idea as “Enter = open selected” in result list. |

---

## 3. High-value shortcut candidates

### 3.1 Enter – Open selected file

- **Action:** When focus is in the results table and a row is selected, **Enter** opens the selected item with the default application (file) or does nothing / opens in Explorer (folder).
- **User value:** Very high. Matches Everything and common “list + Enter” mental model; avoids having to double-click.
- **Implementation:** In `HandleResultsTableKeyboardShortcuts`, when `has_selection` and `KeyPressedOnce(ImGuiKey_Enter)` (or `ImGuiKey_KeypadEnter`), call `OpenFileDefault(selected.fullPath)` for files; for folders, either open in Explorer or no-op (document behavior).
- **Conflict:** Enter already used in search inputs to trigger search; only active when **results table has focus** (already the case for other table shortcuts). No conflict if we require window focus on the table.
- **Effort:** Low.

### 3.2 Ctrl+Enter – Reveal in Explorer / open parent folder

- **Action:** When a row is selected, **Ctrl+Enter** opens the parent folder in Explorer (Windows) / Finder (macOS) / file manager (Linux) and selects the file (reveal in folder).
- **User value:** Very high. Matches Everything’s “open path of selected item” and VS Code’s “reveal in Explorer”; essential for “found it, now show me where it lives.”
- **Implementation:** Call existing `OpenParentFolder(selected.fullPath)` when selection is valid. Already implemented per platform.
- **Conflict:** Ctrl+Enter in search triggers search; again, only when **table has focus** this would apply. Check: table shortcuts run only when `IsWindowFocused(RootAndChildWindows)` and not `WantTextInput`, so when typing in search we don’t hit table shortcuts. Good.
- **Effort:** Low.

### 3.3 Ctrl+Shift+C – Copy path of selected item

- **Action:** When a row is selected, **Ctrl+Shift+C** copies the **full path** of the selected item to the clipboard, regardless of which column the mouse is over.
- **User value:** High. Aligns with Windows 11 “Copy as path” (Ctrl+Shift+C in Explorer) and Everything’s “Copy full path and name”; avoids having to hover the path column and press Ctrl+C.
- **Implementation:** In table shortcut handler, if `has_selection` and Ctrl+Shift+C, call `CopyPathToClipboard(glfw_window, display_results[selectedRow].fullPath)` (or equivalent). Reuse existing clipboard API.
- **Conflict:** None in current list. Document that in the table this means “selected row,” not “hovered cell.”
- **Effort:** Low.

### 3.4 Alt+Enter – Properties (Windows)

- **Action:** When a row is selected, **Alt+Enter** opens the Windows Properties dialog for that file or folder.
- **User value:** Medium–high for power users; matches Explorer and Everything.
- **Implementation:** Invoke shell verb “properties” on the path (e.g. via IContextMenu or ShellExecute with the appropriate verb). Similar to PinToQuickAccess but verb = “properties”.
- **Effort:** Low (Windows); N/A or different on macOS/Linux (e.g. “Get Info” could be a separate shortcut).

### 3.5 F2 – Rename (future)

- **Action:** Rename the selected file or folder in-place or via a dialog.
- **User value:** High for file-management workflows; Everything and Explorer both support it.
- **Implementation:** Medium–high: either in-place edit in the table (sync with filesystem) or invoke shell rename dialog. Risk of conflicts with ImGui focus and validation.
- **Effort:** Medium–high. Defer unless product prioritizes rename-in-app.

### 3.6 Shift+Delete – Permanent delete (optional)

- **Action:** Permanently delete selected file(s) (no Recycle Bin), with confirmation.
- **User value:** Medium; matches Explorer/Everything but dangerous if misused.
- **Implementation:** Platform-specific “permanent delete” (e.g. Windows: delete without recycle bin). Strong confirmation dialog recommended.
- **Effort:** Low–medium. Optional; many apps avoid promoting permanent delete.

### 3.7 O or Ctrl+O – Open selected (alternative to Enter)

- **Action:** Some apps use **O** or **Ctrl+O** to “open” selected item. FindHelper could reserve **Enter** for “open file” and use **Ctrl+Enter** for “reveal in folder”; no need for O if Enter is implemented.
- **Recommendation:** Prefer **Enter** for open (industry standard); skip O unless Enter is reserved elsewhere.

### 3.8 Focus search from table (Tab)

- **Action:** **Tab** from results table focuses the search/filename input so the user can refine the query without using the mouse.
- **User value:** Medium–high for keyboard-heavy users.
- **Implementation:** Set focus to the search input (ImGui focus API or app-level “focus search” action). May require exposing a “focus search input” callback or state from the main window.
- **Effort:** Low–medium depending on how focus is managed.

---

## 4. Recommended priority

| Priority | Shortcut | Action | Rationale | Effort |
|----------|----------|--------|-----------|--------|
| 1 | **Enter** | Open selected file (or folder in Explorer) | Universal “list + Enter” expectation; no mouse needed. | Low |
| 2 | **Ctrl+Enter** | Reveal in Explorer / open parent folder | Matches Everything/VS Code; high value for “where is this file?” | Low |
| 3 | **Ctrl+Shift+C** | Copy full path of selected row | Matches Windows 11 and Everything; works on selected row without hovering. | Low |
| 4 | **Alt+Enter** (Win) | Properties of selected item | Power-user; consistent with Explorer/Everything. | Low |
| 5 | **Tab** (from table) | Focus search input | Improves keyboard flow. | Low–medium |
| 6 | Shift+Delete | Permanent delete (with confirmation) | Optional; higher risk. | Low–medium |
| 7 | F2 | Rename | High value but higher implementation cost. | Medium–high |

---

## 5. Summary table (candidates vs current)

| Action | Everything | Explorer (Win11) | FindHelper today | Proposed |
|--------|------------|-------------------|-------------------|----------|
| Open selected | Enter | Enter | Double-click only | **Enter** |
| Reveal in folder | Ctrl+Enter | — | Double-click path | **Ctrl+Enter** |
| Copy path (selection) | Ctrl+Shift+C | Ctrl+Shift+C | Ctrl+C when hover path | **Ctrl+Shift+C** (selected row) |
| Pin to Quick Access | — | Right-click | Ctrl+Shift+P (Win) | Done |
| Properties | Alt+Enter | Alt+Enter | — | **Alt+Enter** (Win) |
| Rename | F2 | F2 | — | Defer (F2) |
| Permanent delete | Shift+Delete | Shift+Delete | — | Optional |

Implementing **Enter**, **Ctrl+Enter**, and **Ctrl+Shift+C** first would bring FindHelper in line with Everything and Explorer for the most common result-list actions and provide the highest user value for the effort.
