# Test Strategy Audit - 2026-01-08

## Test Health Score: 4/10

**Justification**: The project has a decent foundation of unit tests for core utilities and data structures. However, there are significant gaps in coverage for critical application logic, UI components, and platform-specific code. The lack of integration tests means that the interactions between components are not being verified.

## Critical Gaps

### 1. No Tests for Core Application Logic
- **Component**: `src/core/Application.cpp`, `src/core/ApplicationLogic.cpp`
- **Issue Type**: Coverage Gaps
- **Gap Description**: There are no tests for the main application class or the core application logic. This means that the main event loop, window management, and the overall application lifecycle are not tested.
- **Risk**: High risk of regressions in core application behavior.
- **Suggested Action**: Create a suite of integration tests for the `Application` class. This will likely require mocking the windowing system and renderer to run in a headless environment.
- **Priority**: Critical
- **Effort**: L

### 2. No Tests for UI Components
- **Component**: `src/ui/`
- **Issue Type**: Coverage Gaps
- **Gap Description**: None of the UI components (e.g., `FilterPanel`, `ResultsTable`, `SearchControls`) have any tests.
- **Risk**: High risk of UI regressions and bugs in user-facing features.
- **Suggested Action**: Implement a suite of UI tests using a framework like Dear ImGui's testing tools or a similar solution. These tests should verify that the UI components are rendered correctly and that they respond to user input as expected.
- **Priority**: High
- **Effort**: L

### 3. Incomplete Coverage for Platform-Specific Code
- **Component**: `src/platform/`
- **Issue Type**: Coverage Gaps
- **Gap Description**: While some platform-specific utilities are tested, there is no coverage for the core platform-specific application entry points and bootstrap code (e.g., `main_win.cpp`, `AppBootstrap_win.cpp`).
- **Risk**: High risk of platform-specific bugs that are not caught by the generic tests.
- **Suggested Action**: Create a set of integration tests for each platform that verifies the application can be initialized and shut down correctly.
- **Priority**: High
- **Effort**: M

## Recommended Testing Strategy

- **Unit Tests**: Continue to write unit tests for all new core utilities and data structures. Aim for high line and branch coverage in these components.
- **Integration Tests**: Introduce a suite of integration tests that verify the interactions between the major components of the application (e.g., `Application`, `FileIndex`, `SearchWorker`).
- **UI Tests**: Implement a UI testing strategy to ensure that the user-facing parts of the application are working correctly.
- **End-to-End Tests**: Create a small suite of end-to-end tests that run the full application and verify its behavior from a user's perspective.

## Infrastructure Improvements

- **CI/CD**: The project should have a CI/CD pipeline that automatically builds the code, runs all the tests, and generates a code coverage report for every commit.
- **Sanitizers**: The CI/CD pipeline should run the tests with the Address Sanitizer (ASan) and Thread Sanitizer (TSan) enabled to catch memory errors and data races.
