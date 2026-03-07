# Clang-Tidy Warnings Quick Reference

**Date:** 2026-01-17  
**Purpose:** Quick reference guide for fixing common clang-tidy warnings

---

## Most Common Warnings

### 1. Uninitialized Variables
**Check:** `cppcoreguidelines-init-variables`  
**Fix:** Always initialize variables at declaration

```cpp
// ❌ Before
size_t remaining;
if (condition) {
  remaining = Calculate();
}

// ✅ After
size_t remaining = 0;  // Safe default
if (condition) {
  remaining = Calculate();
}
```

---

### 2. Cognitive Complexity
**Check:** `readability-function-cognitive-complexity`  
**Threshold:** 25  
**Fix:** Extract helper functions, use early returns

```cpp
// ❌ Before: Complexity 60
void ComplexFunction() {
  if (a) {
    if (b) {
      if (c) {
        // ... nested logic
      }
    }
  }
}

// ✅ After: Split into smaller functions
void ComplexFunction() {
  if (!a) return;
  ProcessA();
  ProcessB();
}

void ProcessA() {
  // Extracted logic
}
```

---

### 3. Naming Conventions
**Check:** `readability-identifier-naming`  
**Fix:** Use `snake_case` for local constants (not `kPascalCase`)

```cpp
// ❌ Before
constexpr size_t kMaxPathLength = 100;

// ✅ After
constexpr size_t max_path_length = 100;
// OR if prefix needed:
constexpr size_t k_max_path_length = 100;
```

---

### 4. Uppercase Literal Suffixes
**Check:** `readability-uppercase-literal-suffix`  
**Fix:** Use uppercase `F` instead of lowercase `f`

```cpp
// ❌ Before
float value = 10.5f;

// ✅ After
float value = 10.5F;
```

---

### 5. Const Correctness
**Check:** `misc-const-correctness`  
**Fix:** Add `const` to variables that aren't modified

```cpp
// ❌ Before
size_t mid = CalculateMid();
// ... mid never modified

// ✅ After
const size_t mid = CalculateMid();
```

---

### 6. Using Namespace Directives
**Check:** `google-build-using-namespace`  
**Fix:** Use specific `using` declarations

```cpp
// ❌ Before
using namespace gemini_api_utils;

// ✅ After
using gemini_api_utils::ParseSearchConfigJson;
using gemini_api_utils::ValidatePathPatternFormat;
```

---

### 7. Anonymous Namespace vs Static
**Check:** `misc-use-anonymous-namespace`  
**Fix:** Use anonymous namespace for internal linkage

```cpp
// ❌ Before
static void HelperFunction() {
  // ...
}

// ✅ After
namespace {
void HelperFunction() {
  // ...
}
}  // namespace
```

**For doctest macros (test files):**
```cpp
// Suppress warning from doctest-generated static functions
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
TEST_CASE("Test Name") {
  // ...
}
```

---

### 8. Do-While Loops
**Check:** `cppcoreguidelines-avoid-do-while`  
**Fix:** Convert to while loop or suppress if intentional

```cpp
// ❌ Before
do {
  CHECK(condition);
} while (false);

// ✅ After (if single execution needed)
CHECK(condition);
```

---

### 9. Raw String Literals
**Check:** `modernize-raw-string-literal`  
**Fix:** Use raw string literals for strings with many escapes

```cpp
// ❌ Before
const std::string text = "files with \"quotes\" and \\backslashes";

// ✅ After
const std::string text = R"(files with "quotes" and \backslashes)";
```

---

### 10. Enum Base Type
**Check:** `performance-enum-size`  
**Fix:** Use `std::uint8_t` if enum values fit

```cpp
// ❌ Before
enum class SizeFilter : int {
  None = 0,
  Small = 1
};

// ✅ After
enum class SizeFilter : std::uint8_t {
  None = 0,
  Small = 1
};
```

---

### 11. C-Style Arrays
**Check:** `cppcoreguidelines-avoid-c-arrays`  
**Fix:** Use `std::array`

```cpp
// ❌ Before
const char* examples[] = {"a", "b"};

// ✅ After
const std::array<const char*, 2> examples = {"a", "b"};
```

---

### 12. Braces Around Statements
**Check:** `readability-braces-around-statements`  
**Fix:** Add braces around single-statement blocks

```cpp
// ❌ Before
if (condition)
  DoSomething();

// ✅ After
if (condition) {
  DoSomething();
}
```

---

## Suppression Comments

### Suppress Specific Warning
```cpp
// NOLINTNEXTLINE(check-name)
void Function() {
  // ...
}
```

### Suppress Multiple Warnings
```cpp
// NOLINTNEXTLINE(check1,check2)
void Function() {
  // ...
}
```

### Suppress for Doctest Macros
```cpp
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
TEST_CASE("Test Name") {
  // ...
}
```

---

## Priority Order

1. **Critical:** Uninitialized variables, cognitive complexity
2. **Style:** Naming, literal suffixes, const correctness
3. **Organization:** Anonymous namespace, do-while, raw strings, enums, arrays

---

## Quick Commands

### Run clang-tidy on single file
```bash
clang-tidy file.cpp
```

### Run clang-tidy on all files
```bash
find src tests -name "*.cpp" -o -name "*.h" | grep -v external | xargs clang-tidy
```

### Run specific check
```bash
clang-tidy file.cpp -checks='readability-identifier-naming'
```

### Apply automatic fixes
```bash
clang-tidy file.cpp -fix
```

---

*Last Updated: 2026-01-17*
