# Hot Folder Feature – Research and Integration Options

**Date:** 2026-02-21  
**Purpose:** Explore how to integrate a “hot folder” feature in the UI, where “hot” is derived from heuristics based on Last Modified time. Document benefits, cons, edge cases, and user value.

---

## 1. Definition and Scope

**Hot folder:** A folder that is considered “recently active” according to a heuristic computed from file **Last Modified** times under that folder (within the indexed tree).

- **Scope:** Only folders that are part of the current index (under the crawl root).
- **Limit to user HOME (recommended):** Restrict hot folders to paths **under the user’s home directory** (Windows: `%USERPROFILE%`, macOS/Linux: `$HOME`). The project already exposes `path_utils::GetUserHomePath()` for this. This keeps the list relevant to the current user, reduces noise from system/other volumes, and narrows the set we aggregate over (better performance and clearer UX).
- **Data source:** Existing index metadata: `FileEntry.lastModificationTime` (and path/parent hierarchy). No new persistence required for the heuristic itself.
- **UI goal:** Surfaces “where things changed recently” so the user can quickly scope search or navigate to active areas.

---

## 2. Heuristics Options (Last-Modified Based)

All options use only **Last Modified** time (FILETIME / `LazyFileTime` in the index). No creation time or access time in this research.

| Heuristic | Description | Pros | Cons |
|-----------|-------------|------|------|
| **Max LMT in folder** | Folder “hot” if the **newest** file under it (recursively) was modified recently (e.g. within last 24h / 7d). | Simple, one number per folder; aligns with “something changed here lately”. | Single very old file can make a folder look cold; one touched file can make a big folder “hot”. |
| **Count recent** | Folder “hot” if it has at least N files modified after a cutoff (e.g. “modified in last 7 days”). | Reflects “amount of activity”. | Threshold (N, cutoff) is arbitrary; many small edits vs one big change treated the same. |
| **Weighted score** | Score = sum of weights by recency (e.g. today=3, this week=2, this month=1). Sort folders by score. | Balances recency and volume. | More complex; needs tuning; still heuristic. |
| **Recency tier only** | “Has any file modified Today / This Week” (reuse `TimeFilterUtils::CalculateCutoffTime`). | Reuses existing time semantics; easy to explain. | No notion of “how much” activity. |

**Recommendation for v1:** Start with **max LMT in folder** (or **recency tier** using existing Today/ThisWeek cutoffs). Both are easy to explain (“this folder had something change in the last 7 days”) and align with existing `TimeFilter` and `LastModified` column.

---

## 3. Data and Index Fit

- **Path / hierarchy:** We have `FileEntry.parentID`, `PathBuilder::BuildFullPath`, and `ForEachEntry` / `ForEachEntryWithPath`. So we can group by “directory path” (path prefix or parent_id) and aggregate.
- **Modification time:** `GetFileModificationTimeById()` / `LazyFileTime` – can be **lazy-loaded** (and on Windows may require I/O if not from MFT). So:
  - **On-demand hot-folder computation** over the full index can trigger many lazy loads (one per file) and become expensive on first run.
  - **Precomputed** folder-level aggregates (e.g. max LMT per directory_id) would avoid repeated work but require maintaining them on index updates (insert/remove/rename/move).
- **Directories:** Directory entries often have `lastModificationTime == {0,0}`. Hotness should be derived from **files** under the folder (or from child directories’ max LMT), not from the directory’s own LMT.

**Implication:**  
- For **on-demand:** Prefer iterating only **directories** (or a bounded set of folders) and for each folder compute max LMT over its descendants; optionally cap work (e.g. top K folders, or only folders with at least one file modified after cutoff).  
- For **precomputed:** Add a small structure (e.g. directory_id → max LMT, updated on insert/delete/rename/move) to avoid full scans and lazy-load storms.

---

## 4. UI Integration Options

Existing patterns we can reuse:

- **Recent searches** (`recentSearches`) and **Saved searches** (`savedSearches`) in FilterPanel / EmptyState: list of shortcuts; selection applies path + filters and can trigger search.
- **Folder to Index** in Settings: single path + FolderBrowser for choosing crawl root.
- **Path filter** in search: `SearchContext.path_query` / `SavedSearch.path` – setting a path scopes search to that folder.

Possible placements for “hot folders”:

| Placement | Description | Pros | Cons |
|-----------|-------------|------|------|
| **FilterPanel section** | “Hot folders” list (e.g. top 10 by recency), click sets path filter (and optionally runs search). | Visible next to other filters; same mental model as “scope to path”. | Uses vertical space; needs clear label so users understand “hot” = recent activity. |
| **EmptyState** | When no search yet, show a few “Recently active folders” as chips/buttons; click sets path and optionally focuses search. | Good discoverability when user hasn’t typed anything. | Only visible when results are empty; less useful after first search. |
| **FolderBrowser enhancement** | In the folder picker (e.g. for “Folder to Index”), add a “Hot” or “Recently active” section or sort. | Reuses one dialog; helps pick “where to index” or “where to look”. | Doesn’t help after index is built (folder browser is for selecting path, not for search scope in main UI). |
| **Dedicated sidebar / collapsible** | “Hot folders” as a small panel (e.g. collapsible) beside search. | Clear home for the feature; room for tooltip (“based on last modified”). | More UI surface; need to avoid clutter. |

**Recommendation:**  
- **Primary:** Add a **“Hot folders”** subsection in **FilterPanel** (or next to path filter), with a small list (e.g. top 5–10). Click = set path filter (and optionally trigger search), mirroring “apply saved search” behavior.  
- **Secondary:** In **EmptyState**, show 3–5 “Recently active folders” as quick actions that set path and focus search input.

---

## 5. Benefits

- **Faster navigation to active areas:** User sees “these folders had recent changes” without opening Explorer or guessing.
- **Better use of index:** We already have LMT (and path); hot folders turn that into actionable UI.
- **Consistency with existing filters:** Reuses `TimeFilter` cutoffs and the same “last modified” semantics as the results table and time filter.
- **Scoped search:** One click to “search only in this folder” (path filter) fits current `path_query` and SavedSearch.path.
- **No new persistence for heuristic:** Hot list is derived from index; only optional UI settings (e.g. “show hot folders: yes/no”, “max count”) might be persisted.
- **Limiting to user HOME:** Keeps the list about *the user’s* activity (Documents, Desktop, projects under home), avoids system/app paths and other users’ folders; smaller candidate set ⇒ cheaper aggregation and less UI noise.

---

## 6. Cons and Risks

- **Cost of computation:** If done on-demand over the whole index, aggregating by folder and reading LMT can be expensive (lazy load storm). Mitigation: precompute and maintain folder aggregates, or limit to a subset of folders / top K.
- **Staleness:** After index updates (recrawl, USN changes), hot list should refresh; if we precompute, we must update on every relevant change.
- **Misleading “hot”:** Heuristics are imperfect: one touched file can make a large folder “hot”; renames/moves can shift activity across folders. Tooltip or short help (“Based on last modified time”) sets expectations.
- **LMT not always loaded:** Lazy LMT means some entries may be “not loaded” or “failed”. Heuristic should define behavior (e.g. treat as “old” or skip) to avoid UI noise.
- **UI clutter:** Another list in FilterPanel or EmptyState can feel busy. Keep the list short and optionally collapsible or off by default.
- **HOME-only limitation:** If the user indexes only a path outside HOME (e.g. a different drive or network share), the hot-folder list will be empty. Acceptable trade-off for relevance and performance; we can document or tooltip “Hot folders show activity under your user folder.”

---

## 7. Edge Cases

| Edge case | Handling |
|-----------|----------|
| **Empty folder** | No files ⇒ not hot (or show only if we use “directory’s own LMT” for dirs; prefer “max LMT of files under folder”). |
| **All LMTs not loaded** | Skip folder for “recent” tier, or treat as cold; avoid triggering mass lazy load just for hot list. |
| **Very large index** | Cap work: e.g. only consider folders that already appear in path index, or sample; or rely on precomputed folder aggregates. |
| **Crawl root itself** | Can be “hot” if any file under it is recent; show as “(root)” or with crawl folder name. |
| **Deep paths** | Prefer “direct children of crawl root” or “first N levels” for list to keep labels readable; or show last two path components. |
| **Tie-breaking** | When sorting by max LMT, break ties by path or file count so order is stable. |
| **No index / index building** | Hide hot folders or show “Build index to see hot folders”. |
| **Path with special characters** | Use same path normalization and display as elsewhere (path filter, results table). |
| **HOME scope:** | |
| **Crawl root outside HOME** | Only folders that are both under crawl root *and* under HOME are candidates; if crawl root is e.g. `D:\`, no hot folders (or use HOME ∩ index). |
| **HOME not available** | If `GetUserHomePath()` fails or returns default only, either hide hot folders or show empty list with optional tooltip (“User home not available”). |
| **Path prefix matching** | When filtering “under HOME”, normalize (e.g. trailing slash, case on Windows) so we don’t miss or double-count; reuse existing path comparison helpers if any. |

---

## 8. Value for the User

- **“Where did I last work?”** – Hot folders approximate “recently modified” areas without scanning the whole tree manually.
- **“Search only where it matters”** – Scope search to a hot folder in one click instead of typing or browsing.
- **“Catch up after a break”** – After recrawl or reopening the app, a short list of hot folders helps resume context.
- **Consistency** – Same “last modified” notion as in the results table and time filter, so behavior is predictable.

---

## 9. Implementation Sketch (No Code Yet)

1. **Heuristic:**  
   - Define “recent” via existing `CalculateCutoffTime(TimeFilter::ThisWeek)` (or Today).  
   - For each “folder” (directory path or parent_id), compute **max(lastModificationTime)** over files (and optionally subdirs) under it.  
   - Folders with max LMT ≥ cutoff are “hot”; sort by max LMT descending, take top K (e.g. 10).

2. **Data source:**  
   - **Option A (on-demand):** `FileIndex::ForEachEntryWithPath`, group by directory path (prefix), track max LMT per folder; **filter to paths under `GetUserHomePath()`** (prefix check, normalized); optionally skip entries with LMT not loaded to avoid triggering lazy load for entire index.  
   - **Option B (precomputed):** Maintain a `directory_id → max_LMT` (and maybe file_count) updated in `Insert`/`Remove`/`Move`/`Rename`; hot list = sort this map by max LMT, take top K; **when building the list, include only folders whose path is under user HOME**. Option B is more work but scales better.

3. **UI:**  
   - FilterPanel: “Hot folders” subsection; list of path labels (shortened if needed); click sets path filter in state (and optionally triggers search via existing “apply search” path).  
   - EmptyState: 3–5 hot folders as buttons that set path and focus search.  
   - Tooltip: “Based on last modified time of files in the folder.”

4. **Refresh:**  
   - On-demand: recompute when opening FilterPanel or when index finishes building/recrawl.  
   - Precomputed: update on every index change (or on a short debounce after batch updates).

---

## 10. Summary

| Aspect | Summary |
|--------|---------|
| **What** | “Hot folder” = folder with recent file activity by Last Modified time. |
| **Scope limit** | Restrict to folders **under user HOME** (`path_utils::GetUserHomePath()`); keeps list relevant and reduces cost. |
| **Heuristic** | Prefer max LMT in folder vs cutoff (Today/ThisWeek), or recency tier; reuse `TimeFilterUtils`. |
| **Data** | FileIndex paths + LMT; filter by HOME prefix; watch for lazy-load cost; consider precomputed folder aggregates. |
| **UI** | FilterPanel list + optional EmptyState quick actions; click = set path filter (and optionally search). |
| **Benefits** | Quick navigation to active areas, scoped search, HOME scope keeps list relevant and smaller. |
| **Cons** | Computation cost, staleness, heuristic can be misleading, possible clutter. |
| **Edge cases** | Empty folders, unloaded LMT, large index, crawl root, deep paths, no index; **HOME unset or crawl outside HOME**. |
| **User value** | “Where did I last work?”, one-click scope search, catch-up after break, consistent “last modified” semantics. |

This document is analysis only; no code or CMake changes. Implementation would follow normal project flow (spec, AGENTS.md, tests).
