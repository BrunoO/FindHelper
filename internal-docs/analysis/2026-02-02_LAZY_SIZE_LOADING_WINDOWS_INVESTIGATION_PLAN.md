### Context

Recent refactorings exposed a long‑standing bug in lazy size loading on Windows: many files incorrectly remain with a file size of 0 in the UI even though they are non‑empty on disk. The goal of this task is to:

- Identify the **true root cause(s)** across MFT metadata, Win32/FS APIs, and the lazy loader.
- Design the **correct semantics** for “size loaded vs failed vs not loaded”.
- Implement a **robust, tested fix** that works on Windows and keeps macOS behavior sane.

This document is written as a step‑by‑step Taskmaster‑style plan for investigation and remediation.

---

### Phase 1 – Reproduce and characterize precisely

- **1.1 Set up a clean, observable repro on Windows**
  - Clear any existing index using the normal “clear index” / “rebuild index” mechanism in the app.
  - Ensure you are running a build configuration where:
    - `ENABLE_MFT_METADATA_READING` is set as in production (usually enabled).
    - Logging is compiled in (Debug or equivalent).
  - Enable or temporarily add detailed logging for:
    - `PopulateInitialIndex` (initial index population).
    - `MftMetadataReader::TryGetMetadata` and `MftMetadataReader::LogParseStatistics`.
    - `LazyAttributeLoader::GetFileSize`, `LoadFileSizeAttributes`, `UpdateFileSizeCache`.
    - `GetFileAttributes`, `GetFileSize`, `GetFileModificationTime` in `FileSystemUtils`.

- **1.2 Choose a test dataset that triggers the bug**
  - Index a directory tree that includes:
    - Normal local files with non‑zero size.
    - Some zero‑byte files (for control).
    - OneDrive / cloud‑backed or otherwise “special” files where MFT/Win32 may fail or return odd metadata.

- **1.3 Identify concrete “bad” entries**
  - In the UI or via a diagnostic tool, collect a small list of files where:
    - The file exists and has non‑zero size on disk (Explorer / `dir` / PowerShell confirm this).
    - The app permanently shows size `0` even after some time (i.e. lazy loading had a chance to run).
  - For each such file, record:
    - Full path.
    - Whether the file is on OneDrive or a special volume.
    - What the modification time column shows (valid timestamp vs sentinel / `N/A` / `...`).
    - The internal ID (`file_ref_num` / index ID) if easily available (for traceability).

---

### Phase 2 – Trace the full data flow for one bad file

Pick a representative bad file (call it `id_bad`) and trace how its size flows through the system.

- **2.1 Insertion and initial population (MFT + USN)**
  - In `InitialIndexPopulator.cpp`:
    - Focus on `ProcessUsnRecord` and `PopulateInitialIndex`.
    - For `id_bad`:
      - Log:
        - `file_ref_num` and `parent_ref_num`.
        - Whether `is_directory` is true or false.
        - The initial `mod_time` chosen before any MFT call.
      - With `ENABLE_MFT_METADATA_READING` enabled:
        - Log every call to `MftMetadataReader::TryGetMetadata`:
          - Input `file_ref_num` (and masked `mft_file_ref_num`).
          - Return value (true/false).
          - `out_modification_time` and `out_file_size`.
        - Log when `UpdateFileSizeById(file_ref_num, file_size)` is called and with what value.
  - After `PopulateInitialIndex` and `RecomputeAllPaths` complete:
    - Inspect `FileEntry` for `id_bad`:
      - Confirm `isDirectory == false`.
      - Inspect `fileSize` state:
        - Is it `kFileSizeNotLoaded`, `kFileSizeFailed`, or a concrete value (including 0)?

- **2.2 Lazy loader path (`GetFileSizeById`)**
  - Execution chain:
    - `FileIndex::GetFileSizeById(id_bad)` → `LazyAttributeLoader::GetFileSize(id_bad)`.
  - In `LazyAttributeLoader::GetFileSize(id)`:
    - Log `id`, and from `CheckFileSizeNeedsLoading`:
      - `needsLoading` (bool).
      - `needTime` (bool).
      - Whether `entry->fileSize.IsNotLoaded()` and `entry->lastModificationTime.IsNotLoaded()` are true.
    - If `needsLoading` is false, log the cached value returned and why (already loaded / directory / null entry).
    - If `needsLoading` is true:
      - Log the path after step 2 (path extraction):
        - The string returned by `path_storage_.GetPath(id)`.
      - Log the result of step 4 (I/O):
        - `needTime`.
        - The tuple returned by `LoadFileSizeAttributes(path, needTime)`:
          - `size`.
          - `modTime`.
          - `success`.
      - Log what `UpdateFileSizeCache` writes for this ID:
        - The input `size`, `modTime`, `success`, `needTime`.
        - The resulting `updated_entry->fileSize.GetValueOrZero()` after the update.

- **2.3 Platform attribute providers**
  - For the *exact* path of `id_bad`, exercise the platform helper directly:
    - Call `GetFileAttributes(path)` and log:
      - Whether `GetFileAttributesExW` succeeded.
      - If not, whether the `IShellItem2` path is taken and if `PKEY_Size` / `PKEY_DateModified` succeed.
      - Final `FileAttributes { fileSize, lastModificationTime, success }`.
  - If MFT is in use:
    - For the same `file_ref_num`, log:
      - Result of `TryGetMetadata` (success, file size, modification time).
      - Whether `mft_file_ref_num` is masked correctly (`kFileRecordNumberMask`).

---

### Phase 3 – Classify the real failure mode(s)

Using the data from Phase 2, classify which of the following scenarios (or combination) is actually happening for bad files:

- **Scenario A – Provider claims success but with a wrong size (0)**
  - `LoadAttributes` returns:
    - `attrs.success == true`.
    - `attrs.fileSize == 0`.
  - On disk, the file is not empty.
  - Potential sources:
    - MFT extraction (`ParseDataAttribute` / `ParseFileNameAttribute`) misreading `FileSize`.
    - `IShellItem2` returning 0 for an online‑only stub where logical size is non‑zero.

- **Scenario B – Provider fails but we never recover**
  - `attrs.success == false`.
  - `LazyAttributeLoader` marks `fileSize` as failed (or used to incorrectly mark success for size‑only paths).
  - No designed “second attempt” path exists, so file remains forever in a failed state rendered as size 0.

- **Scenario C – MFT seeds a wrong size up front**
  - During `PopulateInitialIndex`:
    - `TryGetMetadata` returns `true` but `out_file_size` is incorrect (e.g. 0).
    - `UpdateFileSizeById(id, file_size)` caches that wrong size.
    - Later `LazyAttributeLoader::GetFileSize(id)` never runs because `fileSize.IsNotLoaded()` is false.

- **Scenario D – Path/ID mismatch**
  - The ID’s path in `PathStorage` does not match the real file (e.g., after moves, renames, or stale index).
  - Attribute providers may be correct about *that* path, but the index is pointing to the wrong location.

Document which scenario(s) you actually observe. The fix must be targeted at those, not just the original “`success = !path.empty()`” bug.

---

### Phase 4 – Define correct semantics for file size loading

Before touching more code, write down and agree on the intended behavior:

- **4.1 Attribute provider contract (per platform)**
  - For every provider (`MftMetadataReader`, `GetFileAttributesExW` + `IShellItem2`, `stat()`):
    - If the file exists and is readable:
      - `success == true`.
      - `fileSize == logical size` (0 only for true zero‑byte files).
    - If the file does not exist or cannot be read (permissions / network / stub issues):
      - `success == false`.
      - `fileSize == 0` (or ignored) – consumer must not treat this as a valid size.

- **4.2 Lazy value semantics**
  - `LazyFileSize`:
    - `kFileSizeNotLoaded`: never attempted, eligible for lazy load.
    - `kFileSizeFailed`: attempted and failed; behavior must be explicit:
      - Either never retry and indicate failure in UI (e.g., “N/A”).
      - Or allow a limited number of retries with a clear strategy (e.g., fallback provider).
  - `GetValueOrZero()` is for UI convenience only; it is *not* a success signal.

- **4.3 Windows MFT vs Win32/IShellItem2**
  - Decide if MFT metadata is:
    - **Authoritative**: on success, we never consult Win32 for size.
    - Or an **optimization**: on success we use it, but can still fall back to Win32 if evidence suggests it is wrong.
  - Decide what “size” means for OneDrive/cloud:
    - Most likely, **logical cloud size** is what the UI should show, not on‑disk allocation.
    - That choice should be reflected consistently across MFT, Win32, and `IShellItem2` usage.

Write this contract down in code comments near `FileSystemUtils::GetFileAttributes`, `MftMetadataReader::TryGetMetadata`, and `LazyAttributeLoader`.

---

### Phase 5 – Implement the fix across the layers

Implementation work should follow the semantics defined above.

- **5.1 MFT reader and initial population**
  - In `MftMetadataReader::TryGetMetadata`:
    - Only return `true` when:
      - Parsing succeeded (`ParseMftRecord` ok).
      - You found a consistent, trustworthy `FileSize` (from `$DATA` or `$FILE_NAME`).
    - When parsing fails or `FileSize` cannot be trusted:
      - Return `false`.
      - Ensure `out_file_size` remains 0 and `out_modification_time` is set to `kFileTimeNotLoaded` (or appropriate sentinel).
  - In `PopulateInitialIndex`:
    - On `mft_succeeded == true`:
      - Call `UpdateFileSizeById(file_ref_num, file_size)` as today.
    - On `mft_succeeded == false`:
      - Do **not** write a permanent 0.
      - Ensure the `FileEntry::fileSize` for this ID ends up as `kFileSizeNotLoaded` so that lazy loader can try FS APIs later.

- **5.2 Win32/FS attribute provider (`FileSystemUtils::GetFileAttributes`)**
  - Windows branch:
    - For `GetFileAttributesExW`:
      - Make sure:
        - On success: `result.fileSize` and `result.lastModificationTime` are correctly populated, `result.success = true`.
        - On failure: do not set `result.success = true`; leave it `false`.
    - For `IShellItem2` fallback:
      - When `GetUInt64(PKEY_Size)` or `GetFileTime(PKEY_DateModified)` fail:
        - Decide explicitly whether that implies overall failure (`result.success = false`) or partial success.
      - Be conservative: do not mark `success == true` with a bogus or unknown size.
  - macOS/Linux branch:
    - `stat()` path is usually correct; just ensure:
      - On success: `result.success = true`.
      - On failure: log, set `result.success = false`, `result.fileSize = 0`, `result.lastModificationTime = kFileTimeFailed`.

- **5.3 Lazy loader (`LazyAttributeLoader`)**
  - `LoadFileSizeAttributes(path, needTime)`:
    - Always call `LazyAttributeLoader::LoadAttributes(path)` exactly once per invocation.
      - Extract `size = attrs.fileSize`.
      - If `needTime == true`, set `modTime` from `attrs.lastModificationTime` when `attrs.success`, or `kFileTimeFailed` otherwise.
      - Set `success = attrs.success` in both branches.
    - Remove any remaining logic that derives success from `!path.empty()` or similar heuristics.
  - `UpdateFileSizeCache`:
    - Preserve the double‑check locking pattern.
    - On `success == true`:
      - Update `fileSize` and, when `needTime`, update `lastModificationTime` if not already set.
    - On `success == false`:
      - Mark `fileSize.MarkFailed()` so future calls know this ID failed loading.
      - Do **not** write `size = 0` as a “valid” cached value.
  - Consider hardening `LoadFileSize(id)` (manual path) to also use `LoadAttributes` so it can distinguish success from failure instead of blindly caching 0.

---

### Phase 6 – Strengthen tests and add regression coverage

- **6.1 Unit tests for lazy loader behavior**
  - Extend `LazyAttributeLoaderTests.cpp` to cover:
    - A backend that returns `(success=false, fileSize=0)`:
      - Verify:
        - `GetFileSize(id)` returns 0 due to `GetValueOrZero()`.
        - The underlying `FileEntry::fileSize.IsFailed()` is true.
        - Subsequent calls do not attempt repeated I/O (no infinite retry).
    - A backend that returns `(success=true, fileSize=nonzero)`:
      - Verify that:
        - The cached size matches exactly.
        - Repeated calls hit the “already loaded” fast path.
  - Add a test for MFT‑seeded sizes:
    - Simulate `UpdateFileSizeById(id, nonzero)` being called before any lazy load.
    - Confirm that `LazyAttributeLoader::GetFileSize(id)` returns the pre‑seeded value without I/O.

- **6.2 Integration tests / end‑to‑end checks**
  - If you have test helpers for index building + lazy loading, add scenarios:
    - “MFT fails, FS succeeds later”:
      - Ensure MFT path reports failure, `fileSize` stays `NotLoaded`, and lazy loader then uses FS API to get the correct size.
    - “MFT succeeds incorrectly (simulated)”:
      - Decide explicitly whether you want lazy loader to trust MFT or to attempt a secondary check; add tests to enforce the chosen behavior.

---

### Phase 7 – Validate on Windows and macOS

- **7.1 Windows validation**
  - Rebuild and run the app with the new logic.
  - Clear and rebuild the index.
  - Re‑check the previously identified “bad” files:
    - Confirm sizes are now correct, or explicitly marked as failed in a way that is visible in the UI (if you choose to expose failure).
  - Review logs:
    - `PopulateInitialIndex` / MFT statistics.
    - Lazy loading statistics:
      - Number of loads.
      - Number of “already loaded”.
      - Number of failures.

- **7.2 macOS validation**
  - Run `scripts/build_tests_macos.sh` after code changes (per project rules).
  - Manually spot‑check:
    - That lazy size loading works for non‑zero and zero‑byte files.
    - That `stat()` failures (e.g. permission issues) do not silently become permanent “0‑size” cache entries.

---

### Deliverables for Jules

- A short summary (1–2 paragraphs) of:
  - Which scenario(s) turned out to be the real root cause.
  - What semantics were chosen for MFT vs Win32 vs lazy loading.
- A link to:
  - The main fix MR/PR.
  - Any follow‑up clean‑up MRs (e.g., logging, tests, or clang‑tidy clean‑ups that were part of the work).
- Confirmation that:
  - The bug is no longer reproducible on your original repro dataset.
  - The change passes `scripts/build_tests_macos.sh` on macOS and the Windows build/tests in CI.

### Context

Recent refactorings exposed a long‑standing bug in lazy size loading on Windows: many files incorrectly remain with a file size of 0 in the UI even though they are non‑empty on disk. The goal of this task is to:

- Identify the **true root cause(s)** across MFT metadata, Win32/FS APIs, and the lazy loader.
- Design the **correct semantics** for “size loaded vs failed vs not loaded”.
- Implement a **robust, tested fix** that works on Windows and keeps macOS behavior sane.

This document is written as a step‑by‑step Taskmaster‑style plan for investigation and remediation.

---

### Phase 1 – Reproduce and characterize precisely

- **1.1 Set up a clean, observable repro on Windows**
  - Clear any existing index using the normal “clear index” / “rebuild index” mechanism in the app.
  - Ensure you are running a build configuration where:
    - `ENABLE_MFT_METADATA_READING` is set as in production (usually enabled).
    - Logging is compiled in (Debug or equivalent).
  - Enable or temporarily add detailed logging for:
    - `PopulateInitialIndex` (initial index population).
    - `MftMetadataReader::TryGetMetadata` and `MftMetadataReader::LogParseStatistics`.
    - `LazyAttributeLoader::GetFileSize`, `LoadFileSizeAttributes`, `UpdateFileSizeCache`.
    - `GetFileAttributes`, `GetFileSize`, `GetFileModificationTime` in `FileSystemUtils`.

- **1.2 Choose a test dataset that triggers the bug**
  - Index a directory tree that includes:
    - Normal local files with non‑zero size.
    - Some zero‑byte files (for control).
    - OneDrive / cloud‑backed or otherwise “special” files where MFT/Win32 may fail or return odd metadata.

- **1.3 Identify concrete “bad” entries**
  - In the UI or via a diagnostic tool, collect a small list of files where:
    - The file exists and has non‑zero size on disk (Explorer / `dir` / PowerShell confirm this).
    - The app permanently shows size `0` even after some time (i.e. lazy loading had a chance to run).
  - For each such file, record:
    - Full path.
    - Whether the file is on OneDrive or a special volume.
    - What the modification time column shows (valid timestamp vs sentinel / `N/A` / `...`).
    - The internal ID (`file_ref_num` / index ID) if easily available (for traceability).

---

### Phase 2 – Trace the full data flow for one bad file

Pick a representative bad file (call it `id_bad`) and trace how its size flows through the system.

- **2.1 Insertion and initial population (MFT + USN)**
  - In `InitialIndexPopulator.cpp`:
    - Focus on `ProcessUsnRecord` and `PopulateInitialIndex`.
    - For `id_bad`:
      - Log:
        - `file_ref_num` and `parent_ref_num`.
        - Whether `is_directory` is true or false.
        - The initial `mod_time` chosen before any MFT call.
      - With `ENABLE_MFT_METADATA_READING` enabled:
        - Log every call to `MftMetadataReader::TryGetMetadata`:
          - Input `file_ref_num` (and masked `mft_file_ref_num`).
          - Return value (true/false).
          - `out_modification_time` and `out_file_size`.
        - Log when `UpdateFileSizeById(file_ref_num, file_size)` is called and with what value.
  - After `PopulateInitialIndex` and `RecomputeAllPaths` complete:
    - Inspect `FileEntry` for `id_bad`:
      - Confirm `isDirectory == false`.
      - Inspect `fileSize` state:
        - Is it `kFileSizeNotLoaded`, `kFileSizeFailed`, or a concrete value (including 0)?

- **2.2 Lazy loader path (`GetFileSizeById`)**
  - Execution chain:
    - `FileIndex::GetFileSizeById(id_bad)` → `LazyAttributeLoader::GetFileSize(id_bad)`.
  - In `LazyAttributeLoader::GetFileSize(id)`:
    - Log `id`, and from `CheckFileSizeNeedsLoading`:
      - `needsLoading` (bool).
      - `needTime` (bool).
      - Whether `entry->fileSize.IsNotLoaded()` and `entry->lastModificationTime.IsNotLoaded()` are true.
    - If `needsLoading` is false, log the cached value returned and why (already loaded / directory / null entry).
    - If `needsLoading` is true:
      - Log the path after step 2 (path extraction):
        - The string returned by `path_storage_.GetPath(id)`.
      - Log the result of step 4 (I/O):
        - `needTime`.
        - The tuple returned by `LoadFileSizeAttributes(path, needTime)`:
          - `size`.
          - `modTime`.
          - `success`.
      - Log what `UpdateFileSizeCache` writes for this ID:
        - The input `size`, `modTime`, `success`, `needTime`.
        - The resulting `updated_entry->fileSize.GetValueOrZero()` after the update.

- **2.3 Platform attribute providers**
  - For the *exact* path of `id_bad`, exercise the platform helper directly:
    - Call `GetFileAttributes(path)` and log:
      - Whether `GetFileAttributesExW` succeeded.
      - If not, whether the `IShellItem2` path is taken and if `PKEY_Size` / `PKEY_DateModified` succeed.
      - Final `FileAttributes { fileSize, lastModificationTime, success }`.
  - If MFT is in use:
    - For the same `file_ref_num`, log:
      - Result of `TryGetMetadata` (success, file size, modification time).
      - Whether `mft_file_ref_num` is masked correctly (`kFileRecordNumberMask`).

---

### Phase 3 – Classify the real failure mode(s)

Using the data from Phase 2, classify which of the following scenarios (or combination) is actually happening for bad files:

- **Scenario A – Provider claims success but with a wrong size (0)**
  - `LoadAttributes` returns:
    - `attrs.success == true`.
    - `attrs.fileSize == 0`.
  - On disk, the file is not empty.
  - Potential sources:
    - MFT extraction (`ParseDataAttribute` / `ParseFileNameAttribute`) misreading `FileSize`.
    - `IShellItem2` returning 0 for an online‑only stub where logical size is non‑zero.

- **Scenario B – Provider fails but we never recover**
  - `attrs.success == false`.
  - `LazyAttributeLoader` marks `fileSize` as failed (or used to incorrectly mark success for size‑only paths).
  - No designed “second attempt” path exists, so file remains forever in a failed state rendered as size 0.

- **Scenario C – MFT seeds a wrong size up front**
  - During `PopulateInitialIndex`:
    - `TryGetMetadata` returns `true` but `out_file_size` is incorrect (e.g. 0).
    - `UpdateFileSizeById(id, file_size)` caches that wrong size.
    - Later `LazyAttributeLoader::GetFileSize(id)` never runs because `fileSize.IsNotLoaded()` is false.

- **Scenario D – Path/ID mismatch**
  - The ID’s path in `PathStorage` does not match the real file (e.g., after moves, renames, or stale index).
  - Attribute providers may be correct about *that* path, but the index is pointing to the wrong location.

Document which scenario(s) you actually observe. The fix must be targeted at those, not just the original “`success = !path.empty()`” bug.

---

### Phase 4 – Define correct semantics for file size loading

Before touching more code, write down and agree on the intended behavior:

- **4.1 Attribute provider contract (per platform)**
  - For every provider (`MftMetadataReader`, `GetFileAttributesExW` + `IShellItem2`, `stat()`):
    - If the file exists and is readable:
      - `success == true`.
      - `fileSize == logical size` (0 only for true zero‑byte files).
    - If the file does not exist or cannot be read (permissions / network / stub issues):
      - `success == false`.
      - `fileSize == 0` (or ignored) – consumer must not treat this as a valid size.

- **4.2 Lazy value semantics**
  - `LazyFileSize`:
    - `kFileSizeNotLoaded`: never attempted, eligible for lazy load.
    - `kFileSizeFailed`: attempted and failed; behavior must be explicit:
      - Either never retry and indicate failure in UI (e.g., “N/A”).
      - Or allow a limited number of retries with a clear strategy (e.g., fallback provider).
  - `GetValueOrZero()` is for UI convenience only; it is *not* a success signal.

- **4.3 Windows MFT vs Win32/IShellItem2**
  - Decide if MFT metadata is:
    - **Authoritative**: on success, we never consult Win32 for size.
    - Or an **optimization**: on success we use it, but can still fall back to Win32 if evidence suggests it is wrong.
  - Decide what “size” means for OneDrive/cloud:
    - Most likely, **logical cloud size** is what the UI should show, not on‑disk allocation.
    - That choice should be reflected consistently across MFT, Win32, and `IShellItem2` usage.

Write this contract down in code comments near `FileSystemUtils::GetFileAttributes`, `MftMetadataReader::TryGetMetadata`, and `LazyAttributeLoader`.

---

### Phase 5 – Implement the fix across the layers

Implementation work should follow the semantics defined above.

- **5.1 MFT reader and initial population**
  - In `MftMetadataReader::TryGetMetadata`:
    - Only return `true` when:
      - Parsing succeeded (`ParseMftRecord` ok).
      - You found a consistent, trustworthy `FileSize` (from `$DATA` or `$FILE_NAME`).
    - When parsing fails or `FileSize` cannot be trusted:
      - Return `false`.
      - Ensure `out_file_size` remains 0 and `out_modification_time` is set to `kFileTimeNotLoaded` (or appropriate sentinel).
  - In `PopulateInitialIndex`:
    - On `mft_succeeded == true`:
      - Call `UpdateFileSizeById(file_ref_num, file_size)` as today.
    - On `mft_succeeded == false`:
      - Do **not** write a permanent 0.
      - Ensure the `FileEntry::fileSize` for this ID ends up as `kFileSizeNotLoaded` so that lazy loader can try FS APIs later.

- **5.2 Win32/FS attribute provider (`FileSystemUtils::GetFileAttributes`)**
  - Windows branch:
    - For `GetFileAttributesExW`:
      - Make sure:
        - On success: `result.fileSize` and `result.lastModificationTime` are correctly populated, `result.success = true`.
        - On failure: do not set `result.success = true`; leave it `false`.
    - For `IShellItem2` fallback:
      - When `GetUInt64(PKEY_Size)` or `GetFileTime(PKEY_DateModified)` fail:
        - Decide explicitly whether that implies overall failure (`result.success = false`) or partial success.
      - Be conservative: do not mark `success == true` with a bogus or unknown size.
  - macOS/Linux branch:
    - `stat()` path is usually correct; just ensure:
      - On success: `result.success = true`.
      - On failure: log, set `result.success = false`, `result.fileSize = 0`, `result.lastModificationTime = kFileTimeFailed`.

- **5.3 Lazy loader (`LazyAttributeLoader`)**
  - `LoadFileSizeAttributes(path, needTime)`:
    - Always call `LazyAttributeLoader::LoadAttributes(path)` exactly once per invocation.
      - Extract `size = attrs.fileSize`.
      - If `needTime == true`, set `modTime` from `attrs.lastModificationTime` when `attrs.success`, or `kFileTimeFailed` otherwise.
      - Set `success = attrs.success` in both branches.
    - Remove any remaining logic that derives success from `!path.empty()` or similar heuristics.
  - `UpdateFileSizeCache`:
    - Preserve the double‑check locking pattern.
    - On `success == true`:
      - Update `fileSize` and, when `needTime`, update `lastModificationTime` if not already set.
    - On `success == false`:
      - Mark `fileSize.MarkFailed()` so future calls know this ID failed loading.
      - Do **not** write `size = 0` as a “valid” cached value.
  - Consider hardening `LoadFileSize(id)` (manual path) to also use `LoadAttributes` so it can distinguish success from failure instead of blindly caching 0.

---

### Phase 6 – Strengthen tests and add regression coverage

- **6.1 Unit tests for lazy loader behavior**
  - Extend `LazyAttributeLoaderTests.cpp` to cover:
    - A backend that returns `(success=false, fileSize=0)`:
      - Verify:
        - `GetFileSize(id)` returns 0 due to `GetValueOrZero()`.
        - The underlying `FileEntry::fileSize.IsFailed()` is true.
        - Subsequent calls do not attempt repeated I/O (no infinite retry).
    - A backend that returns `(success=true, fileSize=nonzero)`:
      - Verify that:
        - The cached size matches exactly.
        - Repeated calls hit the “already loaded” fast path.
  - Add a test for MFT‑seeded sizes:
    - Simulate `UpdateFileSizeById(id, nonzero)` being called before any lazy load.
    - Confirm that `LazyAttributeLoader::GetFileSize(id)` returns the pre‑seeded value without I/O.

- **6.2 Integration tests / end‑to‑end checks**
  - If you have test helpers for index building + lazy loading, add scenarios:
    - “MFT fails, FS succeeds later”:
      - Ensure MFT path reports failure, `fileSize` stays `NotLoaded`, and lazy loader then uses FS API to get the correct size.
    - “MFT succeeds incorrectly (simulated)”:
      - Decide explicitly whether you want lazy loader to trust MFT or to attempt a secondary check; add tests to enforce the chosen behavior.

---

### Phase 7 – Validate on Windows and macOS

- **7.1 Windows validation**
  - Rebuild and run the app with the new logic.
  - Clear and rebuild the index.
  - Re‑check the previously identified “bad” files:
    - Confirm sizes are now correct, or explicitly marked as failed in a way that is visible in the UI (if you choose to expose failure).
  - Review logs:
    - `PopulateInitialIndex` / MFT statistics.
    - Lazy loading statistics:
      - Number of loads.
      - Number of “already loaded”.
      - Number of failures.

- **7.2 macOS validation**
  - Run `scripts/build_tests_macos.sh` after code changes (per project rules).
  - Manually spot‑check:
    - That lazy size loading works for non‑zero and zero‑byte files.
    - That `stat()` failures (e.g. permission issues) do not silently become permanent “0‑size” cache entries.

---

### Deliverables for Jules

- A short summary (1–2 paragraphs) of:
  - Which scenario(s) turned out to be the real root cause.
  - What semantics were chosen for MFT vs Win32 vs lazy loading.
- A link to:
  - The main fix MR/PR.
  - Any follow‑up clean‑up MRs (e.g., logging, tests, or clang‑tidy clean‑ups that were part of the work).
- Confirmation that:
  - The bug is no longer reproducible on your original repro dataset.
  - The change passes `scripts/build_tests_macos.sh` on macOS and the Windows build/tests in CI.

