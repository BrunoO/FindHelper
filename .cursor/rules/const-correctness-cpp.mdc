---
description: Const correctness and read-only parameters (Sonar S995/S5350, clang-tidy)
globs: "**/*.cpp,**/*.h,**/*.hpp"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
---

# Const Correctness (C++)

Apply `const` consistently to avoid Sonar cpp:S995 / cpp:S5350 and clang-tidy `readability-non-const-parameter` warnings.

## 1. Member functions

Mark non-modifying member functions as `const`.

```cpp
// ❌ WRONG
size_t GetSize() { return files_.size(); }

// ✅ CORRECT
size_t GetSize() const { return files_.size(); }
```

## 2. Parameters (read-only)

Use `const T&` or `const T*` for parameters that are only read. Non-const references for read-only params are a frequent Sonar/clang-tidy issue.

```cpp
// ❌ WRONG - parameter only read
void ProcessData(SearchThreadPool& thread_pool) {
  size_t n = thread_pool.GetThreadCount();
}

// ✅ CORRECT
void ProcessData(const SearchThreadPool& thread_pool) {
  size_t n = thread_pool.GetThreadCount();
}
```

**Guidelines:** `const T&` (read-only), `T&` (modify in place), `const T*` (optional read-only), `T` by value (small types).

## 3. Local variables

Use `const` for locals that are not modified (also helps Sonar and misc-const-correctness).

## Enforcement

- **clang-tidy:** `readability-non-const-parameter` (and `misc-const-correctness` where enabled).
- **Sonar:** cpp:S995, cpp:S5350.
- Before committing: review all reference/pointer parameters and add `const` if read-only.
