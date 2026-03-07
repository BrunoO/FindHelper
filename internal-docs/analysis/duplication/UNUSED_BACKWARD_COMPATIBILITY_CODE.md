# Unused Backward Compatibility Code Analysis

## Overview

This document identifies code kept for "backward compatibility" that is not actually used in the codebase. Since this project has no external consumers, backward compatibility code is unnecessary and should be removed to reduce maintenance overhead.

## Investigation Date

2026-01-05

## Unused Backward Compatibility Code

### 1. `FileIndex::BuildFullPath()`

**Location:** `src/index/FileIndex.h:548`

**Status:** ❌ **NOT USED**

**Comment:** "Kept for backward compatibility (if any external code calls it)"

**Finding:** No calls to `file_index->BuildFullPath()` or `index->BuildFullPath()` found in the codebase. All code uses `PathBuilder::BuildFullPath()` directly.

**Recommendation:** Remove this method. It's a simple wrapper that delegates to `PathBuilder::BuildFullPath()`.

---

### 2. `DirectXManager::Initialize(HWND)`

**Location:** `src/platform/windows/DirectXManager.h:97`, `DirectXManager.cpp:73`

**Status:** ❌ **NOT USED**

**Comment:** "Legacy method for compatibility (delegates to Initialize)"

**Finding:** No calls to `DirectXManager::Initialize(HWND)` found. All code uses `Initialize(GLFWwindow*)` instead. The HWND version extracts the HWND from the GLFW window internally.

**Recommendation:** Remove this overload. The `Initialize(GLFWwindow*)` version is the only one used.

---

### 3. `path_constants::kDefaultVolumeRootPath` and `kDefaultUserRootPath`

**Location:** `src/path/PathConstants.h:32-36`

**Status:** ❌ **NOT USED**

**Comment:** "Legacy constants for backward compatibility (deprecated, use functions above)"

**Finding:** These constants are only mentioned in a comment in `FileIndex.h` but never actually used. All code uses the functions `GetDefaultVolumeRootPath()` and `GetDefaultUserRootPath()` instead.

**Recommendation:** Remove these constants. They're replaced by platform-specific functions.

---

### 4. `SearchContext::use_regex` and `use_regex_path`

**Location:** `src/search/SearchContext.h:44, 46`

**Status:** ❌ **NOT USED**

**Comment:** "Legacy: not used (SimpleRegex removed, use PathPattern instead)"

**Finding:** These flags are never read or written. The comment explicitly states they're not used.

**Recommendation:** Remove these fields from `SearchContext`. They're legacy from when SimpleRegex was used.

---

### 5. `StdRegexUtils::RegexMatch(const std::string&, const std::string&)`

**Location:** `src/utils/StdRegexUtils.h:338`

**Status:** ⚠️ **NEEDS VERIFICATION**

**Comment:** "Overload for std::string pattern and text (for backward compatibility)"

**Finding:** This overload delegates to the `string_view` version. Need to verify if any code actually calls this specific overload or if all code uses the `string_view` version.

**Recommendation:** If unused, remove this overload. The `string_view` version handles all cases.

---

## Actually Used (Keep These)

### 1. `SearchInputField::operator std::string()`

**Location:** `src/search/SearchInputField.h:119`

**Status:** ✅ **USED**

**Comment:** "Implicit conversion to std::string (for backward compatibility)"

**Finding:** Used in `GuiState::BuildCurrentSearchParams()` where `SearchInputField` is assigned to `std::string` fields in `SearchParams`.

**Recommendation:** Keep this conversion operator. It's actively used.

---

### 2. `LazyFileSize::operator uint64_t()` and `LazyFileTime::operator const FILETIME&()`

**Location:** `src/index/LazyValue.h:70, 112`

**Status:** ⚠️ **MAY BE USED**

**Comment:** "Implicit conversion ... (for backward compatibility)"

**Finding:** These implicit conversions may be used when `LazyFileSize` or `LazyFileTime` are used in contexts expecting `uint64_t` or `FILETIME`. Need to verify actual usage.

**Recommendation:** If used, keep. If not used, consider removing to make conversions explicit.

---

## Summary

**Total Unused Items Found:** 4 confirmed, 2 need verification

**Action Items:**
1. ✅ Remove `FileIndex::BuildFullPath()` method - **COMPLETED**
2. ✅ Remove `DirectXManager::Initialize(HWND)` overload - **COMPLETED**
3. ✅ Remove `path_constants::kDefaultVolumeRootPath` and `kDefaultUserRootPath` constants - **COMPLETED**
4. ✅ Remove `SearchContext::use_regex` and `use_regex_path` fields - **COMPLETED**
5. ✅ Verify `StdRegexUtils::RegexMatch(const std::string&, const std::string&)` overload - **KEPT** (used in `Popups.cpp`)
6. ⚠️ `LazyFileSize` and `LazyFileTime` implicit conversions - **KEPT** (low risk, code uses explicit access but conversions may be used in subtle ways)

## Rule Added to AGENTS.md

A new rule has been added to `AGENTS.md` under "Remove Unused Backward Compatibility Code" that:
- Explains why backward compatibility code is unnecessary in this project
- Provides guidance on when to remove it
- Lists examples of unused backward compatibility code
- Explains how to verify if code is actually used

## Benefits of Removal

- **Cleaner codebase:** Less code to maintain
- **Reduced confusion:** Developers won't be misled by deprecated APIs
- **Better clarity:** Only current APIs are available
- **Less maintenance:** No need to keep unused code in sync with changes

