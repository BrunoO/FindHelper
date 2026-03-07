# 2026-02-20 Review of no-verify Tags – Remaining Warnings to Fix

**Purpose**: Identify files and warnings from commits tagged or committed with `--no-verify` so they can be fixed and future commits can pass pre-commit clang-tidy.

---

## Tag `no-verify` (single tag in repo)

- **Commit**: `6c96ea2` — "Rules compliance: float F suffix, explicit lambda, #endif style"
- **Files in that commit** (C/C++ only):
  - `src/core/Application.cpp`
  - `src/core/CommandLineArgs.cpp`
  - `src/gui/ImGuiUtils.h`
  - `src/platform/linux/OpenGLManager.cpp`
  - `src/platform/windows/DirectXManager.cpp`
  - `src/search/SearchTypes.h`
  - `src/ui/ResultsTable.cpp`
  - `src/ui/SearchHelpWindow.cpp`
  - `src/ui/SearchInputs.h`

The tag was applied to mark that this commit was made with `git commit --no-verify` due to pre-commit clang-tidy issues. The conversation summary also mentioned **FilterPanel** as having pre-commit warnings that led to a later no-verify commit (Help window work).

---

## Other commits with `[no-verify]` in message (for context)

| Commit   | Message |
|----------|--------|
| `cedd08c` | ui(EmptyState): transient quick-start panel, save space [no-verify] |
| `f5f16a2` | Refine recent search recording and search worker behavior [no-verify] |
| `27079d0` | Refine recent search recording and UI polish [no-verify] |
| `6acd9b9` | Split panel UI for recent/example searches; fix recent search recording [no-verify] |
| `430446b` | fix: directory rename/move, path trailing separators, markedFileIds iteration order [no-verify] |

These are not tagged but indicate additional files that may have had warnings at commit time.

---

## Warnings identified (clang-tidy run from `build/`)

**File: `src/ui/FilterPanel.cpp`** (confirmed; this file was cited as the reason for no-verify on the Help window commit)

| Line(s) | Check | Suggestion |
|--------|--------|------------|
| 196–198, 208, 216–217, 220 | `misc-const-correctness` | Declare variables `const` where they are not modified (e.g. `settings_width`, `mode_width`, `help_width`, `spacing`, `window_right_edge`, `right_margin`, `target_x`). |
| 209 | `readability-math-missing-parentheses` | Add parentheses to make operator precedence explicit (e.g. `spacing * 2` in the expression for `total_group_width`). |
| 250, 266 | `cppcoreguidelines-pro-type-vararg`, `hicpp-vararg` | ImGui API (`SetTooltip`, etc.) uses varargs; suppress with `// NOLINTNEXTLINE(...)` on the same line if project policy allows, or leave and accept no-verify for this file until ImGui is updated. |

**Platform / environment note**: On macOS, running clang-tidy on `Application.cpp` and some other files can hit `'atomic' file not found` (or similar) when the compile commands or sysroot do not match the current environment. Those files are not listed above as having confirmed **source-level** warnings; re-run clang-tidy on Windows or with a matching build to confirm.

---

## Recommended actions

1. **Fix `src/ui/FilterPanel.cpp`** (high priority)  
   - Add `const` to the float variables that are never modified.  
   - Add parentheses for the expression involving `spacing` (line 209).  
   - For `SetTooltip` / vararg calls: add `// NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)` on the same line (match existing ImGui NOLINT style in the codebase).

2. **Re-run clang-tidy on no-verify commit files** (when build env is correct)  
   - From project root: `scripts/run_clang_tidy.sh --output no-verify-files-report.txt` (optionally restrict to the file list above).  
   - Or from `build/`: `clang-tidy -p . ../src/path/to/file.cpp` for each `.cpp` in the list.  
   - Fix any remaining warnings in `src/` (excluding external code).

3. **Optional: move or update tag**  
   - After fixing FilterPanel (and any other files), the next commit can be made without `--no-verify`.  
   - Optionally move the `no-verify` tag to the last commit that was intentionally no-verify for tracking:  
     `git tag -f no-verify <commit>` and `git push origin -f no-verify` (only if you want the tag to point to that commit).

---

## References

- Pre-commit hook: `scripts/pre-commit-clang-tidy.sh`
- Full clang-tidy run: `scripts/run_clang_tidy.sh`
- Init-statement filter: `scripts/filter_clang_tidy_init_statements.py`
- AGENTS.md: "When to Apply" for const correctness, NOLINT for ImGui varargs
