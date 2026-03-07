# Tech Debt Review - 2026-01-11

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 4
- **Estimated Remediation Effort**: 4 hours

## Findings

### High
- **God Class/Method (`Application::Run`, `Application::ProcessFrame`)**
  - **Debt type**: Maintainability Issues
  - **Risk explanation**: The `Run` and `ProcessFrame` methods in `Application.cpp` are overly complex and long, handling too many responsibilities (main loop, power-saving, idle detection, UI rendering, application logic). This makes the code difficult to understand, test, and maintain.
  - **Suggested fix**:
    - Refactor the power-saving and idle-detection logic from `Run` into a separate `PowerManager` class.
    - Extract the UI rendering logic from `ProcessFrame` into a dedicated `UIRenderer` class.
    - Decompose the remaining logic in `ProcessFrame` into smaller, more focused helper methods.
  - **Severity**: High
  - **Effort**: 3 hours

- **Unused UI Features (`show_settings_`, `show_metrics_`)**
  - **Debt type**: Dead/Unused Code
  - **Risk explanation**: The `show_settings_` and `show_metrics_` booleans are controlled by the `Application` class, but there are no UI elements (like menu items or buttons) to toggle them. This means the settings and metrics windows are unreachable, making the related code dead.
  - **Suggested fix**: Add menu items or buttons to the main UI to allow users to open the settings and metrics windows. For example, in the main menu bar:
    ```cpp
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Settings", nullptr, &show_settings_);
        ImGui::MenuItem("Metrics", nullptr, &show_metrics_);
        ImGui::EndMenu();
    }
    ```
  - **Severity**: High
  - **Effort**: 0.5 hours

### Medium
- **Unused Public Method (`Application::GetIndexBuilder`)**
  - **Debt type**: Dead/Unused Code
  - **Risk explanation**: The `Application::GetIndexBuilder()` method is public but is never called from outside the class. This suggests it's either dead code or was intended for a feature that was never implemented.
  - **Suggested fix**: If the method is not needed, remove it. If it is intended for future use, mark it as such with a comment.
  - **Severity**: Medium
  - **Effort**: 0.25 hours

### Low
- **Raw Pointer Return (`Application::GetIndexBuilder`)**
  - **Debt type**: C++17 Modernization Opportunities
  - **Risk explanation**: The `GetIndexBuilder` method returns a raw pointer, which can be `nullptr`. This forces callers to perform null checks. Using `std::optional` would make the interface more expressive and safer.
  - **Suggested fix**: Change the return type to `std::optional<IIndexBuilder*>` to clearly indicate that the index builder may not be present.
    ```cpp
    // In Application.h
    [[nodiscard]] std::optional<IIndexBuilder*> GetIndexBuilder() const;
    ```
  - **Severity**: Low
  - **Effort**: 0.25 hours

## Quick Wins
1.  **Remove `Application::GetIndexBuilder`**: Since this public method is unused, it can be safely removed to clean up the public API.
2.  **Add UI toggles for settings and metrics windows**: This would re-enable dormant features with a few lines of ImGui code.

## Recommended Actions
1.  **Refactor `Application::Run` and `Application::ProcessFrame`**: This is the highest priority item. Breaking down these "god methods" will significantly improve the maintainability of the core application logic.
2.  **Re-enable settings and metrics windows**: This is a high-impact fix that restores functionality to the application.
3.  **Address the remaining findings**: The other issues should be addressed as part of routine code cleanup.
