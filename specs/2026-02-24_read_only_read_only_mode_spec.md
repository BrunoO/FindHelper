## Feature Spec: `--read-only` No-Disk-Write Mode

### 1. Requirements

#### 1.1 Functional Requirements

- **New CLI flag**
  - **Name**: `--read-only`
  - **Scope**: Process-wide; applies from startup until process exit.
  - **Behavior**:
    - When present, the application **must not perform any filesystem write operation** on any drive/volume.
    - When absent, current behavior is unchanged.

- **Operations that must be inhibited in read-only mode**  
  Based on `README.md` lines 23–31 plus generalization:

  - **Deleting / moving files and folders**
    - Any operation that moves items to:
      - Recycle Bin (Windows)
      - Trash (macOS / Linux)
    - Any direct delete/move/rename/write operation.
    - In read-only mode:
      - UI affordances for deletion/move (buttons, menu items, keyboard shortcuts like Delete) are **disabled/greyed**.
      - Programmatic delete/move APIs are **not invoked**.
      - If triggered via non-UI paths (e.g. regression tests, scripting hooks), calls must be **no-ops that log a warning**, not touching disk.

  - **Saving settings**
    - Writing `settings.json` to the user profile (any platform).
    - In read-only mode:
      - Settings UI still allows temporary in-memory changes during the session.
      - Any attempt to persist settings to disk is **skipped**.
      - The user is clearly informed (UI label / tooltip or one-time banner) that settings changes will not be saved in read-only mode.
      - No partial or alternative location write is allowed (no temp settings file on disk).

  - **Exporting results**
    - Writing CSV (or other export formats) to user-chosen paths.
    - In read-only mode:
      - Export actions are disabled/greyed (buttons and menu entries).
      - If export is attempted via non-UI APIs, the operation fails fast with a user-visible error and **no file is created**.

  - **Drag and drop that moves or copies files**
    - Initiating shell drag-and-drop that can move/copy files to other folders or apps.
    - In read-only mode:
      - Drag operations that **represent file movement/copying** are disabled.  
        (Scrolling/selection gestures that are currently implemented via drag but do not cause FS writes are still allowed.)
      - On Windows, shell drag-drop data objects for file transfers must not be created/registered.

  - **Quick Access pinning (Windows)**
    - Pinning a file/folder to Windows Explorer Quick Access.
    - In read-only mode:
      - Quick Access pin/unpin actions are disabled/greyed.
      - Under the hood, any Shell API that modifies Quick Access state is not called.

  - **Logging to `FindHelper.log`**
    - The log file in the system temp/cache directory **must not be written** in read-only mode.
    - Instead:
      - Logging continues to in-memory structures and stderr/stdout where already supported.
      - If the logger normally opens a file, in read-only mode it must either:
        - Not open the file at all, or
        - Open in read-only mode only if needed to inspect (not expected here).
      - There are **no new log files**, and existing log files are not appended.

  - **Index dumping (`--dump-index-to`)**
    - The feature that “dumps the full index of paths to a text file” must not write to disk.
    - In read-only mode:
      - If `--dump-index-to` is also passed:
        - The application **exits with a clear error** (non-zero exit code) before starting the main run OR
        - Ignores `--dump-index-to` and prints a clear error to stderr and in logs.
      - No index dump file is created.

  - **Other potential writes**
    - Any new or existing feature that writes to disk (caches, diagnostic dumps, crash reports, profile data, temporary files, etc.) must check the read-only flag and:
      - Either be disabled in read-only mode, or
      - Be explicitly documented as **not used** when read-only mode is enabled.
    - Aim for **minimal code changes** by:
      - Centralizing the “can write to disk?” decision in one or a few helper functions.
      - Gating existing write paths through those helpers instead of reworking their internals.

- **User-visible behavior**
  - In read-only mode:
    - The main window displays a small, non-intrusive indicator (e.g. “Read-only mode”) somewhere existing UI already uses for mode/status (no new complex layout).
    - UI elements corresponding to inhibited actions are **visibly disabled/greyed** and, where reasonable, show a tooltip like:
      - “Disabled in read-only mode (`--read-only`); this action would modify files or settings.”
    - Command-line help (`--help`) documents the new flag and its effect in concise bullets.

- **Error handling**
  - If a write operation is requested in read-only mode:
    - **Preferred**: The option is disabled so it cannot be requested.
    - If still requested (e.g. via non-UI API), the app:
      - Logs a warning (to non-file target).
      - Returns a clear error status or no-op result.
      - Does **not** throw unhandled exceptions or terminate unexpectedly.

#### 1.2 Non-Functional Requirements

- **Safety / correctness**
  - In read-only mode, **no filesystem write APIs** should be called:
    - OS-level write APIs (`CreateFile` with write flags, `WriteFile`, `DeleteFile`, `MoveFile`, `unlink`, `rename`, `open` with write flags, etc.).
    - Higher-level wrappers inside this project that are known to write/delete.
  - This must hold across all supported platforms (macOS, Windows, Linux).
  - The guarantee is **best-effort, code-based**; we will build tests and centralize checks to minimize risk.

- **Performance**
  - The presence of the read-only flag should:
    - Add at most a few cheap boolean checks on code paths that already exist.
    - Not introduce additional heap allocations on hot paths (search, indexing).
  - When the flag is **not** used, the performance of the application must be indistinguishable from current behavior.

- **Minimal code changes**
  - Preference order:
    1. Add a **central “read-only” capability check** and:
       - Disable UI entry points (greying out menu items/buttons).
       - Guard existing write functions with simple `if (!IsReadOnlyMode()) { ... }`.
    2. Avoid deep refactors of low-level FS utilities unless necessary.
    3. Avoid new abstractions that are not directly used by this feature (YAGNI).

- **Architectural constraints**
  - C++17 only; follow `AGENTS.md` rules (RAII, explicit lambda captures, `std::string_view` for read-only strings, no C-style arrays, etc.).
  - No attempts to “unify” platform-specific `#ifdef` blocks; if per-platform behavior is needed, use new abstractions with separate implementations.
  - No backward-compat modes; `--read-only` is added cleanly; we do not keep alternate legacy behaviors.

---

### 2. User Stories (Gherkin)

| ID | Priority | Summary | Gherkin |
|----|----------|---------|---------|
| US1 | P0 | Launch with `--read-only` and see mode indicator | Given the user starts FindHelper with `--read-only` When the main window is displayed Then a small status text indicates "Read-only mode" And no file-writing operations are enabled |
| US2 | P0 | Delete/move operations are disabled in read-only mode | Given the app is started with `--read-only` And a search result list is shown When the user selects entries and presses Delete or uses a delete/move menu item Then the UI control is disabled or the action is refused with a clear message And no file is moved, deleted, or sent to Recycle Bin/Trash |
| US3 | P0 | Settings are not persisted in read-only mode | Given the app is running in `--read-only` mode And the user changes preferences in the Settings window When the user closes the app and restarts it without `--read-only` Then the previously changed settings are not loaded from disk (i.e., they were not saved) And no `settings.json` write occurred while in read-only mode |
| US4 | P0 | Export results is disabled | Given the app is started with `--read-only` And search results are visible When the user attempts to export results to CSV Then the export menu/button is disabled or shows a clear error And no CSV file is created or modified on disk |
| US5 | P0 | Quick Access pinning is disabled on Windows | Given the app runs on Windows with `--read-only` And a file/folder is selected When the user uses the Quick Access pin UI Then the option is disabled or the action is refused with a clear message And no Quick Access pin/unpin modification is performed |
| US6 | P0 | Logging avoids writing `FindHelper.log` | Given the app is started with `--read-only` When operations that usually log are executed Then the app still logs to stderr/console or memory as configured But no `FindHelper.log` file is created or appended to on any disk |
| US7 | P0 | `--dump-index-to` conflicts with `--read-only` | Given the user starts FindHelper with both `--read-only` and `--dump-index-to=somefile.txt` When the app parses command-line arguments Then it prints a clear error about incompatible options And exits with a non-zero status without creating any dump file |
| US8 | P1 | Drag-and-drop file moving/copying is disabled | Given the app is running with `--read-only` And the user tries to drag results to a system file explorer or other app in a way that would move/copy files When the drag operation would normally initiate a file move/copy Then the drag is not initiated or is refused And no filesystem modification occurs |
| US9 | P1 | CLI help documents read-only mode | Given the user runs the application with `--help` When help text is displayed Then the `--read-only` flag is documented with a short explanation of which operations it disables |
| US10 | P2 | Non-UI APIs respect read-only mode | Given a test or helper invokes export/delete/pin APIs directly while read-only mode is enabled When those APIs are called Then they return an error or no-op status And do not call any underlying filesystem write/delete APIs |

---

### 3. Architecture Overview

#### 3.1 High-Level Design

- **Global configuration**
  - Extend the existing command-line parsing / settings bootstrap code to:
    - Parse `--read-only`.
    - Store a **single source of truth** boolean (e.g. `AppMode::read_only` or `AppSettings::read_only_mode`) in a central configuration structure already threaded through the app.
  - Provide a **read-only query helper**:
    - Example: `bool IsReadOnlyMode() noexcept;` or `bool AppMode::IsReadOnly() const noexcept;`.
    - Accessible from both UI code and lower-level modules by passing a lightweight reference or pointer (avoid globals where possible, but align with existing patterns).

- **Write-capability gating**
  - Introduce one or two small helper functions that encapsulate the policy:
    - `bool CanModifyFileSystem() noexcept;` (returns `false` when read-only is enabled).
    - Optional: `void FailIfReadOnly(const char* action_description);`
      - Asserts/logs when a write attempt is made in read-only mode (for non-UI code paths).
      - In debug builds, may use assertions; in release, logs and returns safely.
  - Wherever the code currently performs a disk write, wrap the **entry point** in:
    - A check of `CanModifyFileSystem()` to short-circuit.
    - UI-level disabling to prevent users reaching that code path where feasible.

- **UI integration (ImGui)**
  - The `GuiState` or equivalent central UI state type is extended with a **read-only flag**, or it can reference the central app settings that already exist.
  - When rendering UI:
    - **Mode indicator**: Use existing status bar or top info text area to render “Read-only mode (`--read-only` active)” when enabled.
    - **Controls disabling**:
      - Before rendering delete/move/export/pin buttons or menus, check `IsReadOnlyMode()`:
        - Use `ImGui::BeginDisabled()` / `ImGui::EndDisabled()` or equivalent to visually disable and block interaction.
        - Optionally show tooltips explaining the reason.
    - **Drag-and-drop**:
      - Wherever drag-and-drop for file transfers is initiated, gate it on `!IsReadOnlyMode()`.

- **Logging**
  - Adapt the logging subsystem initialization to:
    - Decide **at startup** whether to open/create `FindHelper.log`, based on the read-only flag.
    - In read-only mode:
      - Skip the file sink entirely; only use non-file sinks already supported.
      - Ensure existing logging API remains unchanged from caller perspective (no new mandatory parameters).

- **Index dumping & other CLI write operations**
  - Integrate `--read-only` into the command-line argument validation:
    - On detecting a combination of `--read-only` and `--dump-index-to`, print an error and exit early.
  - For any other CLI options that write to disk, perform a similar validation.

#### 3.2 Minimal Change Strategy

- **Do not refactor core filesystem modules** beyond:
  - Adding a small, well-localized `if (IsReadOnlyMode()) { ... }` at call sites.
  - Ensuring they gracefully handle “no-op because read-only” responses from higher-level logic.
- **Prefer adjustments at the “edges”**:
  - UI button/shortcut enabling.
  - Argument-parsing validation.
  - High-level command handlers (e.g. “perform delete”, “export CSV”, “pin to Quick Access”).
- **Keep dependencies simple**:
  - Pass `const AppMode&` or similar to components that need to know about read-only, instead of introducing new singletons.

---

### 4. Acceptance Criteria

| Story | Criterion | Measurable Check |
|-------|-----------|------------------|
| US1 | Mode indicator appears when `--read-only` is set | Run app with `--read-only`; visually confirm status text present; automated UI test verifies presence of the exact label in the main window. |
| US2 | No delete/move possible in read-only mode | Automated tests: simulate delete via UI and via command handler in read-only; verify that filesystem is unchanged (e.g. via temp dir fixtures) and that the delete handler exits early when `IsReadOnlyMode()` is true. |
| US3 | Settings not written in read-only mode | Functional test: run app with `--read-only`, change settings, close; confirm `settings.json` modification time and contents unchanged; process logs indicate skipped settings write. |
| US4 | Export disabled/no-op in read-only mode | Automated test: attempt export in read-only mode; assert no file exists at target path, and UI/API returns an error or disabled state. |
| US5 | Quick Access pinning disabled | On Windows, test run with `--read-only`; verify Quick Access pin UI is disabled and relevant Shell API (e.g. pin/unpin) is never called (e.g. via abstraction spy or logs). |
| US6 | No `FindHelper.log` writes | Run app with `--read-only` under a temp directory; confirm log file is not created/modified while operations execute. When run without `--read-only`, logging behavior matches current baseline. |
| US7 | `--dump-index-to` rejected with `--read-only` | CLI test: run `findhelper --read-only --dump-index-to=out.txt`; process exits with non-zero code and prints a clear message to stderr; ensure `out.txt` does not exist. |
| US8 | Drag-and-drop that writes FS is disabled | UI regression test (where framework exists): in read-only mode, try to initiate a drag from a result to an external target; confirm the internal drag start handler is not called or is aborted; no filesystem changes occur. |
| US9 | Help text documents `--read-only` | Run `findhelper --help`; parse help text and assert a line describing `--read-only` and listing the main disabled operations. |
| US10 | Non-UI APIs respect flag | Unit/integration tests calling delete/export/pin functions with read-only flag on; assert they return specific error statuses and do not reach the low-level write APIs (e.g. via mocks/spies). |
| ALL | No new static analysis issues | clang-tidy and SonarQube on modified files report no new warnings; existing rules from `AGENTS.md` (e.g. explicit lambda captures, `std::string_view`, DRY constants, no `} if (`) are satisfied. |

---

### 5. Task Breakdown

| Phase | Task | Dependencies | Estimate | Notes |
|-------|------|--------------|----------|-------|
| Spec | Finalize spec and review impacts | None | 0.5–1.0 h | Confirm this spec aligns with `AGENTS.md`, DRY constants, and existing architecture. |
| Implementation 1 | Add `--read-only` parsing and central flag | Spec | 1.0–1.5 h | Extend CLI parser and app mode/settings; expose `IsReadOnlyMode()` helper; update help text. |
| Implementation 2 | Wire flag into logging initialization | Impl 1 | 1.0 h | Ensure logger skips creating `FindHelper.log` in read-only; verify behavior for non-read-only unchanged. |
| Implementation 3 | Gate delete/move operations & related UI | Impl 1 | 2.0–3.0 h | Identify all delete/move entry points; guard with `IsReadOnlyMode()`; disable/grey UI controls; add tooltips. |
| Implementation 4 | Gate settings persistence | Impl 1 | 1.5–2.0 h | Ensure settings save routines skip disk writes in read-only; add user-facing note in Settings UI. |
| Implementation 5 | Gate export and Quick Access pinning + drag-drop | Impl 1 | 2.0–3.0 h | Disable export actions, Quick Access pinning (Windows), and drag-drop file transfers in read-only; minimal changes via edge gating. |
| Implementation 6 | Validate CLI conflicts (`--dump-index-to` + others) | Impl 1 | 1.0–1.5 h | Add early validation, error messages, exit codes; ensure no partial work begins. |
| Tests 1 | Add unit/integration tests for core flag behavior | Impl 1–6 | 3.0–4.0 h | Tests for CLI parsing, settings saving, export, delete/move, logging, `--dump-index-to` conflict; use temp directories. |
| Verification | Run macOS test suite | After code changes | 0.5–1.0 h | Run `scripts/build_tests_macos.sh` once C++ changes are stable; fix any build/test failures. |
| Polishing | Code cleanup and Sonar/clang-tidy checks | After tests | 1.0–1.5 h | Ensure no new static analysis issues; apply Boy Scout fixes near touched code; update docs/README as needed. |

---

### 6. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|-----------|
| Hidden write paths not covered by new checks (e.g. rare diagnostics, temp files) | Could still cause disk writes in read-only mode, violating guarantee | Perform a targeted search for all file write/delete calls (filesystem utils, OS APIs) and audit them; centralize “can write?” check; add tests that exercise typical flows under read-only and monitor filesystem changes in temp dirs. |
| UI controls missed and remain active | User might still trigger a write despite flag | Enumerate all UI entry points related to listed operations; cross-check with README list and code search; add regression tests for key UI actions where the test framework exists. |
| Performance overhead from additional checks | Slight slowdown on hot paths if checks are in inner loops | Ensure `IsReadOnlyMode()` is a trivial, inlined boolean read from immutable config; only place checks at command boundaries, not inside tight loops. |
| Platform-specific behavior divergence (especially Windows Shell APIs) | Read-only mode might be incomplete or inconsistent on Windows vs macOS/Linux | Abstract platform-specific operations behind small wrappers that all consult the central flag; test read-only mode specifically on Windows for Quick Access and drag-drop. |
| Misconfiguration of logging leading to no logs at all | Harder diagnostics in read-only mode | Maintain logging to stderr/console in read-only mode; document in README that file logs are disabled but console logs remain. |
| CLI compatibility for existing scripts using `--dump-index-to` | Scripts may unexpectedly fail when they add `--read-only` | Provide a clear CLI error message about incompatibility; document this in README/help; no backward compatibility is required, but clarity is still important. |

---

### 7. Validation & Handoff

- **Review checklist**
  - Implementation obeys:
    - `AGENTS.md` (C++17 only, RAII, explicit lambda captures in templates, `std::string_view` for read-only strings, in-class member initializers, no C-style arrays, etc.).
    - Clang-tidy and SonarQube alignment (no `} if (` on one line, DRY, const correctness, no unused code).
  - All write-related features enumerated in `README.md` 23–31 are gated by the read-only flag:
    - Delete/move to Recycle Bin/Trash.
    - Settings saving.
    - Export to CSV.
    - Drag-and-drop causing FS writes.
    - Quick Access pinning.
    - Logging to `FindHelper.log`.
    - Index dumping via `--dump-index-to`.
  - Normal mode (without `--read-only`) behaves exactly as before (no regressions).
  - `scripts/build_tests_macos.sh` completes successfully after C++ changes.
  - No new public API is added purely for backward compatibility.

- **How to use this spec with Cursor Agent/Composer**
  - Provide this spec plus references to `AGENTS.md` and `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md` in the implementation prompt.
  - Ask the implementing agent to:
    - Implement tasks in the order of the breakdown.
    - Run `scripts/build_tests_macos.sh` on macOS after code changes.
    - Keep changes minimal and localized: add flag, gate existing operations, avoid large refactors.

- **“What’s new” entry**
  - When the feature is shipped:
    - Add a single bullet to the “What’s new” section in `SearchHelpWindow.cpp`, with the commit date (YYYY-MM-DD) and a short description:
      - e.g. `2026-02-24 — Added --read-only mode to disable all disk modifications for safer usage.`
    - Ensure only entries from the last calendar month remain; remove older ones per project rule.

