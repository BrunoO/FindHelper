# Test Strategy Audit - 2026-01-11

## Executive Summary
- **Health Score**: 3/10
- **Coverage Estimate**: ~40% line coverage
- **Critical Gaps**: `Application`, `ApplicationLogic`, `UsnMonitor`, and all platform-specific code.
- **Total Findings**: 4
- **Estimated Remediation Effort**: 20+ hours

## Findings

### Critical
- **No Tests for Core Application Logic**
  - **Component**: `Application`, `ApplicationLogic`
  - **Issue Type**: Coverage Analysis
  - **Gap Description**: There are no unit tests for the `Application` and `ApplicationLogic` classes, which orchestrate the entire application. This means the main application loop, UI state management, search controller logic, and keyboard shortcuts are completely untested.
  - **Risk**: High risk of regressions in core application behavior. Bugs in these classes could render the application unusable.
  - **Suggested Action**: Create a suite of tests for `ApplicationLogic` that uses a mock `SearchController` and `SearchWorker` to verify that UI events are handled correctly. For the `Application` class, create integration tests that verify the main loop and component lifecycle.
  - **Priority**: Critical
  - **Effort**: Large

### High
- **No Tests for `UsnMonitor`**
  - **Component**: `UsnMonitor`
  - **Issue Type**: Coverage Analysis
  - **Gap Description**: The `UsnMonitor`, a critical component for real-time indexing on Windows, has no unit or integration tests. This includes the multi-threaded producer-consumer logic, error handling, and privilege management.
  - **Risk**: High risk of race conditions, deadlocks, and silent failures in the file monitoring subsystem.
  - **Suggested Action**:
    1.  Create unit tests for the `UsnMonitor`'s state machine (start, stop, error states).
    2.  Develop integration tests that use a controlled environment (e.g., a temporary directory) to create, delete, and rename files, and verify that the `UsnMonitor` correctly updates the `FileIndex`.
    3.  Enable TSAN (Thread Sanitizer) for these tests to detect race conditions.
  - **Priority**: High
  - **Effort**: Large

- **No Platform-Specific Tests**
  - **Component**: All files in `src/platform/`
  - **Issue Type**: Coverage Analysis
  - **Gap Description**: There are no tests for any of the platform-specific code, including Windows-specific API wrappers and macOS/Linux stubs.
  - **Risk**: High risk of platform-specific regressions that would not be caught by the existing cross-platform tests.
  - **Suggested Action**: Create a separate test suite for each platform that includes integration tests for the platform-specific code. For example, the Windows tests should verify that `PrivilegeUtils` correctly manipulates process tokens.
  - **Priority**: High
  - **Effort**: Medium

### Medium
- **No UI Tests**
  - **Component**: All files in `src/ui/`
  - **Issue Type**: Missing Test Types
  - **Gap Description**: There are no tests for the UI components. While testing ImGui can be challenging, it is possible to write tests that verify the logic that determines which widgets are displayed and how they behave.
  - **Risk**: Regressions in UI behavior, such as incorrect filter logic or broken search controls.
  - **Suggested Action**: Implement a suite of UI tests that create a `GuiState` object, call the static rendering methods for each UI component, and then assert that the `GuiState` has been modified correctly based on simulated user input.
  - **Priority**: Medium
  - **Effort**: Large

## Test Health Score: 3/10
The current test suite provides a reasonable level of coverage for low-level utility functions and data structures. However, the complete lack of testing for the core application logic, the critical `UsnMonitor` component, and all platform-specific code represents a major gap in the quality assurance strategy.

## Critical Gaps
- The most critical gap is the absence of tests for the `Application` and `ApplicationLogic` classes. A bug in this area could easily break the entire application.
- The lack of tests for `UsnMonitor` is also a major concern, given its complexity and importance for the Windows version of the application.

## Recommended Testing Strategy
- **Unit Tests**: Focus on testing individual classes in isolation. Use mocks to isolate dependencies.
- **Integration Tests**: Add integration tests for components that interact with the file system or the operating system.
- **UI Tests**: Implement a suite of tests that verify the logic of the UI components.
- **End-to-End Tests**: (Future) Consider adding end-to-end tests that launch the application and interact with it through a UI automation framework.
