# SonarQube Issues Analysis - January 10, 2026

## Summary

**Total new issues created today:** 32 issues across 11 different SonarQube rules.

**Key Finding:** Most issues (25/32) are from rules already documented in `AGENTS.md`, but the code wasn't following them. Two new rules need to be added to prevent future violations.

## Issue Breakdown by Rule

| Rule | Count | Status | Action Needed |
|------|-------|--------|---------------|
| `cpp:S3230` | 7 | **NEW RULE** | Add to AGENTS.md |
| `cpp:S134` | 8 | Already covered | Strengthen enforcement |
| `cpp:S1181` | 5 | Already covered | Strengthen enforcement |
| `cpp:S3776` | 3 | Already covered | Strengthen enforcement |
| `cpp:S3608` | 3 | Already covered | Strengthen enforcement |
| `cpp:S5421` | 1 | **NEW RULE** | Add to AGENTS.md |
| `cpp:S1066` | 1 | Already covered | Already documented |
| `cpp:S6004` | 1 | Already covered | Already documented |
| `cpp:S5008` | 1 | Already covered | Already documented |
| `cpp:S1135` | 1 | Already covered | Already documented |
| `cpp:S107` | 1 | Already covered | Already documented |

## Root Cause Analysis

### 1. New Rules Not Documented (8 issues)

#### `cpp:S3230` - Use In-Class Initializers (7 issues in `FolderBrowser.cpp`)

**Problem:** Constructor initializer lists are being used for members that should use in-class initializers.

**Example from `FolderBrowser.cpp`:**
```cpp
FolderBrowser::FolderBrowser(const char* title)
    : title_(title)
    , is_open_(false)        // ❌ Should use in-class initializer
    , has_selected_(false)  // ❌ Should use in-class initializer
    , selected_path_()       // ❌ Redundant, should use in-class initializer
    , current_path_()        // ❌ Redundant, should use in-class initializer
    , selected_index_(-1)    // ❌ Should use in-class initializer
{
}
```

**Why it appeared:** This is a C++11+ best practice that prefers in-class initialization for default values. SonarQube flags this to encourage modern C++ style.

**Solution:** Use in-class initializers in the header file:
```cpp
class FolderBrowser {
private:
  bool is_open_ = false;
  bool has_selected_ = false;
  std::string selected_path_{};
  int selected_index_ = -1;
  // ...
};
```

#### `cpp:S5421` - Global Variables Should Be Const (1 issue in `SettingsWindow.cpp`)

**Problem:** Global variables that aren't modified should be marked `const`.

**Example from `SettingsWindow.cpp:28`:**
```cpp
static std::unique_ptr<FolderBrowser> g_folder_browser = nullptr;  // ❌ Should be const
```

**Why it appeared:** SonarQube flags non-const global variables as they can be accidentally modified and make code harder to reason about.

**Note:** This might be a false positive if the variable needs to be modified (e.g., `std::make_unique` assignment). However, if the pointer itself doesn't change after initialization, consider using `const` or a different pattern.

### 2. Existing Rules Not Followed (24 issues)

#### `cpp:S3608` - Explicit Lambda Captures (3 issues in `StringSearch.h`)

**Problem:** Lambdas using `[&]` instead of explicit captures.

**Example from `StringSearch.h:171, 218, 251`:**
```cpp
if (string_search_detail::TryAVX2Path(text, pattern, [&]() {  // ❌ Should be explicit
  // ...
})) {
```

**Why it appeared:** Despite being documented in AGENTS.md, the code wasn't following the rule. The deduplication work in `StringSearch.h` introduced new lambdas with implicit captures.

**Action:** Need to update lambdas to use explicit captures like `[&text, &pattern]`.

#### `cpp:S1181` - Generic Exception Catching (5 issues)

**Problem:** Using `catch (const std::exception& e)` instead of more specific exceptions.

**Files affected:**
- `FolderBrowser.cpp:81, 92, 126` (3 issues)
- `SettingsWindow.cpp:271, 338` (2 issues)

**Why it appeared:** Filesystem operations can throw specific exceptions like `std::filesystem::filesystem_error`, but the code catches generic `std::exception`.

**Action:** Catch specific exceptions when possible:
```cpp
catch (const std::filesystem::filesystem_error& e) {
  // Handle filesystem errors
} catch (const std::exception& e) {
  // Handle other exceptions
}
```

#### `cpp:S134` - Nesting Depth (8 issues)

**Problem:** Code nesting exceeds 3 levels.

**Files affected:**
- `SettingsWindow.cpp` (5 issues)
- `FolderBrowser.cpp` (2 issues)
- `EmptyState.cpp` (1 issue)

**Why it appeared:** UI rendering code often has deeply nested ImGui calls. The rule is documented but not consistently applied.

**Action:** Refactor using early returns, guard clauses, or extracted functions.

#### `cpp:S3776` - Cognitive Complexity (3 issues)

**Problem:** Functions exceed cognitive complexity of 25.

**Files affected:**
- `SettingsWindow.cpp:30` - Complexity 141 (way over limit!)
- `FolderBrowser.cpp:131` - Complexity 52
- `EmptyState.cpp:240` - Complexity 26

**Why it appeared:** UI rendering functions tend to be complex. `SettingsWindow::Render()` is particularly complex at 141.

**Action:** Break down large rendering functions into smaller, focused functions.

#### Other Existing Rules (4 issues)

- `cpp:S1066` (1 issue) - Merge nested if statements
- `cpp:S6004` (1 issue) - Use init-statements
- `cpp:S5008` (1 issue) - Avoid void* pointers
- `cpp:S1135` (1 issue) - Complete or remove TODO
- `cpp:S107` (1 issue) - Too many parameters

These are already documented in AGENTS.md but weren't followed.

## Recommendations

### 1. Add New Rules to AGENTS.md

Add two new sections:

1. **Use In-Class Initializers (cpp:S3230)** - Prefer in-class initializers over constructor initializer lists for default values
2. **Global Variables Should Be Const (cpp:S5421)** - Mark global variables as `const` when they don't need to be modified

### 2. Strengthen Existing Rules

For rules that are already documented but not followed:

1. **Add examples** showing common violations in UI code
2. **Add pre-commit checklist** items for these rules
3. **Add code review guidelines** to catch these issues early

### 3. Code-Specific Actions

1. **Refactor `SettingsWindow::Render()`** - Break down the 141 complexity function
2. **Fix lambda captures in `StringSearch.h`** - Make all captures explicit
3. **Refactor `FolderBrowser` constructor** - Use in-class initializers
4. **Reduce nesting in UI code** - Extract helper functions for ImGui rendering

## Prevention Strategy

1. **Before committing:** Run a quick check for:
   - Implicit lambda captures (`[&]`, `[=]`)
   - Constructor initializer lists with default values
   - Non-const global variables
   - Deeply nested code (>3 levels)
   - Generic exception catches

2. **Code review focus:** Pay special attention to:
   - New UI rendering code (high nesting risk)
   - New lambdas (explicit capture requirement)
   - New constructors (in-class initializer preference)
   - New global variables (const requirement)

3. **Automated checks:** Consider adding clang-tidy rules or pre-commit hooks for:
   - `readability-identifier-naming` (already have)
   - `modernize-use-default-member-init` (for cpp:S3230)
   - `readability-avoid-const-params-in-decls` (might help)
