# Proposal: Disable Irrelevant clang-tidy Checks

**Date:** 2026-01-17  
**Purpose:** Reduce noise from clang-tidy checks that are not relevant or applicable to this specific project

## Executive Summary

After analyzing 3,184 clang-tidy warnings, I propose disabling **8 check categories** that generate **~1,200 warnings** (38% of total) but are not relevant to this Windows-focused, cross-platform C++17 project. These are primarily style preferences, platform-specific checks, or checks that conflict with project conventions.

## Proposed Disabled Checks

### 1. Floating Point Literal Suffix (350 warnings) ⭐ **HIGH PRIORITY**

**Checks to disable:**
- `hicpp-uppercase-literal-suffix`
- `readability-uppercase-literal-suffix`

**Rationale:**
- **Project convention**: The codebase consistently uses lowercase `f` suffix (e.g., `0.5f`, `0.4f`)
- **Industry standard**: Lowercase `f` is the most common convention in C++ codebases
- **ImGui compatibility**: ImGui examples and documentation use lowercase `f`
- **No functional impact**: This is purely stylistic - both `0.5f` and `0.5F` are equivalent
- **Code consistency**: Changing to uppercase would require modifying hundreds of literals for no benefit

**Impact:** Eliminates 350 warnings (11% of total)

**Example from codebase:**
```cpp
// Current (project convention)
ImGui::SetCursorPosX((window_width - title_width) * 0.5f);
constexpr float kMinFontSize = 10.0f;

// Check wants (not project convention)
ImGui::SetCursorPosX((window_width - title_width) * 0.5F);
constexpr float kMinFontSize = 10.0F;
```

---

### 2. Fuchsia-Specific Checks (262 warnings) ⭐ **HIGH PRIORITY**

**Checks to disable:**
- `fuchsia-default-arguments-calls` (207 warnings)
- `fuchsia-default-arguments-declarations` (55 warnings)

**Note:** These are already disabled in `.clang-tidy` with `-fuchsia-*`, but warnings are still appearing. This suggests:
- The checks might be enabled via another wildcard
- Or there's a configuration issue

**Rationale:**
- **Platform-specific**: These checks are designed for Fuchsia OS development
- **Not applicable**: This is a Windows-focused project with macOS/Linux support
- **Default arguments are valid C++**: Default function arguments are a standard C++ feature and widely used
- **Project uses default arguments**: The codebase intentionally uses default arguments (e.g., `GenerateSearchConfigAsync(timeout_seconds = 30)`)

**Impact:** Eliminates 262 warnings (8% of total)

**Action:** Verify `-fuchsia-*` is working correctly, or explicitly disable:
```yaml
-fuchsia-default-arguments-calls,
-fuchsia-default-arguments-declarations,
```

---

### 3. Anonymous Namespace vs Static (114 warnings) ⭐ **MEDIUM PRIORITY**

**Check to disable:**
- `llvm-prefer-static-over-anonymous-namespace`

**Rationale:**
- **Both are valid**: Anonymous namespaces and `static` are both valid C++ mechanisms for internal linkage
- **Anonymous namespaces are preferred in some contexts**: They provide better encapsulation for related functions
- **Project uses anonymous namespaces**: The codebase intentionally uses anonymous namespaces (e.g., in `GeminiApiUtils.cpp`, `SettingsWindow.cpp`)
- **Style preference**: This is a style preference, not a correctness issue

**Impact:** Eliminates 114 warnings (4% of total)

**Example from codebase:**
```cpp
// Current (project pattern)
namespace {
void EscapeJsonString(...) { }
void WriteCallback(...) { }
}  // namespace

// Check wants (not necessarily better)
static void EscapeJsonString(...) { }
static void WriteCallback(...) { }
```

---

### 4. Internal Linkage Enforcement (89 warnings) ⭐ **MEDIUM PRIORITY**

**Check to disable:**
- `misc-use-internal-linkage`

**Rationale:**
- **Functions may be intentionally non-static**: Some functions are kept non-static for:
  - Testing purposes (can be accessed from test files)
  - Future extensibility
  - Consistency with class design
- **Overlaps with other checks**: This check overlaps with `llvm-prefer-static-over-anonymous-namespace`
- **Not always appropriate**: Making everything static can reduce flexibility

**Impact:** Eliminates 89 warnings (3% of total)

---

### 5. Static Member Functions (109 warnings) ⭐ **MEDIUM PRIORITY**

**Check to disable:**
- `readability-convert-member-functions-to-static`

**Rationale:**
- **Design choice**: Member functions are sometimes kept as members even if they don't use `this` for:
  - API consistency (all methods are members)
  - Future extensibility (may need `this` later)
  - Interface design (part of class interface)
- **Not always appropriate**: Converting to static changes the API and can break encapsulation
- **Project pattern**: The codebase uses member functions as part of class design

**Impact:** Eliminates 109 warnings (3% of total)

**Example:**
```cpp
// Current (project pattern)
class FileIndex {
public:
  size_t GetSize() const { return files_.size(); }  // Doesn't use this, but part of interface
};

// Check wants
class FileIndex {
public:
  static size_t GetSize() const { return files_.size(); }  // Changes API
};
```

---

### 6. Namespace Comments (13 warnings) ⭐ **LOW PRIORITY**

**Checks to disable:**
- `google-readability-namespace-comments`
- `llvm-namespace-comment`

**Rationale:**
- **Style preference**: Namespace closing comments are optional and not critical
- **No functional impact**: Missing comments don't affect code correctness
- **Project doesn't use them**: The codebase doesn't consistently use namespace closing comments
- **Low value**: Only 13 warnings, but easy to disable

**Impact:** Eliminates 13 warnings (<1% of total)

**Example:**
```cpp
// Check wants
}  // namespace ui

// Current (also valid)
}  // namespace
```

---

### 7. Redundant Inline Specifier (12 warnings) ⭐ **LOW PRIORITY**

**Check to disable:**
- `readability-redundant-inline-specifier`

**Rationale:**
- **Minor issue**: Redundant `inline` doesn't cause problems, just noise
- **Header files**: `inline` is often used in header files for template/header-only code
- **Low impact**: Only 12 warnings

**Impact:** Eliminates 12 warnings (<1% of total)

---

### 8. Implicit Bool Conversion (53 warnings) ⭐ **CONSIDER CAREFULLY**

**Check to disable:**
- `readability-implicit-bool-conversion`

**Rationale:**
- **Idiomatic C++**: Implicit pointer-to-bool conversion is idiomatic (e.g., `if (ptr)`)
- **Common pattern**: Checking pointers/nullability is a common, accepted pattern
- **No safety issue**: Modern C++ allows this, and it's widely used

**Counter-argument:**
- **Explicit is better**: Some argue explicit checks (`if (ptr != nullptr)`) are clearer
- **Project may want this**: This check can catch bugs

**Recommendation:** **KEEP ENABLED** but document that some warnings may be acceptable (e.g., pointer null checks)

**Impact if disabled:** Would eliminate 53 warnings (2% of total), but **recommend keeping this check**

---

## Summary Table

| Check Category | Warnings | Priority | Recommendation |
|---------------|----------|----------|----------------|
| Floating Point Suffix | 350 | HIGH | **DISABLE** |
| Fuchsia Default Args | 262 | HIGH | **DISABLE** (verify config) |
| Anonymous Namespace | 114 | MEDIUM | **DISABLE** |
| Internal Linkage | 89 | MEDIUM | **DISABLE** |
| Static Member Functions | 109 | MEDIUM | **DISABLE** |
| Namespace Comments | 13 | LOW | **DISABLE** |
| Redundant Inline | 12 | LOW | **DISABLE** |
| Implicit Bool Conversion | 53 | MEDIUM | **KEEP** (document exceptions) |
| **TOTAL** | **1,002** | | **~31% reduction** |

## Proposed `.clang-tidy` Changes

Add the following to the `Checks:` section:

```yaml
Checks: >-
  *,
  -altera-*,
  -bugprone-easily-swappable-parameters,
  -cppcoreguidelines-avoid-do-while,
  -cppcoreguidelines-avoid-magic-numbers,
  -cppcoreguidelines-macro-to-enum,
  -fuchsia-*,
  -hicpp-uppercase-literal-suffix,           # NEW: Disable uppercase F requirement
  -readability-uppercase-literal-suffix,    # NEW: Disable uppercase F requirement
  -llvm-prefer-static-over-anonymous-namespace,  # NEW: Allow anonymous namespaces
  -misc-use-internal-linkage,               # NEW: Allow non-static functions
  -readability-convert-member-functions-to-static,  # NEW: Keep member functions as members
  -google-readability-namespace-comments,   # NEW: Don't require namespace comments
  -llvm-namespace-comment,                  # NEW: Don't require namespace comments
  -readability-redundant-inline-specifier,  # NEW: Allow redundant inline
  -hicpp-special-member-functions,
  -llvm-header-guard,
  -llvmlibc-*,
  -misc-include-cleaner,
  -misc-use-anonymous-namespace,
  -modernize-macro-to-enum,
  -modernize-use-trailing-return-type,
  -portability-avoid-pragma-once,
  -readability-identifier-length,
  -readability-use-concise-preprocessor-directives
```

## Checks to KEEP (Important for Code Quality)

These checks should **remain enabled** as they catch real issues:

1. **Uninitialized Variables (92 warnings)** - `cppcoreguidelines-init-variables` - **KEEP** (catches bugs)
2. **Const Correctness (134 warnings)** - `misc-const-correctness` - **KEEP** (improves code quality)
3. **Member Initialization (46 warnings)** - `cppcoreguidelines-pro-type-member-init` - **KEEP** (catches bugs)
4. **C-Style Arrays (19 warnings)** - `cppcoreguidelines-avoid-c-arrays` - **KEEP** (safety issue)
5. **C-Style Casts (6 warnings)** - `cppcoreguidelines-pro-type-cstyle-cast` - **KEEP** (safety issue)
6. **Empty Catch Statements (14 warnings)** - `bugprone-empty-catch` - **KEEP** (catches bugs)
7. **Cognitive Complexity (18 warnings)** - `readability-function-cognitive-complexity` - **KEEP** (maintainability)
8. **Include Order (70 warnings)** - `llvm-include-order` - **KEEP** (project standard)

## Magic Numbers - Consider Configuration Tuning

**Current:** 278 warnings from `readability-magic-numbers`

**Recommendation:** **KEEP ENABLED** but consider expanding ignored values for UI constants:

```yaml
CheckOptions:
  readability-magic-numbers.IgnoredFloatingPointValues: '0.0;0.5;1.0;-1.0;2.0;100.0;0.3;0.4;0.5;0.7;0.8;0.2;0.25;0.3;0.35'
```

Many magic numbers in UI code (0.5f, 0.4f, etc.) are common UI constants (percentages, ratios) and could be ignored.

## Naming Conventions - Keep but Review Configuration

**Current:** 525 warnings from `readability-identifier-naming`

**Recommendation:** **KEEP ENABLED** but review configuration. Many warnings are legitimate violations that should be fixed:
- Parameter naming (e.g., `soaView` should be `soa_view`)
- Member naming (e.g., `isDirectory` should be `is_directory_`)
- Namespace naming (e.g., `AppBootstrapCommon` should be `app_bootstrap_common`)

These are real issues that should be addressed, not disabled.

## Implementation Steps

1. **Update `.clang-tidy`** with proposed disabled checks
2. **Verify Fuchsia checks** are actually disabled (may need explicit disable)
3. **Re-run clang-tidy** to confirm warning reduction
4. **Document exceptions** for checks we keep (e.g., implicit bool for pointer checks)
5. **Update project documentation** to explain disabled checks and rationale

## Expected Results

After implementing these changes:
- **Warning reduction:** ~1,000 warnings eliminated (31% reduction)
- **Remaining warnings:** ~2,184 warnings (mostly legitimate issues to fix)
- **Focus on real issues:** Naming conventions, uninitialized variables, const correctness, etc.
- **Better signal-to-noise ratio:** Easier to identify and fix real problems

## Notes

- **Floating point suffix**: This is the biggest win (350 warnings) with zero functional impact
- **Fuchsia checks**: Should already be disabled but still generating warnings - needs investigation
- **Style vs. correctness**: We're disabling style preferences, not correctness checks
- **Project conventions**: All disabled checks conflict with established project patterns

## Questions for Review

1. Should we disable `readability-implicit-bool-conversion`? (53 warnings, but can catch bugs)
2. Should we expand magic number ignored values for UI constants?
3. Are there other style checks generating noise that should be reviewed?
