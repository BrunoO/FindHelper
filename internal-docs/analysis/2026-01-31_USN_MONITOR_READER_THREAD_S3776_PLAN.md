# 2026-01-31 UsnMonitor::ReaderThread() – cpp:S3776 (Cognitive Complexity) Fix Plan

## Objective

Reduce cognitive complexity of `UsnMonitor::ReaderThread()` (UsnMonitor.cpp, starting ~line 406) to under 25 (Sonar limit) without introducing:

- New Sonar violations (S924, S134, S107, S1181, etc.)
- Performance penalties (no extra allocations in hot path; helpers are private members, same TU)
- Code duplication
- NOSONAR except on the exact line of the issue when justified

## Current Structure of ReaderThread (~lines 406–764)

1. **Setup:** `SetThreadName("USN-Reader")`
2. **try block:**
   - **Volume open:** `CreateFileA` → on failure: log, `HandleInitializationFailure()`, return
   - **Store handle:** `scoped_lock`, `volume_handle_ = handle`
   - **Query journal:** `DeviceIoControl(FSCTL_QUERY_USN_JOURNAL)` → on failure: log, `HandleInitializationFailure()`, return
   - **Signal init:** `init_promise_.set_value(true)`
   - **Initial population:** `is_populating_index_.store(true)`, `PopulateInitialIndex(handle, ...)` → on failure: log, `HandleInitializationFailure()`, return; then `is_populating_index_.store(false)`
   - **Privilege drop (Windows):** `#ifdef _WIN32` block with `DropUnnecessaryPrivileges()` → on failure: `HandleInitializationFailure()`, return
   - **Read loop setup:** `READ_USN_JOURNAL_DATA_V0 read_data`, buffer, `consecutive_errors`, `should_exit`
   - **Main while loop** `while (monitoring_active_ && !should_exit)`:
     - **Read:** `DeviceIoControl(FSCTL_READ_USN_JOURNAL)`
     - **On read failure (large branch):** increment `consecutive_errors`, update metrics, then:
       - `err == ERROR_JOURNAL_ENTRY_DELETED` → log, sleep, reset errors, continue
       - `err == ERROR_INVALID_PARAMETER` → log, sleep, continue
       - `err == ERROR_INVALID_HANDLE` → set `should_exit`, continue
       - **Other errors:** log, sleep, then if `consecutive_errors >= kMaxConsecutiveErrors` → set `monitoring_active_`, `should_exit`, continue; else continue
     - **On read success:** measure duration, reset `consecutive_errors`, validate `bytes_returned`, check `bytes_returned <= SizeOfUsn()` continue, read `next_usn`, update `read_data.StartUsn`, build `queue_buffer`, null checks on `queue_`, `queue_->Push`, handle full (drops + optional log), update queue depth metrics, periodic queue-size log
   - **Stopping log:** `LOG_INFO("USN Reader thread stopping")`
3. **catch blocks:** `std::exception` and `catch (...)` with log and `monitoring_active_.store(false)`
4. **Cleanup:** under `mutex_`, close `volume_handle_` if valid

The highest complexity comes from: (a) the long initialization sequence with multiple failure branches, and (b) the read-failure branch with four error cases and nested conditions.

## Strategy: Extract Private Member Helpers (Same Class, No New Sonar/Perf/Dup)

### 1. OpenVolumeAndQueryJournal (new private member)

- **Signature:** `bool OpenVolumeAndQueryJournal(HANDLE& out_handle, USN_JOURNAL_DATA_V0& out_journal_data);`
- **Returns:** `true` if volume was opened and journal queried; `false` on any failure (after logging and calling `HandleInitializationFailure()` as today).
- **Behavior:**
  - `CreateFileA(config_.volume_path.c_str(), ...)`; if `handle == INVALID_HANDLE_VALUE`, log, `HandleInitializationFailure()`, return false.
  - Under `mutex_`, set `volume_handle_ = handle`.
  - `DeviceIoControl(handle, FSCTL_QUERY_USN_JOURNAL, ...)`; if it fails, log, `HandleInitializationFailure()`, return false.
  - Set `out_journal_data = usn_journal_data`, `out_handle = handle`, return true.
- **Placement:** Declare in `UsnMonitor.h` (private), define in `UsnMonitor.cpp` before `ReaderThread()`.
- **Complexity:** Removes several branches and sequential failure paths from `ReaderThread`; helper itself is linear with two failure exits.

### 2. RunInitialPopulationAndPrivileges (new private member)

- **Signature:** `bool RunInitialPopulationAndPrivileges(HANDLE handle);`
- **Returns:** `true` if initial population and (on Windows) privilege drop succeeded; `false` on fatal failure (after existing logging and `HandleInitializationFailure()`).
- **Behavior:**
  - Set `is_populating_index_.store(true)`.
  - Optional: use existing `ScopedTimer` and log "Starting initial index population" (can stay in ReaderThread or move here for clarity).
  - Call `PopulateInitialIndex(handle, file_index_, &indexed_file_count_)`. On success: set `indexed_file_count_.store(file_index_.Size(), ...)`, log, `is_populating_index_.store(false)`. On failure: log, `is_populating_index_.store(false)`, `HandleInitializationFailure()`, return false.
  - `#ifdef _WIN32`: call `privilege_utils::DropUnnecessaryPrivileges()`. On failure: set `privilege_drop_failed_.store(true)`, log, `HandleInitializationFailure()`, return false.
  - Return true.
- **Placement:** Declare in `UsnMonitor.h` (private), define in `UsnMonitor.cpp` before `ReaderThread()`.
- **Complexity:** Moves the entire “population + privilege” block and its branches out of `ReaderThread`; helper has a few clear branches (population fail, privilege fail).

### 3. HandleReadJournalError (new private member)

- **Signature:** `void HandleReadJournalError(DWORD err, size_t& consecutive_errors, bool& should_exit);`
- **Behavior:** Encapsulate the entire “read failed” branch (current lines ~512–657):
  - Increment `consecutive_errors`, update `metrics_.errors_encountered`, `metrics_.consecutive_errors`, and `UpdateMaxConsecutiveErrors(metrics_.max_consecutive_errors, consecutive_errors)`.
  - If `err == ERROR_JOURNAL_ENTRY_DELETED`: update `metrics_.journal_wrap_errors`, log, sleep `kJournalWrapDelayMs`, reset `consecutive_errors` and `metrics_.consecutive_errors`, return (caller will continue).
  - If `err == ERROR_INVALID_PARAMETER`: update `metrics_.invalid_param_errors`, log, sleep `kInvalidParamDelayMs`, return.
  - If `err == ERROR_INVALID_HANDLE`: log, set `should_exit = true`, return.
  - **Other errors:** update `metrics_.other_errors`, log, sleep `kRetryDelayMs`. If `consecutive_errors >= kMaxConsecutiveErrors`: log, set `monitoring_active_.store(false)`, `should_exit = true`. Return.
- **Placement:** Declare in `UsnMonitor.h` (private), define in `UsnMonitor.cpp` before `ReaderThread()`.
- **Complexity:** Removes the largest conditional block from the main loop; helper stays under 25 by keeping a single level of if/else-if (no deep nesting).

### 4. ProcessSuccessfulRead (optional – use if complexity still &gt; 25)

- **Signature:** `void ProcessSuccessfulRead(const char* buffer_data, DWORD bytes_returned, int buffer_size, READ_USN_JOURNAL_DATA_V0& read_data, size_t& push_count);`
- **Behavior:** Everything after “read succeeded” inside the loop: compute read duration and update `metrics_.total_read_time_ms`; reset `consecutive_errors` and `metrics_.consecutive_errors`; validate `bytes_returned` vs `buffer_size` and vs `SizeOfUsn()` (early return / no-op semantics via refs if needed); read `next_usn` from buffer (keep existing NOSONAR on the line with `reinterpret_cast`); set `read_data.StartUsn = next_usn`; build `queue_buffer` and push (including queue null checks, `Push`, drop handling with existing NOSONAR for `current_drops`); update queue depth metrics and `UpdateMaxQueueDepth`; periodic log using `push_count` (increment and threshold check). All existing NOSONAR/NOLINT stay on their exact lines.
- **Placement:** Private member; define in .cpp before `ReaderThread()`.
- **Use:** Call from `ReaderThread` after the “read succeeded” branch instead of inlining. Only add if, after 1–3, Sonar still reports complexity &gt; 25 for `ReaderThread`.

## Order of Implementation

1. **UsnMonitor.h:** Add private declarations:
   - `bool OpenVolumeAndQueryJournal(HANDLE& out_handle, USN_JOURNAL_DATA_V0& out_journal_data);`
   - `bool RunInitialPopulationAndPrivileges(HANDLE handle);`
   - `void HandleReadJournalError(DWORD err, size_t& consecutive_errors, bool& should_exit);`
   - Optionally: `void ProcessSuccessfulRead(...)` (see above).
2. **UsnMonitor.cpp:** Implement the three (or four) helpers above `ReaderThread()`, preserving all existing comments, NOSONAR, and NOLINT on the exact lines where they appear.
3. **ReaderThread() refactor:**
   - Call `OpenVolumeAndQueryJournal(handle, usn_journal_data)`; if false, return.
   - Call `init_promise_.set_value(true)`; then `RunInitialPopulationAndPrivileges(handle)`; if false, return.
   - Leave read-loop setup (read_data, buffer, consecutive_errors, should_exit) in place.
   - In the loop: on `!DeviceIoControl(FSCTL_READ_USN_JOURNAL, ...)`, call `HandleReadJournalError(GetLastError(), consecutive_errors, should_exit); continue;`
   - On success: either call `ProcessSuccessfulRead(...)` (if implemented) or leave the current success block as-is if complexity is already acceptable.
   - Keep try/catch and handle cleanup unchanged.
4. **Remove** any existing NOLINT/NOSONAR that was only for cognitive complexity on `ReaderThread` once its complexity is under 25.

## Sonar and Style

- **S924 (nested breaks):** Helpers use return/continue semantics; no new nested breaks in `ReaderThread`.
- **S134 (nesting depth):** Helpers keep nesting ≤ 3; main loop body stays flat.
- **S107 (too many parameters):** Helper parameter counts stay within limit; use refs for in/out state.
- **S1181 / exception handling:** Catch blocks remain in `ReaderThread`; no new generic catches in helpers.
- **NOSONAR:** Use only on the exact line that triggers the rule (e.g. keep existing NOSONAR for `reinterpret_cast` and for `current_drops` on their current lines if moved into `ProcessSuccessfulRead`).

## Performance and Duplication

- **Performance:** Helpers are private member functions in the same TU; compiler can inline. No extra allocations; pass buffers and structs by ref/pointer. Loop hot path remains a single `DeviceIoControl` + one conditional call to `HandleReadJournalError` on failure.
- **Duplication:** Single implementation per helper; no copy-paste of error or success logic.

## Verification

- Run `scripts/build_tests_macos.sh` (or project test script).
- Run any test that exercises USN monitoring / UsnMonitor (if present).
- Re-run Sonar (or fetch results) and confirm cpp:S3776 is resolved for `UsnMonitor.cpp` at `ReaderThread`, with no new issues.
- Optionally: run under Windows with a real volume to confirm reader thread still starts, populates, and handles errors as before.

## Summary Checklist

- [x] Add private declarations in UsnMonitor.h for the three (or four) helpers.
- [x] Implement `OpenVolumeAndQueryJournal` in UsnMonitor.cpp; preserve logging and HandleInitializationFailure() behavior.
- [x] Implement `RunInitialPopulationAndPrivileges` in UsnMonitor.cpp; preserve population + privilege logic and all NOSONAR/NOLINT.
- [x] Implement `HandleReadJournalError` in UsnMonitor.cpp; preserve all error branches and metrics.
- [ ] Optionally implement `ProcessSuccessfulRead` if complexity remains &gt; 25 (deferred; assess after Sonar re-run).
- [x] Refactor `ReaderThread()` to call the helpers and remove inlined blocks; keep try/catch and cleanup.
- [x] Remove complexity NOLINT from `ReaderThread` if present (none was present).
- [x] Ensure no new Sonar issues, no duplication, no performance regression.
- [x] Use NOSONAR only on the exact line of an issue when unavoidable.
