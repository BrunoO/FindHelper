# Architecture & Code Smells Review - 2026-01-08

## Executive Summary
- **Architecture Health Score**: 5/10
- **Top 3 Systemic Issues**:
  1.  **Single Responsibility Principle Violations**: Widespread use of "God Classes" that are difficult to maintain and test.
  2.  **Tight Coupling**: High-level modules are tightly coupled to low-level implementation details.
  3.  **Inconsistent Abstractions**: Platform-specific details leak into shared code, and abstractions are not consistently applied.
- **Recommended Refactorings**:
  - Refactor `FileIndex` and `PathPatternMatcher` to separate concerns.
  - Introduce interfaces for platform-specific functionality to reduce coupling.
- **Threading Model Assessment**: The current threading model is functional but fragile. The heavy reliance on a single `FileIndex` class for thread management creates a bottleneck and increases the risk of deadlocks.

## Findings

### High

**1. SOLID Principle Violation: Single Responsibility Principle (SRP) - `FileIndex`**
- **Location**: `src/index/FileIndex.h`, `src/index/FileIndex.cpp`
- **Smell Type**: SRP Violation (God Class)
- **Impact**: The `FileIndex` class is responsible for too many things, including data storage, indexing, searching, and thread management. This makes the class difficult to maintain, test, and reason about.
- **Refactoring Suggestion**: Decompose the `FileIndex` class into smaller, more focused classes, each with a single responsibility (e.g., `IndexStorage`, `SearchOrchestrator`, `IndexBuilder`).
- **Severity**: High
- **Effort**: L

**2. SOLID Principle Violation: Single Responsibility Principle (SRP) - `PathPatternMatcher`**
- **Location**: `src/path/PathPatternMatcher.h`, `src/path/PathPatternMatcher.cpp`
- **Smell Type**: SRP Violation (God Class)
- **Impact**: The `PathPatternMatcher` class is responsible for compiling patterns, managing the state machine, and performing the matching. This makes the class overly complex.
- **Refactoring Suggestion**: Split the class into a `PathPatternCompiler` and a `PathPatternStateMachine` to separate the concerns of compilation and matching.
- **Severity**: High
- **Effort**: M

### Medium

**1. Dependency Inversion Principle (DIP) Violation**
- **Location**: `src/core/Application.cpp`
- **Smell Type**: DIP Violation
- **Impact**: The `Application` class directly instantiates and manages the `FileIndex` class, creating a tight coupling between the high-level application logic and the low-level indexing implementation. This makes it difficult to replace the `FileIndex` with a different implementation or to test the `Application` class in isolation.
- **Refactoring Suggestion**: Introduce an `IFileIndex` interface and use dependency injection to provide the `FileIndex` to the `Application` class.
- **Severity**: Medium
- **Effort**: M

**2. Leaky Abstractions**
- **Location**: `src/platform/windows/FileOperations_win.cpp`
- **Smell Type**: Abstraction Failures (Leaky Abstractions)
- **Impact**: The Windows-specific implementation of file operations uses Windows-specific types like `HANDLE` and `WIN32_FIND_DATA`, which can leak into platform-agnostic code if not properly encapsulated.
- **Refactoring Suggestion**: Ensure that all platform-specific types are hidden behind a platform-agnostic interface and that no platform-specific details are exposed to the shared codebase.
- **Severity**: Medium
- **Effort**: S

### Low

**1. Missing Abstractions for Platform-Specific Code**
- **Location**: `src/platform/`
- **Smell Type**: Abstraction Failures (Missing Abstractions)
- **Impact**: There is duplicated logic across the platform-specific files (e.g., `StringUtils_win.cpp`, `StringUtils_mac.cpp`, `StringUtils_linux.cpp`). This makes it more difficult to maintain and can lead to inconsistencies between platforms.
- **Refactoring Suggestion**: Introduce a common interface for string utilities and other platform-specific functionality, and provide platform-specific implementations.
- **Severity**: Low
- **Effort**: M
