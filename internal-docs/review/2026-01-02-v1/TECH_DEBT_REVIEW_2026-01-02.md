# Tech Debt Review - 2026-01-02

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 8
- **Estimated Remediation Effort**: 4-6 hours

## Findings

### High

**1. Duplicated Logic in `RenderFilterBadge` Callbacks**
- **File**: `ui/FilterPanel.cpp`
- **Debt Type**: Duplicated Logic
- **Risk Explanation**: The lambda functions passed to `RenderFilterBadge` for clearing filters are nearly identical, each calling `state.MarkInputChanged()`. This duplication makes it harder to maintain and introduces a risk of inconsistent behavior if one callback is updated and others are not.
- **Suggested Fix**: Create a helper function that takes the filter-clearing action as a parameter and wraps it with the call to `MarkInputChanged`.

```cpp
// Before
RenderFilterBadge("Extensions", true, [&]() {
  state.extensionInput.SetValue(kDefaultExtensions);
  mark_input_changed();
});
RenderFilterBadge("Filename", true, [&]() {
  state.filenameInput.Clear();
  mark_input_changed();
});

// After
auto clear_and_update = [&](auto clear_action) {
  return [&, clear_action]() {
    clear_action();
    mark_input_changed();
  };
};
RenderFilterBadge("Extensions", true, clear_and_update([&]() { state.extensionInput.SetValue(kDefaultExtensions); }));
RenderFilterBadge("Filename", true, clear_and_update([&]() { state.filenameInput.Clear(); }));
```
- **Severity**: High
- **Effort**: 15-20 minutes

**2. "God Class" - `Application` orchestrates too much**
- **File**: `Application.cpp`
- **Debt Type**: God Class
- **Risk Explanation**: The `Application` class has too many responsibilities, including managing the main loop, handling UI rendering, processing application logic, and managing component lifecycles. This makes the class difficult to test, maintain, and reason about.
- **Suggested Fix**: Refactor the `Application` class to delegate more responsibilities to smaller, more focused classes. For example, create a `UIManager` class to handle all UI rendering and a `LogicManager` to handle application logic updates.
- **Severity**: High
- **Effort**: 2-3 hours

### Medium

**3. Unnecessary `std::snprintf` for static labels**
- **File**: `ui/FilterPanel.cpp`
- **Debt Type**: Performance Debt
- **Risk Explanation**: Using `std::snprintf` to create labels that do not change at runtime is inefficient. This adds unnecessary overhead to the rendering loop.
- **Suggested Fix**: Use static `const char*` labels for the filter badges.

```cpp
// Before
char label[256];
std::snprintf(label, IM_ARRAYSIZE(label), "Last Modified: %s", time_filter_labels[filter_index]);
RenderFilterBadge(label, true, ...);

// After
const char* label = time_filter_labels[filter_index];
RenderFilterBadge(label, true, ...);
```
- **Severity**: Medium
- **Effort**: 5 minutes

**4. `RenderInputFieldWithEnter` is too complex**
- **File**: `ui/SearchInputs.cpp`
- **Debt Type**: Maintainability Issues
- **Risk Explanation**: The `RenderInputFieldWithEnter` function has too many parameters and a high cyclomatic complexity. This makes it difficult to understand and modify.
- **Suggested Fix**: Refactor the function to use a builder pattern or a struct to pass parameters. This will make the function signature cleaner and easier to read.
- **Severity**: Medium
- **Effort**: 30-45 minutes

**5. Redundant `Is...FilterActive` functions**
- **File**: `ui/FilterPanel.cpp`
- **Debt Type**: Duplicated Logic
- **Risk Explanation**: The `IsExtensionFilterActive`, `IsFilenameFilterActive`, and `IsPathFilterActive` functions are very similar and could be consolidated into a single, more generic function.
- **Suggested Fix**: Create a single function that takes a `SearchInputField` as a parameter and returns whether it is active.
- **Severity**: Medium
- **Effort**: 10 minutes

### Low

**6. Missing `[[nodiscard]]` on functions returning a value**
- **File**: `ui/FilterPanel.h`
- **Debt Type**: C++17 Modernization Opportunities
- **Risk Explanation**: The `Is...FilterActive` functions return a boolean value that should not be ignored. Adding `[[nodiscard]]` will cause the compiler to issue a warning if the return value is not used.
- **Suggested Fix**: Add the `[[nodiscard]]` attribute to the function declarations.
- **Severity**: Low
- **Effort**: 5 minutes

**7. Inconsistent use of `const`**
- **File**: `ui/FilterPanel.h`
- **Debt Type**: C++ Technical Debt
- **Risk Explanation**: Some functions that do not modify the `GuiState` object take it as a non-const reference. This is misleading and can lead to bugs.
- **Suggested Fix**: Add `const` to the `GuiState` parameter in functions where it is not modified.
- **Severity**: Low
- **Effort**: 5 minutes

**8. `Render` method in `SearchControls.cpp` is too long**
- **File**: `ui/SearchControls.cpp`
- **Debt Type**: Maintainability Issues
- **Risk Explanation**: The `Render` method in `SearchControls` is too long and has multiple responsibilities.
- **Suggested Fix**: Break the method down into smaller, more manageable functions, each with a single responsibility.
- **Severity**: Low
- **Effort**: 20 minutes

## Quick Wins
1.  **Use static labels instead of `snprintf`**: A simple and safe change that improves performance.
2.  **Add `[[nodiscard]]` to functions**: A quick way to improve code safety with no risk.
3.  **Add `const` to `GuiState` parameters**: Improves code correctness and readability.

## Recommended Actions
1.  **Refactor `Application` class**: This is the most important issue to address, as it will have the largest impact on the maintainability of the codebase.
2.  **Refactor `RenderFilterBadge` callbacks**: This will reduce code duplication and make the UI code easier to maintain.
3.  **Address the remaining medium and low priority issues**: These are smaller fixes that will still improve the overall quality of the code.
