---
description: C++ naming conventions — PascalCase types, snake_case_ members, kPascalCase constants
globs: "**/*.cpp,**/*.h,**/*.hpp"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
---

# Naming conventions (C++)

Follow **`docs/standards/CXX17_NAMING_CONVENTIONS.md`**. Before submitting, verify all identifiers match.

## Quick reference

| Kind | Convention | Example |
|------|------------|---------|
| Classes / structs / enums | `PascalCase` | `FileIndex`, `SearchResult` |
| Functions / methods | `PascalCase` | `GetSize()`, `StartMonitoring()` |
| Local variables / parameters | `snake_case` | `buffer_size`, `offset` |
| Member variables | `snake_case_` (trailing underscore) | `file_index_`, `mutex_` |
| Global variables | `g_snake_case` | `g_file_index` |
| Constants / constexpr | `kPascalCase` | `kBufferSize`, `kMaxQueueSize` |
| Namespaces | `snake_case` | `find_helper`, `file_operations` |
| Macros | `UPPER_SNAKE_CASE` | `NOMINMAX`, `APP_VERSION` |

**Action:** Before committing, check that new and touched identifiers follow these rules. Full details and examples: `docs/standards/CXX17_NAMING_CONVENTIONS.md`.

## Ubiquitous language (USN/MFT)

For USN/MFT code, `docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md` is the source of
truth for domain terms (`IndexedFile`, `NtfsFileReference`, `JournalCursor`,
`UsnRecord`, `UsnReason`, `UnresolvedReference`, …). Use those terms in new
identifiers and comments; do not introduce synonyms — extend the glossary instead.

## Documented exceptions

- **`GuiState` aggregate:** public members use camelCase (UI aggregate exception; see the
  class comment in `src/gui/GuiState.h`). This exception applies **only to the fields
  directly declared inside `class GuiState`** (e.g. `timeFilter`, `lastSortColumn`).
- **`GuiState` substate structs** (`SelectionState`, `ExportWorkflowState`, `GeminiWorkflowState`,
  `IndexBuildProgressState`, `HistoryInteractionState`) and every other struct used outside
  `GuiState` itself: **must use plain snake_case public members** (e.g. `error_message`,
  `pending_rename_id`). clang-tidy enforces this via `readability-identifier-naming`.
- A few GuiState-internal members keep `snake_case_` with trailing underscore by design
  (`async_sort_`, `gemini.api_future` group members were migrated to snake_case).
- Preprocessor-literal mapping (sentinel helpers), `dired`-style helpers keep
  `NOLINT(readability-identifier-naming)` where the API name is load-bearing
  (e.g. `strcpy_safe`/`strcat_safe` in `utils/StringUtils.h`; ImGui-like endpoint method names).
