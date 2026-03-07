# Tech Debt Review - 2026-01-01

## Executive Summary
- **Health Score**: 6/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 5
- **Estimated Remediation Effort**: 12 hours

## Findings

### High
**1. "God Class" in `FileIndex.h`**
- **Code:** `FileIndex.h`
- **Debt type:** Maintainability Issues
- **Risk explanation:** The `FileIndex` class has too many responsibilities, including data storage, indexing, searching, path management, and thread pool management. This makes the class difficult to maintain, test, and understand. Changes to one area of functionality have a high risk of impacting other, unrelated areas.
- **Suggested fix:** Refactor `FileIndex` into smaller, more focused classes. For example, create a `SearchOrchestrator` class to handle the search logic, a `PathManager` class to handle path storage and manipulation, and a `ThreadPoolManager` for the thread pool.
- **Severity:** High
- **Effort:** 8 hours

**2. Inefficient `GetAllEntries()` Implementation**
- **Code:** `FileIndex.h`, line 240
  ```cpp
  std::vector<std::tuple<uint64_t, std::string, bool, uint64_t>>
  GetAllEntries() const {
    ScopedTimer timer("FileIndex::GetAllEntries");

    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::tuple<uint64_t, std::string, bool, uint64_t>> entries;
    entries.reserve(storage_.Size());

    for (const auto &[id, entry] : storage_) {
      std::string_view path_view = GetPathViewLocked(id);
      entries.push_back(
          {id, std::string(path_view), entry.isDirectory, entry.fileSize.value});
    }

    return entries;
  }
  ```
- **Debt type:** Memory & Performance Debt
- **Risk explanation:** This method allocates a large vector and creates a `std::string` for every entry in the index. For a large index (e.g., 1 million files), this can consume a significant amount of memory and cause performance issues due to repeated memory allocations.
- **Suggested fix:** Replace this method with a callback-based approach that processes entries one at a time, avoiding the need to store them all in memory at once.
  ```cpp
  template <typename F>
  void ForEachEntryWithDetails(F&& callback) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& [id, entry] : storage_) {
      std::string_view path_view = GetPathViewLocked(id);
      callback(id, path_view, entry.isDirectory, entry.fileSize.value);
    }
  }
  ```
- **Severity:** High
- **Effort:** 2 hours

### Medium
**1. Unused Forward Declarations**
- **Code:** `FileIndex.h`, lines 58-61
  ```cpp
  class SearchThreadPool;
  class StaticChunkingStrategy;
  class HybridStrategy;
  class DynamicStrategy;
  class InterleavedChunkingStrategy;
  ```
- **Debt type:** Dead/Unused Code
- **Risk explanation:** These forward declarations are not used in the header file, adding unnecessary clutter and potentially confusing developers.
- **Suggested fix:** Remove the unused forward declarations.
- **Severity:** Medium
- **Effort:** 5 minutes

**2. Redundant `BuildFullPath` Method**
- **Code:** `FileIndex.h`, line 603
  ```cpp
  std::string BuildFullPath(uint64_t parent_id, std::string_view name) const {
    return PathBuilder::BuildFullPath(parent_id, name, storage_);
  }
  ```
- **Debt type:** Dead/Unused Code
- **Risk explanation:** The comment above this method states it is "Kept for backward compatibility". If no external code is using it, it is dead code. This increases maintenance overhead.
- **Suggested fix:** Search the codebase for usages of this method. If none are found, remove it.
- **Severity:** Medium
- **Effort:** 30 minutes

### Low
**1. Missing `[[nodiscard]]` Attribute**
- **Code:** `FileIndex.h`, line 113
  ```cpp
  [[nodiscard]] bool Exists(uint64_t id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return storage_.GetEntry(id) != nullptr;
  }
  ```
- **Debt type:** C++17 Modernization Opportunities
- **Risk explanation:** The `Exists` method returns a boolean value that should be checked by the caller. Without `[[nodiscard]]`, the compiler will not warn if the return value is ignored, which could lead to subtle bugs.
- **Suggested fix:** Add the `[[nodiscard]]` attribute to the `Exists` method and other methods that return values that should not be ignored.
- **Severity:** Low
- **Effort:** 15 minutes

## Quick Wins
1. **Remove unused forward declarations in `FileIndex.h`**.
2. **Add `[[nodiscard]]` to the `Exists` method**.
3. **Investigate and remove the redundant `BuildFullPath` method if it's not used**.

## Recommended Actions
1. **Prioritize refactoring the `FileIndex` class to improve maintainability.**
2. **Implement a callback-based alternative to `GetAllEntries()` to reduce memory consumption.**
3. **Address the "Quick Wins" to immediately improve code quality.**
