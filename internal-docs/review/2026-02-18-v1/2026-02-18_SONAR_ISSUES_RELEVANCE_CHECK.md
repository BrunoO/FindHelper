# Sonar issues relevance check (2026-02-18)

Checked the 8 open Sonar issues from `./sonar-results/sonarqube_issues.json` against the current codebase.

## Summary

| # | File:Line | Rule | Still relevant? | Notes |
|---|-----------|------|-----------------|-------|
| 1 | PathStorage.cpp:93 | cpp:S6004 | **Yes** | `path_len` is only used in the following `if`; can use init-statement. |
| 2 | ResultsTable.cpp:954 | cpp:S6004 | **Yes** | `want_text_input` is only used in the following `if`; can use init-statement. |
| 3 | ResultsTable.cpp:1163 | cpp:S6004 | **Yes** | `kResultColumnCount` is only used in the following `if (ImGui::BeginTable(...))`; can use init-statement. |
| 4 | ResultsTable.cpp:757 | cpp:S134 | **Yes** | Nesting: `BeginPopupModal` → `if (Button)` → `for` → `if (!path.empty() && ...)` = 4 levels. |
| 5 | ResultsTable.cpp:945 | cpp:S3776 | **Yes** | Function has NOLINT for clang-tidy only; Sonar still reports complexity 61 (needs refactor or NOSONAR). |
| 6 | ResultsTable.cpp:324 | cpp:S3776 | **Yes** | Same: NOLINT for clang-tidy; Sonar still reports complexity 50. |
| 7 | ResultsTable.cpp:1013 | cpp:S1066 | **Yes** | Nested `if (state.selectedRow > 0)` inside `else if (UpArrow \|\| P)` can be merged into one condition. |
| 8 | ResultsTable.cpp:1019 | cpp:S125 | **Unclear** | At current line 1019 there is only the comment `// Row-specific shortcuts ('m', 'u', 't')`. No commented-out code visible; may be resolved or Sonar may flag section comments. |

## Details

### S6004 (init-statement) – 3 issues

- **PathStorage.cpp:92–93**  
  `const size_t path_len = std::strlen(path);` is only used in `if (path_len < old_len)`. Safe to write:  
  `if (size_t path_len = std::strlen(path); path_len < old_len) { continue; }`

- **ResultsTable.cpp:951–954**  
  `const bool want_text_input = io.WantTextInput;` is only used in `if (!window_focused || want_text_input)`. Can use init-statement in that `if`.

- **ResultsTable.cpp:1162–1163**  
  `constexpr int kResultColumnCount = 8;` is only used in `ImGui::BeginTable("SearchResults", kResultColumnCount, ...)`. Can use init-statement:  
  `if (constexpr int kResultColumnCount = 8; ImGui::BeginTable("SearchResults", kResultColumnCount, ...))`  
  (C++17 allows this form.)

### S134 (nesting) – ResultsTable.cpp:757

- Nesting levels: 1) `BeginPopupModal`, 2) `if (ImGui::Button("Delete All"))`, 3) `for (file_id : state.markedFileIds)`, 4) `if (!path.empty() && DeleteFileToRecycleBin(...))`.  
- Fix: extract the inner loop body (or the whole “delete all” block) into a helper to reduce depth.

### S3776 (cognitive complexity) – ResultsTable.cpp:324, 945

- Both functions already have `NOLINTNEXTLINE(readability-function-cognitive-complexity)` (clang-tidy). Sonar does not honor that; it still reports cpp:S3776.
- Options: refactor to reduce complexity below 25, or add `// NOSONAR(cpp:S3776)` with a short rationale if the current structure is intentional.

### S1066 (merge nested if) – ResultsTable.cpp:1012–1016

- Current pattern:  
  `} else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_P)) {`  
  `  if (state.selectedRow > 0) { ... }`  
- Can be merged to:  
  `} else if ((ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_P)) && state.selectedRow > 0) { ... }`

### S125 (commented-out code) – ResultsTable.cpp:1019

- At the current line 1019 the only content is the documentation comment `// Row-specific shortcuts ('m', 'u', 't')`. No executable code is commented out there.
- Either the issue referred to an older version (since fixed) or Sonar is flagging a section comment; S125 is usually for commented-out code. Re-run Sonar to confirm if it still appears.

---

**Conclusion:** 7 of 8 issues are still relevant and can be addressed as above. The 8th (S125 at 1019) is unclear; re-scan with Sonar to confirm.
