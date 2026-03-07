# Task: Implement macOS Drag-and-Drop for Results Table

**Date:** 2026-02-01  
**Source:** Plan to achieve parity with Windows drag-and-drop on macOS; delegate via taskmaster prompt.  
**Reference:** `docs/plans/2026-02-01_MACOS_DRAG_AND_DROP_PLAN.md`, `docs/prompts/TaskmasterPrompt.md`, `AGENTS document`, `src/platform/windows/DragDropUtils.h`, `src/ui/ResultsTable.cpp`, `src/platform/macos/FileOperations_mac.mm`

---

## Summary

Implement drag-and-drop from the search results table on macOS so that users can drag a filename or full path from the table and drop it onto Finder, Desktop, or other apps (same behavior as on Windows). The UI already detects the drag gesture; the call to start the system drag is currently wrapped in `#ifdef _WIN32`. Add a macOS implementation of `drag_drop::StartFileDragDrop(const std::string& full_path_utf8)` using AppKit (NSPasteboard, drag session), a shared declaration so the UI can call it on both platforms, CMake changes to build the macOS implementation, and doc updates. Do not modify Windows implementation or platform-specific `#ifdef` blocks to change platform intent; follow AGENTS document and the strict constraints document.

***

```markdown
# Prompt: Implement macOS Drag-and-Drop for Results Table

## Mission Context (The "Why")

The application supports drag-and-drop from the search results table on Windows (user drags a file row and drops it onto Explorer, Desktop, or other apps). On macOS, the same gesture is detected but no system drag is started because the implementation is Windows-only. Users on macOS expect to drag files from the table to Finder or other applications. This task implements the macOS side so that behavior matches Windows within the same single-file, files-only scope.

## Core Objective (The "What")

Implement `drag_drop::StartFileDragDrop(const std::string& full_path_utf8)` for macOS in a new platform file, wire the existing results-table drag gesture to call it when running on macOS, and ensure the app builds and the feature works on macOS without changing Windows behavior.

## Desired Outcome (The "How it Should Be")

- On macOS, when the user drags a file row (filename or full path column) past the existing threshold, a system drag session starts and the user can drop the file onto Finder, Desktop, or another app.
- Same API as Windows: `drag_drop::StartFileDragDrop(full_path_utf8)`; single file only; directories rejected; UTF-8 path input.
- ResultsTable.cpp calls this API under `#if defined(_WIN32) || defined(__APPLE__)` and includes a single declaration that is valid on both platforms (shared header or existing Windows header used for declaration only on macOS).
- New code lives under `src/platform/macos/` (e.g. `DragDrop_mac.mm`). CMake adds this source only to the macOS app target.
- Windows implementation and `#ifdef _WIN32` blocks are unchanged. No new Sonar/clang-tidy violations; follow AGENTS document and docs/prompts/AGENT_STRICT_CONSTRAINTS.md.
- Documentation (e.g. ResultsTable.h or Keyboard Shortcuts popup) updated to say drag-and-drop is supported on Windows and macOS.

## Visual Workflow (Mermaid)

    ```mermaid
    graph TD
        A[Start] --> B[Choose shared API strategy]
        B --> C[Add/use declaration for StartFileDragDrop]
        C --> D[Implement StartFileDragDrop in DragDrop_mac.mm]
        D --> E[Use AppKit: NSPasteboard + drag session]
        E --> F[Convert UTF-8 path to NSURL / NSString]
        F --> G[Reject directories; single file only]
        G --> H[Update ResultsTable: include + call on __APPLE__]
        H --> I[Add DragDrop_mac.mm to CMake macOS target]
        I --> J[Update docs: Windows and macOS]
        J --> K[Build macOS app and test drag to Finder]
        K --> L[End]
    ```

## The Process / Workflow

1. **Shared declaration**
   - Either add a small platform-agnostic header (e.g. `src/platform/FileDragDrop.h`) declaring `namespace drag_drop { bool StartFileDragDrop(const std::string& full_path_utf8); }`, or keep the declaration in `platform/windows/DragDropUtils.h` and have macOS include that header for the declaration only (see FileOperations.h pattern).
   - Ensure ResultsTable.cpp can include this declaration on both Windows and macOS.

2. **macOS implementation**
   - Create `src/platform/macos/DragDrop_mac.mm` (Objective-C++).
   - Implement `drag_drop::StartFileDragDrop(const std::string& full_path_utf8)`:
     - Validate path (non-empty, exists, is file not directory).
     - Convert UTF-8 path to NSString/NSURL.
     - Create pasteboard content (e.g. file URL type) and start a drag session (e.g. NSDraggingSource protocol or high-level AppKit drag API). Run on main thread only.
   - Use existing patterns from `FileOperations_mac.mm` (e.g. UTF-8 to NSString conversion, logging). Do not duplicate large blocks; extract or reuse helpers if needed.

3. **ResultsTable integration**
   - In `HandleDragAndDrop` in ResultsTable.cpp: extend the platform guard from `#ifdef _WIN32` to `#if defined(_WIN32) || defined(__APPLE__)` for the call to `drag_drop::StartFileDragDrop(drag_result.fullPath)`.
   - Include the drag-drop declaration on macOS (so the symbol is declared when building for macOS). Do not modify logic inside existing `#ifdef _WIN32` blocks to make them “cross-platform”; only add an `#elif defined(__APPLE__)` or a combined guard and include as above.

4. **CMake**
   - Add the new macOS source file (e.g. `src/platform/macos/DragDrop_mac.mm`) to the macOS app target in CMakeLists.txt, in the same section as other `platform/macos/*.mm` sources. Do not add it to Windows or Linux targets.

5. **Documentation**
   - Update ResultsTable.h (and any UI text such as Keyboard Shortcuts popup) to state that drag-and-drop from filename/path columns is supported on Windows and macOS (not only Windows).

6. **Verification**
   - Build the macOS app (e.g. `scripts/build_tests_macos.sh` if that builds the app, or the project’s standard macOS build). Manually test: run app, run a search, drag a file row and drop onto Finder or Desktop; confirm the file is copied/dropped as expected. Confirm Windows build and behavior unchanged.

## Anticipated Pitfalls

- Running drag session off the main thread: AppKit drag must run on the main thread; the call site is already on the UI thread—keep it that way.
- Path encoding: use UTF-8 consistently; convert to NSString/NSURL using the same pattern as FileOperations_mac.mm.
- Over-scoping: implement single-file drag only; reject directories; do not add multi-file or new Windows behavior.
- Modifying Windows code: do not change `#ifdef _WIN32` block contents to “fix” cross-platform; only add macOS branch or shared declaration and macOS impl.
- CMake: add the new .mm file only to the macOS target; avoid breaking Windows or Linux builds.

## Acceptance Criteria / Verification Steps

- [ ] `drag_drop::StartFileDragDrop` is declared in a header that ResultsTable can include on both Windows and macOS.
- [ ] `DragDrop_mac.mm` implements `drag_drop::StartFileDragDrop` using AppKit; single file only; directories rejected; main thread only.
- [ ] ResultsTable.cpp calls `StartFileDragDrop` when the drag gesture is confirmed and `defined(_WIN32) || defined(__APPLE__)`.
- [ ] CMakeLists.txt includes the new macOS drag-drop source only in the macOS app target.
- [ ] macOS app builds; dragging a file from the results table and dropping onto Finder/Desktop works.
- [ ] Windows build and behavior unchanged; no new Sonar/clang-tidy violations; AGENTS document and strict constraints followed.
- [ ] Docs (e.g. ResultsTable.h, shortcuts popup) updated to “Windows and macOS”.

## Strict Constraints / Rules to Follow

- Do not introduce new SonarQube or clang-tidy violations; fix underlying issues instead of suppressing. Apply preferred style (in-class initializers, const refs, no `} if (` on one line) per AGENTS document and docs/prompts/AGENT_STRICT_CONSTRAINTS.md.
- Do not modify code inside `#ifdef _WIN32`, `#ifdef __APPLE__`, or `#ifdef __linux__` to make it “cross-platform” (e.g. do not change Windows backslashes or logic). Add macOS implementation in new `__APPLE__` code or shared declaration + macOS file only.
- Keep all `#include` directives in the top block of the file; use lowercase `<windows.h>` where applicable.
- Do not run build/compile unless the task requires it; use project scripts (e.g. scripts/build_tests_macos.sh on macOS) when verifying.
- Follow AGENTS document: Windows rules for code that compiles on Windows; platform-specific blocks unchanged; C++17; naming conventions; DRY (reuse FileOperations_mac helpers where appropriate).

## Context and Reference Files

- **Plan:** docs/plans/2026-02-01_MACOS_DRAG_AND_DROP_PLAN.md
- **Strict constraints:** docs/prompts/AGENT_STRICT_CONSTRAINTS.md
- **Project rules:** AGENTS document
- **Call site and gesture:** src/ui/ResultsTable.cpp (HandleDragAndDrop, drag_candidate_*, StartFileDragDrop call under #ifdef _WIN32)
- **Windows API:** src/platform/windows/DragDropUtils.h, src/platform/windows/DragDropUtils.cpp
- **macOS pattern:** src/platform/macos/FileOperations_mac.mm (namespace, UTF-8 conversion, AppKit usage, shared header include)
- **CMake macOS sources:** CMakeLists.txt (section that lists platform/macos/*.mm for the main app)

Proceed with the task.
```
