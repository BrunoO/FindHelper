# Memory impact of fix/duplicate-path-index branch

**Date:** 2026-02-19  
**Purpose:** Analyze whether the changes on `fix/duplicate-path-index` can explain higher app memory usage.

---

## Conclusion

**Yes.** The branch introduces one **new in-memory structure** that scales with index size: **`FileIndex::path_to_id_`**. That is the only change that unambiguously **increases** memory. The other code changes either reduce or leave memory roughly unchanged.

---

## 1. FileIndex – `path_to_id_` (new)

**Change:** Added `hash_map_t<std::string, uint64_t> path_to_id_` to enforce one path = one entry (Option B).

**Memory impact:**
- For **every indexed path** we now store:
  - One **normalized full path string** (key): canonical separator, trimmed, lowercased on Windows/macOS
  - One `uint64_t` file id (value)
- Paths are already stored elsewhere (PathStorage / path buffer). `path_to_id_` is an **additional copy** of each path, normalized, used only for deduplication lookups.

**Rough estimate:**  
If the index has **N** files and average path length is **L** bytes:
- Keys: N × (L + 1) bytes (string data) + N × (node overhead, typically a few pointers)
- Values: N × 8 bytes  
So extra memory is on the order of **N × (L + 20..40)** bytes (e.g. 500k files × 80 bytes ≈ **40 MB**).

**Conclusion:** This is the **dominant** new memory cost and can explain “the app uses more memory” with this branch.

---

## 2. GuiState – `deferredCloudFiles` and `markedFileIds` (std::set → hash_set_t)

**Change:** Replaced `std::set<uint64_t>` with `hash_set_t<uint64_t>`.

**Memory impact:**
- `std::set` is typically a red-black tree (several pointers + key per node).
- `hash_set_t` (unordered_set) uses a bucket array + nodes; for small sets, per-element overhead is often **lower** than for a tree.
- So this change is more likely to **slightly reduce** or keep memory similar than to increase it.

---

## 3. SearchController / SearchWorker – removed path deduplication sets

**Change:** Removed `std::unordered_set<std::string> seen_paths` used when merging/streaming results (because the index now guarantees one path = one entry).

**Memory impact:**
- **Before:** Temporary sets of path strings were built during merge and when consuming streaming batches.
- **After:** No such sets; we just append results.
- So **peak memory during search** should **decrease** (no large temporary set of path strings).

---

## 4. Summary table

| Change | Effect on memory |
|--------|-------------------|
| **FileIndex::path_to_id_** (new map: normalized path → id) | **Increases** – one extra string per indexed file + map overhead |
| GuiState: std::set → hash_set_t (deferredCloudFiles, markedFileIds) | Neutral or small **decrease** |
| SearchController / SearchWorker: removed seen_paths | **Decreases** peak memory during search |

---

## 5. If you need to reduce memory

- **path_to_id_** is the only new scalable structure. Options to reduce its footprint (each with trade-offs):
  - **Hash-only key:** Store only `hash(normalized_path) → id` and accept rare hash collisions (e.g. second-level check only when needed). Saves the N × L string storage at the cost of complexity and possible collisions.
  - **Path interning:** Store path strings in a single pool and keep only pool offsets in the map (reuse PathStorage or a dedicated pool). Removes duplicate path text but requires design and possibly lock/thread considerations.
  - **Document and accept:** If the extra memory is acceptable for your target index sizes, document the cost in the duplicate-path fix plan and in code comments so future changes don’t add more duplicate path storage without considering it.

---

## 6. Why two path copies? Can we have one path for all purposes?

**Why we need a normalized form at all**  
Deduplication must treat different spellings of the same path as one: `C:\Foo\Bar`, `c:\foo\bar`, and `C:/Foo/Bar` should map to a single entry. So we need one **canonical** form (separator, trailing slash, case on Windows/macOS). That canonical form is what we use as the key for "path already in index?".

**Why there are two copies today**  
- **PathStorage** stores the path in whatever form was passed in (crawler, USN, etc.) – and when we reuse an existing id we call `InsertPathLocked(id, full_path, ...)` with the **original** `full_path`, so the buffer can keep mixed forms.  
- **path_to_id_** uses the **normalized** path as the map key so we can look up by canonical form. The map therefore keeps its own copy of that string (the key).  
So we have: one representation in the path buffer (authoritative for display/search but not necessarily normalized), and a second representation as the map key (normalized, for dedup). Hence two copies.

**How to have one path for all purposes**  
We don't *need* two copies; the current design chose a simple map key (full normalized string). To use a single path everywhere:

1. **Store only the normalized path**  
   When inserting or refreshing, pass the **normalized** path into PathStorage (e.g. pass `norm` instead of `full_path` to `InsertPathLocked`). Then the path buffer is the single source of truth and is already in canonical form for display, search, and dedup.

2. **Stop storing the path string in the map**  
   Use **hash(normalized_path) → id** instead of **normalized_path → id**. On insert: compute `norm`, then `h = hash(norm)`. Look up by `h`; if found, confirm with `GetPathLocked(existing_id) == norm` (collision resolution). If not found or no match, insert and store `path_to_id_[h] = id`. The map then holds only hash + id (and possibly a short collision chain), not the path string. The path exists only in PathStorage (normalized).

Result: **one path** (normalized) stored once in PathStorage; the dedup structure is a small hash→id map with collision resolution via the stored path. No duplicate path strings; extra memory from path_to_id_ drops to a small constant per entry (hash + id) instead of N × L.

**User-facing consequences of storing only the normalized path**  
If we make PathStorage the single source of truth and store only the normalized form, that form is what the user sees and uses everywhere (results table, tooltips, copy path, open file, open parent folder, context menu, drag-drop):

| Aspect | Consequence |
|--------|-------------|
| **Display** | Paths appear in **normalized** form: lowercase on Windows and macOS, forward slashes, no trailing slash. Users used to "system" or original casing (e.g. `C:\Users\Foo\Documents`) would see `c:/users/foo/documents`. Purely cosmetic; open/copy still work on case-insensitive systems. |
| **Open / Copy / Explorer** | On Windows and default macOS (case-insensitive FS), opening a file or folder by the normalized path works. Same for copy-to-clipboard and "Open in Explorer/Finder". No functional break. |
| **Case-sensitive filesystems** | **Risk:** On **Linux** (typically case-sensitive) we currently do *not* normalize case, so no change. On **macOS with a case-sensitive volume** (e.g. case-sensitive APFS), normalizing to lowercase would treat `File.txt` and `file.txt` as the same path and **merge them into one entry** – one of the two files would disappear from the index from the user's perspective. So if we ever store-only-normalized, we must **not** apply case normalization on case-sensitive filesystems (or detect FS case-sensitivity and skip case normalization there). |
| **Expectation** | Some users expect paths to match what the OS or Explorer/Finder shows. Showing lowercase everywhere is a deliberate UX choice: simpler and consistent, at the cost of not preserving "original" casing. |

In short: from a user perspective, storing only the normalized path mainly changes **how paths look** (lowercase, forward slashes) and **where they work** (fine on Windows/default macOS; wrong on case-sensitive volumes if we normalize case there). Functionality (open, copy, search) is unchanged on the usual case-insensitive setups.

---

## 7. Do we need to normalize case?

**Short answer:** No, not necessarily. Case normalization is only useful when the same logical path can reach the index with different casing (e.g. crawler sends `C:\Users\Foo`, USN sends `c:\users\foo`). That can happen on case-insensitive OS, but **in practice it may be rare**: each producer uses one API (crawler: FindFirstFileExW / readdir; USN: filename from the journal), so casing is usually consistent for a given source. We have not seen evidence that mixed-case duplicates are a real problem.

**If we drop case normalization** (and only normalize separator + trailing slash):

| Pro | Con |
|-----|-----|
| Simpler normalization; no ToLower. | We might get two index entries for the same file if two producers ever send different casing (e.g. crawl then USN with different casing). |
| No risk of merging distinct files on **case-sensitive** FS (Linux, macOS case-sensitive APFS): `File.txt` and `file.txt` stay two entries. | Rare duplicate would only show up if we actually see it in the wild. |
| If we later store “one path” only, we preserve **original** casing in the UI instead of forcing lowercase. | |

**Recommendation:** Consider **removing case normalization** from `NormalizePathForDedup` and keeping only separator canonicalization and trailing-slash trim. If duplicate paths that differ only by case ever appear (e.g. from mixed crawler + USN), we can add case normalization back or limit it to a specific platform where it’s observed. That way we avoid the case-sensitive-FS bug and simplify behavior unless we have evidence we need it.

---

## References

- `docs/plans/2026-02-18_DUPLICATE_PATH_INDEX_FIX_PLAN.md` – Option B and path_to_id_
- `src/index/FileIndex.h` / `FileIndex.cpp` – path_to_id_, NormalizePathForDedup, RebuildPathToIdMapLocked
