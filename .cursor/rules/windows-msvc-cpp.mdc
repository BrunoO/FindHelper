---
description: Windows/MSVC compatibility (min/max, strcpy_safe, lambda captures, include order, forward decl, iterator)
globs: "**/*.cpp,**/*.h,**/*.hpp"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
---

# Windows/MSVC C++ compatibility

When editing C++ that is built on Windows, apply these rules to avoid build and Sonar issues. They address patterns that compile on GCC/Clang but fail or warn on MSVC (e.g. C3535, C2440, C4099, C2062, C2059, C2143, C4996).

## (std::min) / (std::max)

`Windows.h` defines `min`/`max` macros. Use parentheses so the macro is not expanded:

```cpp
int r = (std::min)(a, b);
int m = (std::max)(x, y);
```

## String copy/concat

Use `strcpy_safe` / `strcat_safe` from `StringUtils.h` instead of `strncpy`/`strcpy`/`strcat`. MSVC warns on the latter; safe helpers guarantee null-termination.

## Lambdas in template functions

Use **explicit** capture lists. Implicit `[&]`/`[=]` in template code can cause MSVC C2062/C2059/C2143 and cascade into STL headers.

```cpp
// ✅ CORRECT
auto fn = [&a, &b](int i) { ... };
```

## Include order and case

All `#include` at top of file; system then project. Use **lowercase** include paths (e.g. `<windows.h>`). Sonar S954/S3806.

## Forward declarations

Forward declaration keyword must match definition: `struct` vs `struct`, not `class` vs `struct`. Run `python3 scripts/find_class_struct_mismatches.py` when touching headers.

## strlen (cpp:S1081)

Use only on guaranteed null-terminated buffers; prefer `.size()` / `std::string_view`. Document with NOSONAR on same line if safe.

## Iterator vs pointer (MSVC C3535/C2440)

For variables holding `.begin()`, `.end()`, or other iterator types, use `auto` (or an explicit iterator type). Do not use `const auto*` or `auto*` — MSVC cannot deduce pointer from iterator.

```cpp
// ❌ BAD on MSVC
const auto* it = text.begin();

// ✅ GOOD
auto it = text.begin();
```

## Verification

No Windows-specific checks run on macOS pre-commit. Before pushing changes that touch hot paths (e.g. `StringSearch.h`, template-heavy code), rely on CI or a local Windows build: `cmake -S . -B build -A x64` then `cmake --build build --config Release`.

---

**Full details:** AGENTS.md § Windows-Specific Coding Rules; **`docs/platform/windows/MSVC_BUILD_GOTCHAS.md`** (canonical list of MSVC pitfalls).
