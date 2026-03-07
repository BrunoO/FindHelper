# 2026-02-18 Broader sweep: `const std::string&` → `std::string_view`

## Scope

Identify remaining read-only parameters that are good candidates for `std::string_view` (no storage, no null-termination requirement at API boundary). Exclude parameters that store the string or are part of a cross-platform API that must match another implementation.

---

## Summary table

| Location | Parameter(s) | Verdict | Notes |
|----------|--------------|---------|--------|
| **FileIndex** | `UpdatePrefixLocked(old_prefix, new_prefix)` | ✅ Convert | Delegates to `PathOperations::UpdatePrefix(string_view, string_view)`. |
| **ExportSearchResultsService** | `ExportToCsv(..., output_path)` | ✅ Convert | Only read; convert to `std::string` only for `std::ofstream` ctor. |
| **UsnMonitor.cpp** | `filename` in helpers | ✅ Convert | Read-only (compare, pass to index); call site has `std::string`. |
| **EmptyState.cpp** | `RenderRecentSearchButton(..., label, ...)` | ✅ Convert | Read-only; use temporary `std::string` for `ImGui::Button` if needed. |
| **StatusBar.cpp** | `RenderRightGroup(..., status_text, memory_text)` | ❌ Skip | Caller already builds `std::string`; ImGui needs null-terminated. Converting to string_view would require temp strings for every `Text`/`TextColored` call. |
| **ShellContextUtils.cpp** | `InvokeContextMenuCommand(..., path)` | ✅ Convert | Only used in log concatenation; build `std::string(path)` for logs. |
| **StdRegexUtils.h** | Lambda `(const std::string& t, ...)` | ⚠️ Optional | `regex_match`/`regex_search` need string; convert inside lambda from `string_view`. |
| **IndexBuildState.h** | `SetLastErrorMessage(message)` | ❌ Keep | Message is stored; NOSONAR documents this. |
| **PathStorage** | `InsertPath`, `UpdatePath`, `AppendString` | ❌ Keep | Parameters are stored or written to internal buffer. |
| **FileIndexStorage.h** | `Intern(const std::string &str)` | ❌ Keep | Stores/interns the string. |
| **FileOperations.h** (all platforms) | `DeleteFileToRecycleBin(full_path)` | ❌ Keep | Cross-platform API; NOSONAR on Linux says type must match. |
| **Logger.h** | `Log(..., message)`, `ScopedTimer(operation_name)` | ❌ Skip for now | May store or pass to C-style APIs; separate pass. |

---

## Implemented in this sweep

- FileIndex::UpdatePrefixLocked → `std::string_view`
- ExportSearchResultsService::ExportToCsv → `output_path` as `std::string_view`
- UsnMonitor.cpp: all `filename` parameters in helpers → `std::string_view`
- EmptyState.cpp: RenderRecentSearchButton `label` → `std::string_view` (local `label_str` for ImGui::Button)
- ShellContextUtils.cpp: InvokeContextMenuCommand `path` → `std::string_view` (build `std::string(path)` for log concatenation)
- StatusBar: not changed (see table)

## Not changed (rationale)

- **IndexBuildState, PathStorage, FileIndexStorage, FileOperations, Logger**: Store string or must match existing API; left as-is.
- **StdRegexUtils**: Lambdas could take `string_view` and convert once; low priority, left for a later pass.
