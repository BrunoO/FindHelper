# Clang-Tidy Disabled Checks Review

**Date:** 2026-01-17  
**Purpose:** Review disabled clang-tidy checks to identify any that should be re-enabled

---

## Executive Summary

After reviewing all disabled clang-tidy checks, **one critical check should be re-enabled**:

1. **`cppcoreguidelines-special-member-functions`** - **CRITICAL** - Should be enabled
   - Enforces Rule of Three/Five/Zero
   - Critical for RAII-based code (which this project uses extensively)
   - Prevents resource leaks and double-delete bugs

**Other checks are appropriately disabled** for valid reasons (noise, style conflicts, false positives).

---

## Disabled Checks Analysis

### ✅ **Should Re-Enable: Critical**

#### 1. `cppcoreguidelines-special-member-functions` ⚠️ **CRITICAL**

**Current Status:** ❌ **DISABLED** (line 34)  
**Recommendation:** ✅ **RE-ENABLE**

**Why It's Important:**
- Enforces C++ Core Guidelines rule C.21: If you define any special member function (destructor, copy ctor, copy assignment, move ctor, move assignment), you should define or delete **all five**
- Critical for RAII-based code (this project uses RAII extensively)
- Prevents resource leaks, double-delete bugs, and dangling pointers
- Ensures proper move semantics implementation

**What It Catches:**
- Classes that define a destructor but not copy/move operations (potential shallow copy bugs)
- Classes that define copy operations but not move operations (performance issues)
- Classes that define some but not all special member functions (inconsistent behavior)

**Project Context:**
- This project uses RAII extensively (smart pointers, `ScopedHandle`, `VolumeHandle`, etc.)
- Many classes manage resources (file handles, memory, locks)
- Missing special member functions can lead to resource leaks or double-delete bugs

**Risk if Disabled:**
- **HIGH** - Resource leaks, double-delete bugs, performance issues
- Classes managing resources may have incomplete special member function definitions
- Move semantics may not be properly implemented

**Action:**
```yaml
# Remove from disabled list:
# -cppcoreguidelines-special-member-functions,  # REMOVE THIS LINE
```

**Note:** This check can be configured to allow certain patterns (e.g., only missing move functions, allow default destructor only). If it's too noisy, configure it rather than disabling it entirely.

---

### ✅ **Appropriately Disabled: Valid Reasons**

#### 2. `cppcoreguidelines-avoid-do-while` ✅ **CORRECTLY DISABLED**

**Current Status:** ❌ **DISABLED** (line 31)  
**Reason:** Do-while loops are acceptable in this project  
**Recommendation:** ✅ **KEEP DISABLED**

**Justification:**
- Do-while loops are a valid C++ construct
- Used in test code and some control flow patterns
- No safety or correctness issues with do-while loops
- Disabling is appropriate

---

#### 3. `misc-use-anonymous-namespace` ✅ **CORRECTLY DISABLED**

**Current Status:** ❌ **DISABLED** (line 40)  
**Reason:** Doctest macros generate static functions that can't be moved to anonymous namespace  
**Recommendation:** ✅ **KEEP DISABLED**

**Justification:**
- Doctest framework generates static functions via macros
- These functions cannot be moved to anonymous namespace
- Would cause false positives in test files
- Disabling is appropriate

---

#### 4. `cppcoreguidelines-avoid-magic-numbers` ✅ **CORRECTLY DISABLED**

**Current Status:** ❌ **DISABLED** (line 32)  
**Reason:** `readability-magic-numbers` is enabled instead (more configurable)  
**Recommendation:** ✅ **KEEP DISABLED**

**Justification:**
- `readability-magic-numbers` is enabled and more configurable
- Allows ignoring common values (0, 1, -1, powers of 2, etc.)
- Better suited for this project's needs
- Having both enabled would be redundant

---

#### 5. `misc-include-cleaner` ✅ **CORRECTLY DISABLED**

**Current Status:** ❌ **DISABLED** (line 39)  
**Reason:** Highly unstable and noisy, suggests removing necessary includes  
**Recommendation:** ✅ **KEEP DISABLED**

**Justification:**
- Frequently suggests removing necessary includes (due to templates, indirect dependencies)
- Suggests adding includes that are already transitively present
- Creates noise without improving code quality
- Documented as unstable in project docs

---

#### 6. `modernize-use-trailing-return-type` ✅ **CORRECTLY DISABLED**

**Current Status:** ❌ **DISABLED** (line 42)  
**Reason:** Contradicts established codebase style, intrusive  
**Recommendation:** ✅ **KEEP DISABLED**

**Justification:**
- Trailing return types (`auto func() -> type`) are stylistic
- Contradicts established codebase style
- Would require thousands of changes
- No correctness or safety benefit

---

#### 7. `bugprone-easily-swappable-parameters` ✅ **CORRECTLY DISABLED**

**Current Status:** ❌ **DISABLED** (line 30)  
**Reason:** Can be very noisy, many false positives  
**Recommendation:** ✅ **KEEP DISABLED** (but consider enabling with configuration)

**Justification:**
- Can flag many legitimate function signatures
- Often produces false positives
- Can be configured to be less aggressive, but still noisy
- **Alternative:** Could be enabled with careful configuration, but current disable is reasonable

**Note:** If enabled, would need careful configuration:
```yaml
CheckOptions:
  bugprone-easily-swappable-parameters.MinimumLength: 3  # Only flag 3+ adjacent parameters
  bugprone-easily-swappable-parameters.SuppressParametersUsedTogether: true  # Suppress when params used together
```

---

#### 8. `cppcoreguidelines-special-member-functions` (hicpp version) ✅ **CORRECTLY DISABLED**

**Current Status:** ❌ **DISABLED** (line 36)  
**Reason:** Redundant with cppcoreguidelines version (when enabled)  
**Recommendation:** ✅ **KEEP DISABLED** (but enable cppcoreguidelines version)

**Justification:**
- `hicpp-special-member-functions` is similar to `cppcoreguidelines-special-member-functions`
- Having both enabled would be redundant
- Prefer cppcoreguidelines version (more widely used, better documented)

---

#### 9. Other Disabled Checks ✅ **CORRECTLY DISABLED**

- `-altera-*` - Altera-specific checks, not relevant
- `-cppcoreguidelines-macro-to-enum` - Stylistic, can be noisy
- `-modernize-macro-to-enum` - Stylistic, can be noisy
- `-fuchsia-*` - Fuchsia OS-specific, not relevant
- `-llvm-header-guard` - Conflicts with `#pragma once` (project standard)
- `-llvmlibc-*` - LLVM libc-specific, not relevant
- `-portability-avoid-pragma-once` - Project uses `#pragma once` (standard)
- `-readability-identifier-length` - Too restrictive, would flag many valid names
- `-readability-use-concise-preprocessor-directives` - Stylistic, not critical

---

## Recommendations

### Immediate Action: Re-Enable Critical Check

**Priority:** ⚠️ **HIGH**

1. **Re-enable `cppcoreguidelines-special-member-functions`**
   - Remove from disabled list in `.clang-tidy`
   - Run clang-tidy to see what issues it finds
   - Fix any violations (likely classes managing resources)
   - If too noisy, configure it rather than disabling

**Expected Impact:**
- Will flag classes with incomplete special member function definitions
- May find 5-15 violations in resource-managing classes
- Prevents future bugs (resource leaks, double-delete)

**Configuration Options (if too noisy):**
```yaml
CheckOptions:
  # Allow classes with only default destructor (Rule of Zero pattern)
  cppcoreguidelines-special-member-functions.AllowSoleDefaultDtor: true
  
  # Allow missing move operations (if copy is sufficient)
  cppcoreguidelines-special-member-functions.AllowMissingMoveFunctions: false  # Keep strict
  
  # Allow missing copy operations (if move-only is intended)
  cppcoreguidelines-special-member-functions.AllowMissingCopyFunctions: false  # Keep strict
```

---

### Optional: Consider Enabling with Configuration

**Priority:** ⚠️ **LOW**

2. **Consider `bugprone-easily-swappable-parameters` with configuration**
   - Enable with careful configuration to reduce noise
   - Only flag functions with 3+ adjacent convertible parameters
   - Suppress when parameters are used together
   - **Note:** Current disable is reasonable, this is optional

---

## Impact Analysis

### If We Re-Enable `cppcoreguidelines-special-member-functions`

**Positive:**
- ✅ Catches incomplete special member function definitions
- ✅ Prevents resource leaks and double-delete bugs
- ✅ Ensures proper move semantics
- ✅ Aligns with RAII best practices (which this project uses)

**Potential Issues:**
- ⚠️ May flag 5-15 existing classes
- ⚠️ May require adding `=default` or `=delete` to special member functions
- ⚠️ May require implementing move constructors/assignment operators

**Effort:**
- **Low-Medium:** Most fixes are adding `=default` or `=delete`
- **Time:** 1-2 hours to fix all violations

**Risk:**
- **Low:** Fixes are straightforward (add `=default` or `=delete`)
- **High Value:** Prevents serious bugs (resource leaks, double-delete)

---

## Conclusion

**One critical check should be re-enabled:**
- ✅ **`cppcoreguidelines-special-member-functions`** - Critical for RAII-based code

**All other disabled checks are appropriately disabled** for valid reasons (noise, style conflicts, false positives, or not applicable).

**Action Plan:**
1. Re-enable `cppcoreguidelines-special-member-functions`
2. Run clang-tidy to identify violations
3. Fix violations (add `=default` or `=delete` as appropriate)
4. Configure check if too noisy (rather than disabling)

---

*Last Updated: 2026-01-17*
