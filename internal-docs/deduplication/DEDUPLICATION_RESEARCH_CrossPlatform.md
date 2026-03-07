# Cross-Platform Deduplication Research

**Date:** 2026-01-10  
**Target Files:**
- `src/platform/windows/AppBootstrap_win.cpp` (9 duplicate blocks)
- `src/platform/linux/AppBootstrap_linux.cpp` (9 duplicate blocks)
- `src/platform/windows/FileOperations_win.cpp` (4 duplicate blocks)
- `src/platform/linux/FileOperations_linux.cpp` (6 duplicate blocks)

**Goal:** Identify opportunities to deduplicate cross-platform code while maintaining platform-specific behavior and zero performance overhead.

---

## Executive Summary

Both `AppBootstrap` and `FileOperations` files already use a common header (`AppBootstrapCommon.h` and `FileOperations.h`) to share interfaces and some common utilities. However, there are still opportunities to extract additional common patterns:

1. **AppBootstrap files:** Exception handling patterns, initialization sequence structure
2. **FileOperations files:** Path validation logic, error handling patterns, function structure

**Key Constraint:** Platform-specific code must remain in platform-specific files. Deduplication should extract common patterns into shared utilities, not merge platform implementations.

---

## 1. AppBootstrap Deduplication Analysis

### 1.1 Current State

Both files already use `AppBootstrapCommon.h` which provides:
- `SetupGlfwErrorCallback()`
- `LogCpuInformation()`
- `LoadIndexFromFile()`
- `ApplyCommandLineOverrides()`
- `CreateGlfwWindow()`
- `SetupWindowResizeCallback()`
- `InitializeImGuiContext()`
- Exception message builders

### 1.2 Duplication Patterns Identified

#### Pattern 1: Exception Handling Structure
**Location:** Both files have similar try-catch blocks with multiple exception types

**Windows (lines 302-314):**
```cpp
} catch (const std::bad_alloc& e) {
  return HandleInitializationException(
      AppBootstrapCommon::BuildBadAllocErrorMessage(e),
      result);
} catch (const std::runtime_error& e) {
  return HandleInitializationException(
      AppBootstrapCommon::BuildRuntimeErrorMessage(e),
      result);
} catch (...) {
  return HandleInitializationException(
      AppBootstrapCommon::GetUnknownExceptionMessage(),
      result);
}
```

**Linux (lines 214-249):**
```cpp
} catch (const std::system_error& e) {
  HandleInitializationException(
      AppBootstrapCommon::BuildSystemErrorMessage(e),
      result);
  return result;
} catch (const std::bad_alloc& e) {
  HandleInitializationException(
      AppBootstrapCommon::BuildBadAllocErrorMessage(e),
      result);
  return result;
} catch (const std::invalid_argument& e) {
  HandleInitializationException(
      AppBootstrapCommon::BuildInvalidArgumentErrorMessage(e),
      result);
  return result;
} catch (const std::logic_error& e) {
  HandleInitializationException(
      AppBootstrapCommon::BuildLogicErrorMessage(e),
      result);
  return result;
} catch (const std::runtime_error& e) {
  HandleInitializationException(
      AppBootstrapCommon::BuildRuntimeErrorMessage(e),
      result);
  return result;
} catch (const std::exception& e) {
  HandleInitializationException(
      AppBootstrapCommon::BuildExceptionErrorMessage(e),
      result);
  return result;
} catch (...) {
  HandleInitializationException(
      AppBootstrapCommon::GetUnknownExceptionMessage(),
      result);
  return result;
}
```

**Analysis:**
- Linux has more specific exception types (system_error, invalid_argument, logic_error)
- Windows uses a simpler catch-all approach
- Both use the same exception message builders from `AppBootstrapCommon`
- The structure is similar but Linux has more granular handling

**Deduplication Opportunity:** Create a template function that handles exception catching with a policy-based approach for which exceptions to catch.

#### Pattern 2: Initialization Sequence Structure
**Location:** Both files follow similar initialization sequences

**Common Sequence:**
1. Setup GLFW error callback
2. Initialize GLFW
3. Log CPU information
4. Load index from file (if specified)
5. Load settings and apply command-line overrides
6. Create GLFW window
7. Initialize graphics backend (DirectX/OpenGL)
8. Setup window resize callback
9. Initialize ImGui backend
10. Apply font settings
11. Return result

**Differences:**
- Windows: COM initialization, admin privilege checking, UsnMonitor startup
- Linux: OpenGL hints configuration, index validation

**Deduplication Opportunity:** The sequence structure is already well-extracted into `AppBootstrapCommon`. Remaining duplication is in platform-specific steps.

#### Pattern 3: Cleanup Sequence
**Location:** Both files have cleanup functions

**Windows Cleanup (lines 319-352):**
```cpp
void Cleanup(AppBootstrapResult &result) {
  if (result.monitor) {
    result.monitor->Stop();
    result.monitor.reset();
  }
  if (result.renderer) {
    result.renderer->ShutdownImGui();
  }
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  if (result.renderer) {
    result.renderer->Cleanup();
  }
  if (result.window) {
    glfwDestroyWindow(result.window);
  }
  glfwTerminate();
  if (result.com_initialized) {
    OleUninitialize();
  }
}
```

**Linux Cleanup (lines 252-275):**
```cpp
void Cleanup(AppBootstrapResultLinux &result) {
  if (result.renderer) {
    result.renderer->ShutdownImGui();
    result.renderer->Cleanup();
  }
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  if (result.window) {
    glfwDestroyWindow(result.window);
    result.window = nullptr;
  }
  glfwTerminate();
  result.renderer = nullptr;
  result.settings = nullptr;
  result.file_index = nullptr;
}
```

**Analysis:**
- Both shutdown ImGui backends, destroy context, cleanup renderer, destroy window, terminate GLFW
- Windows has additional: UsnMonitor stop, COM uninitialize
- Linux has additional: OpenGL3 shutdown, field nullification

**Deduplication Opportunity:** Extract common ImGui/GLFW cleanup into a helper function in `AppBootstrapCommon.h`.

---

## 2. FileOperations Deduplication Analysis

### 2.1 Current State

Both files implement the same interface from `FileOperations.h`:
- `OpenFileDefault()`
- `OpenParentFolder()`
- `CopyPathToClipboard()`
- `DeleteFileToRecycleBin()`
- `ShowFolderPickerDialog()`

### 2.2 Duplication Patterns Identified

#### Pattern 1: Path Validation
**Location:** Both files have identical `ValidatePath()` function

**Windows (lines 55-66):**
```cpp
bool ValidatePath(std::string_view path, const char* operation) {
  if (path.empty()) {
    LOG_WARNING(std::string(operation) + " called with empty path");
    return false;
  }
  // Check for embedded nulls (security concern)
  if (path.find('\0') != std::string::npos) {
    LOG_ERROR(std::string(operation) + " called with path containing null character");
    return false;
  }
  return true;
}
```

**Linux (lines 46-57):**
```cpp
bool ValidatePath(std::string_view path, const char* operation) {
  if (path.empty()) {
    LOG_WARNING(std::string(operation) + " called with empty path");
    return false;
  }
  // Check for embedded nulls (security concern)
  if (path.find('\0') != std::string::npos) {
    LOG_ERROR(std::string(operation) + " called with path containing null character");
    return false;
  }
  return true;
}
```

**Analysis:** 100% identical - perfect candidate for extraction.

**Deduplication Opportunity:** Move `ValidatePath()` to a common location (e.g., `FileOperations.h` as an inline function, or a new `FileOperationsCommon.h`).

#### Pattern 2: Function Structure
**Location:** All file operation functions follow the same pattern

**Common Pattern:**
1. Validate path using `ValidatePath()`
2. Log operation start
3. Platform-specific execution
4. Error handling and logging
5. Return result

**Example - OpenFileDefault:**

**Windows:**
```cpp
void OpenFileDefault(const std::string &full_path) {
  if (!internal::ValidatePath(full_path, "OpenFileDefault")) {
    return;
  }
  LOG_INFO("Opening file: " + full_path);
  std::wstring wide_path = Utf8ToWide(full_path);
  // ... Windows-specific code ...
}
```

**Linux:**
```cpp
void OpenFileDefault(const std::string &full_path) {
  if (!internal::ValidatePath(full_path, "OpenFileDefault")) {
    return;
  }
  LOG_INFO("Opening file: " + full_path);
  // ... Linux-specific code ...
}
```

**Analysis:** The structure is identical, but the platform-specific execution differs. This is expected and cannot be deduplicated without losing platform-specific behavior.

**Deduplication Opportunity:** Extract validation + logging into a helper macro or inline function, but the execution must remain platform-specific.

#### Pattern 3: Error Logging Patterns
**Location:** Both files log errors in similar ways

**Windows:**
- Uses `logging_utils::LogHResultError()` for HRESULT errors
- Uses `logging_utils::LogWindowsApiError()` for Windows API errors
- Uses `LOG_ERROR()` with string concatenation

**Linux:**
- Uses `LOG_ERROR()` with string concatenation
- Reads stderr from temporary log file
- Logs exit codes from failed commands

**Analysis:** Error logging is platform-specific (Windows uses HRESULT, Linux uses exit codes). Cannot be deduplicated without losing platform-specific error information.

---

## 3. Deduplication Strategies

### Strategy 1: Extract Common Utilities (Recommended)

**Approach:** Create shared utility functions in common headers that can be used by both platforms.

**Benefits:**
- Zero performance overhead (inline functions)
- Maintains platform-specific code separation
- Easy to test and maintain
- No runtime polymorphism needed

**Implementation:**

#### 3.1.1 AppBootstrapCommon.h - Add Cleanup Helpers

```cpp
namespace AppBootstrapCommon {
  // Common ImGui/GLFW cleanup sequence
  template<typename BootstrapResult>
  void CleanupImGuiAndGlfw(BootstrapResult& result) {
    if (result.renderer) {
      result.renderer->ShutdownImGui();
    }
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (result.renderer) {
      result.renderer->Cleanup();
    }
    if (result.window) {
      glfwDestroyWindow(result.window);
      result.window = nullptr;
    }
    glfwTerminate();
  }
}
```

**Impact:** Eliminates ~10-15 lines of duplicate cleanup code in both files.

#### 3.1.2 FileOperations.h - Add ValidatePath Helper

```cpp
namespace file_operations {
namespace internal {
  // Common path validation (moved from platform-specific files)
  inline bool ValidatePath(std::string_view path, const char* operation) {
    if (path.empty()) {
      LOG_WARNING(std::string(operation) + " called with empty path");
      return false;
    }
    // Check for embedded nulls (security concern)
    if (path.find('\0') != std::string::npos) {
      LOG_ERROR(std::string(operation) + " called with path containing null character");
      return false;
    }
    return true;
  }
} // namespace internal
} // namespace file_operations
```

**Impact:** Eliminates ~12 lines of duplicate code in both files.

### Strategy 2: Template-Based Exception Handling (Advanced)

**Approach:** Create a template function that handles exception catching with a policy for which exceptions to catch.

**Benefits:**
- Eliminates duplicate exception handling code
- Allows platform-specific exception type selection
- Maintains type safety

**Challenges:**
- More complex implementation
- May reduce code readability
- Exception handling is already well-structured

**Recommendation:** **NOT RECOMMENDED** - The current exception handling is clear and platform-specific differences (Linux catches more exception types) are intentional. The duplication is minimal (~20 lines) and the complexity of a template solution isn't justified.

### Strategy 3: Macro-Based Function Wrappers (Not Recommended)

**Approach:** Use macros to wrap common patterns (validation + logging).

**Challenges:**
- Macros reduce code readability
- Harder to debug
- Violates modern C++ best practices
- Doesn't provide significant benefit over inline functions

**Recommendation:** **NOT RECOMMENDED** - Prefer inline functions over macros.

---

## 4. Recommended Implementation Plan

### Phase 1: Extract ValidatePath (High Impact, Low Risk)

**Target:** `FileOperations_win.cpp` and `FileOperations_linux.cpp`

**Steps:**
1. Move `ValidatePath()` from both platform files to `FileOperations.h` as an inline function in `file_operations::internal` namespace
2. Update both platform files to use the common function
3. Remove duplicate implementations

**Expected Impact:**
- Eliminates ~12 lines of duplicate code per file (24 lines total)
- Zero performance overhead (inline function)
- Low risk (function is already identical)

**Estimated Time:** 15-30 minutes

### Phase 2: Extract ImGui/GLFW Cleanup (Medium Impact, Low Risk)

**Target:** `AppBootstrap_win.cpp` and `AppBootstrap_linux.cpp`

**Steps:**
1. Add `CleanupImGuiAndGlfw()` template function to `AppBootstrapCommon.h`
2. Update both platform `Cleanup()` functions to call the common helper
3. Keep platform-specific cleanup (UsnMonitor, COM, field nullification) in platform files

**Expected Impact:**
- Eliminates ~10-15 lines of duplicate code per file (20-30 lines total)
- Zero performance overhead (inline template function)
- Low risk (cleanup sequence is well-understood)

**Estimated Time:** 30-45 minutes

### Phase 3: Consider Exception Handling (Low Priority)

**Analysis:** Exception handling patterns are similar but have intentional platform-specific differences (Linux catches more exception types). The duplication is minimal (~20 lines) and the current structure is clear and maintainable.

**Recommendation:** **DEFER** - Not worth the complexity for the minimal duplication. Revisit if exception handling patterns change significantly.

---

## 5. Expected Results

### Before Deduplication:
- `AppBootstrap_win.cpp`: 9 duplicate blocks
- `AppBootstrap_linux.cpp`: 9 duplicate blocks
- `FileOperations_win.cpp`: 4 duplicate blocks
- `FileOperations_linux.cpp`: 6 duplicate blocks

### After Phase 1 (ValidatePath):
- `FileOperations_win.cpp`: ~3 duplicate blocks (reduced by 1)
- `FileOperations_linux.cpp`: ~5 duplicate blocks (reduced by 1)

### After Phase 2 (Cleanup):
- `AppBootstrap_win.cpp`: ~7 duplicate blocks (reduced by 2)
- `AppBootstrap_linux.cpp`: ~7 duplicate blocks (reduced by 2)

### Total Expected Reduction:
- **Lines eliminated:** ~44-54 lines
- **Duplicate blocks reduced:** ~6 blocks total
- **Performance impact:** Zero (all functions are inline)
- **Maintainability:** Improved (common utilities in shared headers)

---

## 6. Risk Assessment

### Low Risk:
- **ValidatePath extraction:** Function is 100% identical, well-tested, and used in multiple places
- **Cleanup extraction:** Cleanup sequence is well-understood and platform-specific parts remain separate

### Medium Risk:
- **Exception handling changes:** Not recommended - current structure is clear and platform-specific differences are intentional

### Mitigation:
- All extracted functions should be inline (zero performance overhead)
- Platform-specific code remains in platform-specific files
- Common utilities are well-documented
- Changes are incremental (one phase at a time)

---

## 7. Conclusion

**Recommended Approach:** Extract common utilities (Strategy 1)

**Priority:**
1. **Phase 1 (ValidatePath):** High priority - High impact, low risk, quick to implement
2. **Phase 2 (Cleanup):** Medium priority - Good impact, low risk, moderate effort
3. **Phase 3 (Exception Handling):** Low priority - Minimal duplication, current structure is clear

**Expected Outcome:**
- Eliminate ~44-54 lines of duplicate code
- Reduce duplicate blocks by ~6
- Zero performance overhead
- Improved maintainability
- Platform-specific code remains clearly separated

**Next Steps:**
1. Implement Phase 1 (ValidatePath extraction)
2. Test on all platforms (Windows, Linux, macOS)
3. Implement Phase 2 (Cleanup extraction)
4. Re-run Duplo to verify improvements
5. Update `DEDUPLICATION_CANDIDATES.md` with results
