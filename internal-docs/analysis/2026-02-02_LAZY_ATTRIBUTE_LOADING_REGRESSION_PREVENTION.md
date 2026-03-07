# Lazy attribute loading – regression prevention options

**Date:** 2026-02-02  
**Purpose:** Options to avoid regressions in lazy loading of file size and modification time (LazyAttributeLoader, MFT, initial index, UI).

---

## Context

Lazy loading of attributes (size, modification time) involves several layers and is easy to break:

- **InitialIndexPopulator** (MFT): may or may not cache size; must not cache unreliable MFT size 0.
- **LazyAttributeLoader**: double-check locking, I/O without lock, success vs failure semantics.
- **FileSystemUtils::GetFileAttributes**: Windows (GetFileAttributesExW + IShellItem2) and macOS/Unix (stat()).
- **SearchResultUtils / UI**: sort-by-Size/LastModified, display, filter caches.

Past bugs included: wrong success detection in size-only path (caching failed load as 0), and MFT size 0 cached so lazy loader never ran (Scenario C).

---

## Option 1 – Strengthen unit tests (recommended)

**Current:** `LazyAttributeLoaderTests.cpp` already covers:

- LoadAttributes success/failure, GetFileSize/GetModificationTime, non-existent and empty path, zero-byte file.
- Manual LoadFileSize/LoadModificationTime, already-loaded and directory.
- Failure scenarios: deleted file, non-existent path, empty path (VerifyFileSizeFailure / VerifyModificationTimeFailure).
- Concurrent access and optimization (load both together).

**Add or extend:**

1. **Success vs failure caching**
   - Mock or fixture where `LoadAttributes` returns `(success = false, fileSize = 0)`.
   - Assert: `GetFileSize(id)` leaves storage as **failed** (e.g. `IsFailed()`), not as “loaded 0”; second call does not retry I/O (or assert failed count).
   - Prevents regressions where “success” is derived from something other than `attrs.success` (e.g. `!path.empty()`).

2. **MFT / initial population contract (if testable on Windows)**
   - When MFT is simulated or disabled: entries that never got a size must remain `kFileSizeNotLoaded` so lazy loader runs.
   - If you have a small test that builds an index and then calls `GetFileSizeById` for an entry that was not given size by MFT, assert that the first call triggers load and the stored value is not “wrong 0” when the FS reports non-zero.

3. **Zero-byte file**
   - Keep or add: real zero-byte file → LoadAttributes success + size 0 → cache 0; display shows 0. Ensures we don’t treat “size 0” as “failed” or “not loaded”.

These tests should live in `LazyAttributeLoaderTests.cpp` (and any index/MFT test file if you add one). Run in CI so changes to LazyAttributeLoader, InitialIndexPopulator, or GetFileAttributes are guarded.

---

## Option 2 – Document semantics in code (recommended)

**Where:** In the implementation files, not only in design docs.

- **LazyAttributeLoader.cpp**  
  At the top or near `LoadFileSizeAttributes` / `UpdateFileSizeCache`: short comment that:
  - `success` must come only from the attribute provider (`attrs.success`); never from `!path.empty()` or similar.
  - On `success == false` we must call `MarkFailed()` and must not write `size` (e.g. 0) as a valid cached value.

- **InitialIndexPopulator.cpp**  
  Keep/expand the comment that:
  - MFT size 0 is not cached; only non-zero MFT sizes are cached so lazy loader can run for 0 and get correct size via FS API.

- **FileSystemUtils.h** (GetFileAttributes)  
  One-line contract: “On success: result.success == true and result.fileSize is the logical size (0 only for true zero-byte files). On failure: result.success == false; result.fileSize must not be treated as valid.”

This makes the “contract” visible at the call sites and reduces the chance that a future change (e.g. a new “optimization”) violates success/failure or MFT 0 semantics.

---

## Option 3 – Change checklist (recommended)

**Before merging changes that touch:**

- `LazyAttributeLoader.cpp/.h`
- `InitialIndexPopulator.cpp` (MFT / UpdateFileSizeById)
- `FileSystemUtils.h` (GetFileAttributes, GetFileSize, GetFileModificationTime)
- `SearchResultUtils.cpp` (StartAttributeLoadingAsync, EnsureDisplayStringsPopulated, sort/display paths)

**Checklist:**

1. **Success vs failure**
   - Any place that decides “load succeeded” must use the provider’s result (e.g. `attrs.success`), not path non-empty or similar.
   - On failure, storage must be updated with “failed” (e.g. `MarkFailed()`), not with 0 as a valid size.

2. **MFT and 0**
   - InitialIndexPopulator must not cache MFT-reported size 0 as a final value; leave as `kFileSizeNotLoaded` so lazy loader runs.

3. **Locks and I/O**
   - No lock held across `GetFileAttributes` / `GetFileSize` / `GetFileModificationTime` (see `LAZY_ATTRIBUTE_LOADER_DESIGN.md`).
   - Double-check after acquiring the unique lock before writing the cache.

4. **Tests**
   - Run `lazy_attribute_loader_tests` (and full test suite if available) after changes.

You can copy this checklist into a short “Lazy loading” section in your PR template or into `docs/design/LAZY_ATTRIBUTE_LOADER_DESIGN.md` so it’s easy to find.

---

## Option 4 – Assertions in debug builds (optional)

**Where it helps:** Catch “success” misuse or “cache 0 on failure” at runtime in development.

- In `UpdateFileSizeCache`:  
  `assert(!success || (size != kFileSizeFailed && size != kFileSizeNotLoaded) || size == 0);`  
  (or a clearer variant: if `success`, then `size` is either a valid size including 0, and never a sentinel.)
- In `LazyAttributeLoader::GetFileSize` (or helper):  
  When calling `UpdateFileSizeCache`, you can assert that when `success == false` we never pass a “valid” size that gets written (we only call `MarkFailed()`).

Keep assertions in `#ifdef NDEBUG` / `#if !defined(NDEBUG)` so they don’t run in release. This is a low-cost way to catch semantic regressions during manual or automated runs of debug builds.

---

## Option 5 – CI and test runs

- **Already:** Run `scripts/build_tests_macos.sh` (or equivalent) on every change to the relevant sources; ensure `lazy_attribute_loader_tests` (and any index tests) are part of that.
- **Optional:** On Windows CI, run the same tests so that Windows-specific paths (GetFileAttributesExW, IShellItem2, MFT) are exercised.
- **Optional:** Add a single “regression” test that encodes the fixed bug (e.g. “provider returns success=false, size=0 → storage is failed, not loaded 0”) so that a revert of the fix would fail the test.

---

## Option 6 – Single “contract” document (optional)

Keep one short doc that lists:

- The three states: **NotLoaded**, **Loaded** (value or 0), **Failed**.
- Who may write what: MFT only caches non-zero size; lazy loader writes only on provider success or MarkFailed.
- That “Loading attributes…” in the status bar is shown for the whole sort-by-Size/LastModified flow.

You can add a “Semantics” subsection to `docs/design/LAZY_ATTRIBUTE_LOADER_DESIGN.md` or keep it in `docs/analysis/2026-02-02_LAZY_SIZE_LOADING_WINDOWS_INVESTIGATION_PLAN.md` (Phase 4) and link to it from the design doc. That gives a single place to look when changing behavior.

---

## Summary

| Option                         | Effort | Impact |
|--------------------------------|--------|--------|
| 1. Strengthen unit tests       | Medium | High   |
| 2. Document semantics in code  | Low    | Medium |
| 3. Change checklist            | Low    | Medium |
| 4. Assertions (debug)          | Low    | Low–Medium |
| 5. CI / test runs              | Low    | High   |
| 6. Single contract document     | Low    | Low–Medium |

**Recommended minimum:** Options 1 (add success/failure and, if possible, MFT 0 tests), 2 (comments in LazyAttributeLoader, InitialIndexPopulator, GetFileAttributes), and 3 (checklist for PRs). Option 5 is assumed (tests in CI). Option 4 and 6 can be added if you want extra safety and a single place to read the contract.

---

## Related docs

- **Design and safety:** `docs/design/LAZY_ATTRIBUTE_LOADER_DESIGN.md`  
  Lock ordering, I/O without lock, double-check, failure handling.

- **Investigation and semantics:** `docs/analysis/2026-02-02_LAZY_SIZE_LOADING_WINDOWS_INVESTIGATION_PLAN.md`  
  Scenarios A–D, Phase 4 semantics, Phase 5 implementation, Phase 6 tests.
