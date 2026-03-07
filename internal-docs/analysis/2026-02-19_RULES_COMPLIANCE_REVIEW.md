# Rules Compliance Review

**Date:** 2026-02-19  
**Scope:** Project rules from AGENTS.md and user rules; project source under `src/` and `tests/` (excluding `external/`).

## Summary

| Category | Status | Notes |
|----------|--------|------|
| `#endif` comments | ✅ | No bare `#endif` in `src/` |
| Class/struct forward declarations | ✅ | Script reports no mismatches |
| `(std::min)` / `(std::max)` | ✅ | Only use in `src/` is `(std::min)` in EmptyState.cpp |
| `lock_guard` vs `scoped_lock` | ✅ | No `lock_guard<std::mutex>` in `src/` |
| `} if (` (S3972) | ✅ | No occurrences |
| Popup context / SetNextWindowPos | ✅ | Save/Delete popups: OpenPopup and BeginPopupModal in same main window |
| C-style arrays | ✅ | Remaining uses have NOSONAR (API/embedded data) |
| `catch (...)` | ✅ | All either handle (log + return) or have NOSONAR with rationale |
| `reinterpret_cast` | ✅ | All documented with NOSONAR where required |
| `<windows.h>` lowercase | ✅ | Project code uses `<windows.h>` (external differs) |
| Unsafe C strings | ✅ | No raw `strncpy`/`strcpy` in `src/` |
| TODO / commented-out code | ✅ | No TODO or obvious commented-out code in `src/` |

---

## Violations and Recommendations

### 1. Float literals: use `F` suffix (user rule)

**Rule:** "float literals must be written with a 'F' not 'f' like in '3.14F'".

**Finding:** Several files use lowercase `f` (e.g. `0.0f`, `2.0f`, `5.0f`, `600.0f`).

**Locations (project code only):**

| File | Example lines |
|------|----------------|
| `src/ui/ResultsTable.cpp` | 74, 1187, 1189, 1214 |
| `src/gui/ImGuiUtils.h` | 21 |
| `src/core/Application.cpp` | 362 |
| `src/search/SearchTypes.h` | 118 |
| `src/ui/SearchHelpWindow.cpp` | 27–31 |
| `src/platform/windows/DirectXManager.cpp` | 35 |
| `src/platform/linux/OpenGLManager.cpp` | 29 |
| `src/ui/SearchInputs.h` | 35, 39 |

**Recommendation:** Use `F` for float literals (e.g. `0.0F`, `2.0F`) to satisfy the user rule and avoid clang-tidy warnings. ImGui/third-party APIs that take `float` remain unchanged; only literal suffixes are in scope.

---

### 2. Explicit lambda captures (AGENTS.md)

**Rule:** Prefer explicit capture lists; in template contexts implicit `[&]`/`[=]` can break MSVC.

**Finding:** One place uses implicit `[&]` in project code:

- **`src/core/CommandLineArgs.cpp`** (around line 112):  
  `if (const int value{[&]() { ... }()}; ...)`  
  The lambda only uses `arg_sv` and `name_len_with_equals`.

**Recommendation:** Use explicit captures, e.g. `[&arg_sv, name_len_with_equals]`, so the code matches AGENTS.md and stays safe under MSVC in template contexts.

**Note:** `src/utils/Logger.h` uses `[&]` inside a macro and already has a NOSONAR comment for that pattern; no change needed there.

---

### 3. `#endif` comment style (AGENTS.md)

**Rule:** Use two spaces before `//` in `#endif` comments (e.g. `#endif  // _WIN32`).

**Finding:** One inconsistent spacing:

- **`src/ui/ResultsTable.cpp`** (line 23):  
  `#endif                // _WIN32`  
  Uses many spaces instead of two.

**Recommendation:** Normalize to `#endif  // _WIN32`.

---

### 4. `const std::string&` vs `std::string_view` (optional)

**Rule:** Prefer `std::string_view` for read-only string parameters.

**Finding:** A few read-only parameters still use `const std::string&`:

- `src/core/IndexBuildState.h`: `SetLastErrorMessage(const std::string& message)` — already NOSONAR (message is stored).
- ~~`src/index/FileIndex.cpp`: `PathHash(const std::string& norm)`~~ — **Fixed:** now `PathHash(std::string_view norm)` using `std::hash<std::string_view>`.
- ~~`src/ui/EmptyState.cpp`: `ShowTooltipIfHovered(const std::string& text)`~~ — **Fixed:** now `ShowTooltipIfHovered(std::string_view text)` using `ImGui::TextUnformatted(text.data(), text.data() + text.size())`.
- `src/utils/StdRegexUtils.h`: lambda parameters `(const std::string& t, ...)` — may be constrained by `regex_match`/`regex_search` API.

**Recommendation:** Treat as low-priority cleanup: where the parameter is only read and the API allows, consider switching to `std::string_view` and document any that must stay as `std::string` (e.g. storage or API requirement).

---

### 5. Generic `catch (...)` documentation (optional)

**Rule:** Document when a generic `catch (...)` is necessary.

**Finding:** These handlers do meaningful work (log and return) but have no comment/NOSONAR:

- `src/core/Application.cpp` (~503): logs and returns 1.
- `src/platform/linux/main_linux.cpp` (~27): logs and returns 1.
- `src/platform/windows/main_win.cpp` (~27): logs and returns 1.

**Recommendation:** Add a short comment (e.g. "Catch-all for non-standard exceptions from RunApplication") or NOSONAR with rationale where Sonar flags them, so it’s clear the generic catch is intentional.

---

## Out of scope / no change

- **External code:** `external/` was not modified; bare `#endif`, `std::min`/`std::max` without parentheses, and float `f` in third-party code are left as-is.
- **Popup design:** SaveSearchPopup and DeleteSavedSearchPopup are opened from `FilterPanel::RenderSavedSearches` and rendered by `Popups::RenderSavedSearchPopups`, both in the same main window after `EndChild()`; no window-context or SetNextWindowPos issue found.
- **C-style arrays / reinterpret_cast / catch-all:** Remaining uses in `src/` are justified and already documented (NOSONAR or comments).

---

## Suggested fix order

1. **High:** Float literals `f` → `F` in project code (user rule + clang-tidy).
2. **High:** Explicit lambda capture in `CommandLineArgs.cpp` (AGENTS.md + MSVC safety).
3. **Low:** Normalize `#endif` spacing in `ResultsTable.cpp`.
4. **Optional:** Document or NOSONAR top-level `catch (...)` where needed; consider `string_view` for read-only string parameters where appropriate.
