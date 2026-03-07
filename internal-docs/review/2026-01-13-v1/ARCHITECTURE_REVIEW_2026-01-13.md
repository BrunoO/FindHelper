# Architecture Review - 2026-01-13

## Executive Summary
- **Health Score**: 5/10
- **Critical Issues**: 1
- **High Issues**: 2
- **Total Findings**: 8
- **Estimated Remediation Effort**: 30 hours

## Findings

### Critical
- **Lack of Clear Separation Between UI and Business Logic**
  - **Location**: `src/core/Application.cpp`, `src/ui/`
  - **Risk Explanation**: The `Application` class directly manipulates UI components and also contains core business logic for searching and indexing. This tight coupling makes it difficult to test the business logic independently of the UI, and UI changes can easily break core functionality.
  - **Suggested Fix**: Introduce a clear separation of concerns by implementing a Model-View-ViewModel (MVVM) or similar architectural pattern. The `Application` class should be responsible for orchestrating the different components, but the UI and business logic should be in separate, independent modules.
  - **Severity**: Critical
  - **Effort**: 20 hours

### High
- **"God Class" Components**
  - **Location**: `src/index/FileIndex.h`, `src/core/Application.h`
  - **Risk Explanation**: Several classes, most notably `FileIndex` and `Application`, have grown into "God Classes" with too many responsibilities. This leads to a high degree of coupling and makes the code difficult to understand, test, and maintain.
  - **Suggested Fix**: Decompose these classes into smaller, more focused components with single responsibilities. For example, `FileIndex` could be broken down into `FileIndexStorage`, `FileIndexSearcher`, and `FileIndexUpdater`.
  - **Severity**: High
  - **Effort**: 15 hours

- **Inconsistent Threading Model**
  - **Location**: `src/search/SearchWorker.cpp`, `src/usn/UsnMonitor.cpp`
  - **Risk Explanation**: The application uses a mix of different threading models, including a dedicated thread for the `SearchWorker`, a thread pool for some tasks, and raw `std::thread` objects in other places. This inconsistency makes it difficult to reason about the application's concurrency and can lead to race conditions and deadlocks.
  - **Suggested Fix**: Standardize on a single, well-defined threading model. A dedicated thread pool for all background tasks would be a good approach, with clear guidelines on how to submit work to the pool and how to handle synchronization.
  - **Severity**: High
  - **Effort**: 10 hours

### Medium
- **Global State and Singletons**
- **Header-Heavy Design Leading to Long Compile Times**
- **Lack of a Clear Plugin or Extension Architecture**

### Low
- **Inconsistent Use of Namespaces**
- **Some Circular Dependencies Between Components**

## Quick Wins
1.  **Introduce a `ViewModel` for a small part of the UI**: Start by refactoring a small, self-contained UI component to use a `ViewModel` to demonstrate the pattern and its benefits.
2.  **Break out a small piece of `FileIndex`**: Extract a single responsibility, such as filtering, into a separate class to begin the process of decomposing the "God Class".
3.  **Standardize on a single thread pool**: Create a global, shared thread pool and refactor one component to use it.

## Recommended Actions
1.  **Separate UI and Business Logic**: This is the most critical architectural issue and should be addressed first.
2.  **Decompose "God Classes"**: Break down the large, monolithic classes into smaller, more manageable components.
3.  **Establish a Consistent Threading Model**: This will improve the application's stability and make it easier to reason about concurrency.
