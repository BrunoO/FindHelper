# SonarQube Analysis: CpuFeatures.cpp

**Date:** 2026-01-15  
**File:** `CpuFeatures.cpp`  
**Total Issues:** 15  
**Status:** ⚠️ **NEEDS ATTENTION**

---

## Executive Summary

SonarQube has identified **15 code quality issues** in `CpuFeatures.cpp`:
- **CRITICAL:** 10 issues (67%)
- **MAJOR:** 3 issues (20%)
- **MINOR:** 1 issue (7%)
- **MEDIUM:** 1 issue (7%)

**Priority Actions:**
1. ❌ **CRITICAL:** Replace `malloc`/`free` with modern C++ memory management (3 issues)
2. ❌ **CRITICAL:** Fix global variable const-correctness (7 issues)
3. ⚠️ **CRITICAL:** Reduce deep nesting in `CheckHyperThreadingEnabled()` (1 issue)
4. ⚠️ **MAJOR:** Fix include path case sensitivity (1 issue)
5. ⚠️ **MAJOR:** Replace C-style array with `std::array` (1 issue)

---

## Issue Breakdown by Severity

### CRITICAL Issues (10)

#### 1. Global Variables Should Be Const (S5421) - 7 instances

**Lines:** 15, 16, 19, 20, 21, 22, 23  
**Rule:** `cpp:S5421`  
**Severity:** CRITICAL  
**Impact:** HIGH (Maintainability)  
**Effort:** 20min each (140min total)

**Problem:**
```cpp
// Lines 14-23
static std::atomic<bool> g_avx2_supported{false};
static std::atomic<bool> g_avx2_checked{false};
static std::atomic<bool> g_ht_enabled{false};
static std::atomic<bool> g_ht_checked{false};
static std::atomic<unsigned int> g_physical_cores{0};
static std::atomic<unsigned int> g_logical_cores{0};
static std::atomic<bool> g_core_counts_checked{false};
```

**Analysis:**
These are `std::atomic` variables used for thread-safe caching. SonarQube flags them because they're not `const`, but they **cannot** be `const` because they need to be modified at runtime (they're cache variables that get populated on first access).

**Recommendation:**
- **Option 1:** Suppress with `// NOSONAR` comment - These variables are intentionally non-const for caching purposes
- **Option 2:** Document the design decision in a comment explaining why they cannot be const
- **Option 3:** Consider using a different pattern (e.g., `std::call_once` with a function-local static), but this may be over-engineering

**Justification:**
These are thread-safe cache variables that must be mutable. Making them `const` would break functionality. This is a false positive from SonarQube.

---

#### 2. Remove Use of `malloc` (S1231)

**Line:** 182  
**Rule:** `cpp:S1231`  
**Severity:** CRITICAL  
**Impact:** HIGH (Maintainability)  
**Effort:** 10min

**Problem:**
```182:182:CpuFeatures.cpp
        (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(bufferSize);
```

**Current Code:**
```cpp
PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = 
    (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(bufferSize);
```

**Recommendation:**
Replace with `std::vector` or `std::unique_ptr` with custom deleter:

```cpp
// Option 1: std::vector (recommended)
std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(
    bufferSize / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));

if (GetLogicalProcessorInformation(buffer.data(), &bufferSize)) {
    // ... use buffer ...
    // No manual free needed - RAII handles it
}

// Option 2: std::unique_ptr with custom deleter
auto buffer = std::unique_ptr<SYSTEM_LOGICAL_PROCESSOR_INFORMATION[], 
                               decltype(&free)>(nullptr, free);
buffer.reset((PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(bufferSize));
```

**Priority:** **HIGH** - This is a real issue that should be fixed.

---

#### 3. Remove Use of `free` (S1231) - 2 instances

**Lines:** 222, 231  
**Rule:** `cpp:S1231`  
**Severity:** CRITICAL  
**Impact:** HIGH (Maintainability)  
**Effort:** 10min each (20min total)

**Problem:**
```222:222:CpuFeatures.cpp
        free(buffer);
```

```231:231:CpuFeatures.cpp
    free(buffer);
```

**Current Code:**
```cpp
free(buffer);  // Line 222 and 231
```

**Recommendation:**
If using `std::vector` (recommended for `malloc` fix), remove `free()` calls entirely. RAII will handle cleanup automatically.

**Priority:** **HIGH** - Fix together with `malloc` issue.

---

#### 4. Deep Nesting (S134)

**Line:** 210  
**Rule:** `cpp:S134`  
**Severity:** CRITICAL  
**Impact:** HIGH (Maintainability)  
**Effort:** 10min

**Problem:**
The `CheckHyperThreadingEnabled()` function has 4 levels of nesting (if statements).

**Nesting Flow:**
- Line 198: `if (GetLogicalProcessorInformation(buffer, &bufferSize))` (Level 1)
- Line 201: `for (DWORD i = 0; i < numEntries; ++i)` (Level 2)
- Line 202: `if (buffer[i].Relationship == RelationProcessorCore)` (Level 3)
- Line 210: `if (buffer[i].ProcessorCore.Flags != 0 || logicalCount > 1)` (Level 4)

**Recommendation:**
Extract nested logic into helper functions:

```cpp
static bool ProcessProcessorCore(
    const SYSTEM_LOGICAL_PROCESSOR_INFORMATION& info,
    unsigned int& physicalCores,
    unsigned int& logicalCores) {
    
    if (info.Relationship != RelationProcessorCore) {
        return false;
    }
    
    physicalCores++;
    unsigned int logicalCount = CountSetBits(info.ProcessorMask);
    logicalCores += logicalCount;
    
    return (info.ProcessorCore.Flags != 0 || logicalCount > 1);
}

static bool CheckHyperThreadingEnabled() {
    // ... buffer allocation ...
    
    bool hyperThreadingEnabled = false;
    unsigned int physicalCores = 0;
    unsigned int logicalCores = 0;
    
    if (GetLogicalProcessorInformation(buffer.data(), &bufferSize)) {
        DWORD numEntries = bufferSize / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        
        for (DWORD i = 0; i < numEntries; ++i) {
            if (ProcessProcessorCore(buffer[i], physicalCores, logicalCores)) {
                hyperThreadingEnabled = true;
            }
        }
        
        if (logicalCores > physicalCores) {
            hyperThreadingEnabled = true;
        }
    } else {
        // ... error handling ...
    }
    
    // ... cleanup and caching ...
}
```

**Priority:** **MEDIUM** - Improves readability but doesn't affect functionality.

---

### MAJOR Issues (3)

#### 5. Non-Portable Path to File (S3806)

**Line:** 9  
**Rule:** `cpp:S3806`  
**Severity:** MAJOR  
**Impact:** MEDIUM (Maintainability)  
**Effort:** 5min

**Problem:**
```9:9:CpuFeatures.cpp
#include <windows.h>  // For GetLogicalProcessorInformation
```

**Message:** "non-portable path to file '<Windows.h>'; specified path differs in case from file name on disk"

**Recommendation:**
Change to match the actual case on Windows:
```cpp
#include <Windows.h>  // Capital W
```

**Note:** On Windows, the file system is case-insensitive, but SonarQube flags this for portability. However, since this is Windows-specific code (`#ifdef _WIN32`), the current include is fine. This is a minor issue.

**Priority:** **LOW** - Windows-specific code, case doesn't matter on Windows.

---

#### 6. Replace Redundant Type with `auto` (S5827)

**Line:** 181  
**Rule:** `cpp:S5827`  
**Severity:** MAJOR  
**Impact:** MEDIUM (Maintainability)  
**Effort:** 2min

**Problem:**
```181:182:CpuFeatures.cpp
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = 
        (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(bufferSize);
```

**Recommendation:**
If keeping `malloc` (not recommended), use `auto`:
```cpp
auto buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(bufferSize);
```

**Note:** This becomes irrelevant if we replace `malloc` with `std::vector` (recommended).

**Priority:** **LOW** - Will be addressed when fixing `malloc` issue.

---

#### 7. Use `std::array` or `std::vector` Instead of C-Style Array (S5945)

**Line:** 33  
**Rule:** `cpp:S5945`  
**Severity:** MAJOR  
**Impact:** MEDIUM (Maintainability)  
**Effort:** 10min

**Problem:**
```33:33:CpuFeatures.cpp
    int info[4];
```

**Current Code:**
```cpp
static bool CheckAVX2Support() {
    int info[4];  // Line 33
    // ...
}
```

**Recommendation:**
Replace with `std::array`:
```cpp
#include <array>

static bool CheckAVX2Support() {
    std::array<int, 4> info;
    cpuid(info.data(), 0);  // Use .data() for C API compatibility
    // ...
}
```

**Note:** The `cpuid` function expects a pointer, so use `.data()` or `&info[0]`.

**Priority:** **MEDIUM** - Good practice, improves type safety.

---

### MINOR Issues (1)

#### 8. Use Init-Statement for Variable Declaration (S6004)

**Line:** 39  
**Rule:** `cpp:S6004`  
**Severity:** MINOR  
**Impact:** LOW (Maintainability)  
**Effort:** 2min

**Problem:**
```36:40:CpuFeatures.cpp
    // Check if CPUID supports extended features (leaf 7)
    cpuid(info, 0);
    unsigned int max_leaf = info[0];
    
    if (max_leaf < 7) {
```

**Recommendation:**
Use C++17 init-statement:
```cpp
if (unsigned int max_leaf = info[0]; max_leaf < 7) {
    return false;
}
```

**Note:** This requires moving the `cpuid` call inside the if statement or restructuring slightly.

**Current Code:**
```cpp
cpuid(info, 0);
unsigned int max_leaf = info[0];

if (max_leaf < 7) {
    return false;
}
```

**Recommended:**
```cpp
cpuid(info, 0);
if (unsigned int max_leaf = info[0]; max_leaf < 7) {
    return false;
}
```

**Priority:** **LOW** - Minor code style improvement.

---

## Summary Table

| # | Rule | Severity | Line | Issue | Effort | Priority |
|---|------|----------|------|-------|--------|----------|
| 1 | S5421 | CRITICAL | 15 | Global variable should be const | 20min | LOW* |
| 2 | S5421 | CRITICAL | 16 | Global variable should be const | 20min | LOW* |
| 3 | S5421 | CRITICAL | 19 | Global variable should be const | 20min | LOW* |
| 4 | S5421 | CRITICAL | 20 | Global variable should be const | 20min | LOW* |
| 5 | S5421 | CRITICAL | 21 | Global variable should be const | 20min | LOW* |
| 6 | S5421 | CRITICAL | 22 | Global variable should be const | 20min | LOW* |
| 7 | S5421 | CRITICAL | 23 | Global variable should be const | 20min | LOW* |
| 8 | S1231 | CRITICAL | 182 | Remove use of `malloc` | 10min | **HIGH** |
| 9 | S1231 | CRITICAL | 222 | Remove use of `free` | 10min | **HIGH** |
| 10 | S1231 | CRITICAL | 231 | Remove use of `free` | 10min | **HIGH** |
| 11 | S134 | CRITICAL | 210 | Deep nesting | 10min | MEDIUM |
| 12 | S3806 | MAJOR | 9 | Include path case | 5min | LOW |
| 13 | S5827 | MAJOR | 181 | Use `auto` | 2min | LOW |
| 14 | S5945 | MAJOR | 33 | Use `std::array` | 10min | MEDIUM |
| 15 | S6004 | MINOR | 39 | Use init-statement | 2min | LOW |

\* **False positives** - These variables cannot be const (they're cache variables)

---

## Recommended Action Plan

### Phase 1: Critical Memory Management (HIGH Priority)
**Estimated Time:** 30 minutes

1. **Replace `malloc`/`free` with `std::vector`** (Lines 181-182, 222, 231)
   - Replace `malloc` with `std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION>`
   - Remove `free()` calls (RAII handles cleanup)
   - Update `GetLogicalProcessorInformation` call to use `.data()`

**Impact:** Eliminates 3 CRITICAL issues, improves memory safety

---

### Phase 2: Code Quality Improvements (MEDIUM Priority)
**Estimated Time:** 20 minutes

2. **Replace C-style array with `std::array`** (Line 33)
   - Change `int info[4]` to `std::array<int, 4> info`
   - Update `cpuid` calls to use `.data()` or `&info[0]`

3. **Reduce nesting in `CheckHyperThreadingEnabled()`** (Line 210)
   - Extract processor core processing into helper function
   - Improves readability

**Impact:** Eliminates 2 issues, improves code maintainability

---

### Phase 3: Code Style (LOW Priority)
**Estimated Time:** 10 minutes

4. **Use C++17 init-statement** (Line 39)
   - Move variable declaration into if statement

5. **Fix include path case** (Line 9)
   - Change `<windows.h>` to `<Windows.h>` (optional, Windows-specific code)

**Impact:** Eliminates 2 minor issues

---

### Phase 4: False Positives (Document/Suppress)
**Estimated Time:** 5 minutes

6. **Document or suppress global variable const issues** (Lines 15-16, 19-23)
   - Add `// NOSONAR` comments explaining why variables cannot be const
   - Or add documentation comment explaining the caching pattern

**Impact:** Documents design decision, clarifies false positives

---

## Code Quality Metrics

### Current State
- **Total Issues:** 15
- **Critical:** 10 (67%)
- **Major:** 3 (20%)
- **Minor:** 1 (7%)
- **False Positives:** 7 (47% of total issues)

### After Recommended Fixes
- **Total Issues:** 0-7 (depending on false positive handling)
- **Critical:** 0-7 (all false positives if documented)
- **Major:** 0
- **Minor:** 0

### Estimated Total Effort
- **Phase 1 (Critical):** 30 minutes
- **Phase 2 (Quality):** 20 minutes
- **Phase 3 (Style):** 10 minutes
- **Phase 4 (Documentation):** 5 minutes
- **Total:** ~65 minutes

---

## Additional Observations

### Positive Aspects
✅ **Thread Safety:** Good use of `std::atomic` for thread-safe caching  
✅ **Platform Abstraction:** Good separation of Windows vs. non-Windows code  
✅ **Error Handling:** Proper error handling in `CheckHyperThreadingEnabled()`  
✅ **Documentation:** Code is reasonably well-documented

### Areas for Improvement
⚠️ **Memory Management:** Use modern C++ (RAII) instead of C-style `malloc`/`free`  
⚠️ **Code Complexity:** Deep nesting in `CheckHyperThreadingEnabled()`  
⚠️ **Type Safety:** C-style arrays should be replaced with `std::array`

---

## Conclusion

The code has **7 false positive issues** (global variable const-correctness) that should be documented or suppressed. The **real issues** are:

1. **CRITICAL:** Use of `malloc`/`free` instead of modern C++ memory management (3 issues)
2. **CRITICAL:** Deep nesting that can be refactored (1 issue)
3. **MAJOR:** C-style array that should be `std::array` (1 issue)

**Recommended Priority:**
1. Fix `malloc`/`free` issues (30 min) - **HIGH PRIORITY**
2. Replace C-style array (10 min) - **MEDIUM PRIORITY**
3. Reduce nesting (10 min) - **MEDIUM PRIORITY**
4. Address false positives with documentation (5 min) - **LOW PRIORITY**

After these fixes, the code will be significantly improved and SonarQube-compliant (except for documented false positives).

