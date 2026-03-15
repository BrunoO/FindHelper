# MSVC / Windows Build Gotchas

This document lists common patterns that compile on GCC/Clang (and macOS/Linux) but can fail or behave differently on Windows with MSVC. Use it when adding or refactoring code that will be built on Windows.

**Cursor rule:** `.cursor/rules/windows-msvc-cpp.mdc` encodes these pitfalls for AI agents; when editing C++ that is built on Windows, that rule applies.

## 1. Iterator vs pointer type deduction

**Problem:** MSVC cannot deduce `const auto*` or `auto*` from an iterator (e.g. `std::string_view::const_iterator`). Using `.begin()` or `.end()` and assigning to a pointer-typed variable causes C3535 / C2440.

**Wrong:**
```cpp
const auto* it_text = text.begin();  // Fails on MSVC: .begin() returns an iterator, not a pointer
it_text = std::find_if(it_text, text.end(), ...);
```

**Correct:**
```cpp
auto it_text = text.begin();  // Deduces iterator type on all compilers
it_text = std::find_if(it_text, text.end(), ...);
```

**Rule:** For variables holding `.begin()`, `.end()`, or other iterator types, use `auto` (or an explicit iterator type alias), not `const auto*` or `auto*`.

---

## 2. std::min / std::max and Windows.h

**Problem:** `Windows.h` (included on Windows builds) defines macros `min` and `max` that conflict with `std::min` and `std::max`.

**Rule:** Always use parentheses so the macro is not expanded: `(std::min)(a, b)` and `(std::max)(a, b)`. See AGENTS.md "Windows-Specific Coding Rules".

---

## 3. Lambda captures in template code

**Problem:** Implicit captures `[&]` and `[=]` in lambdas inside template functions can cause MSVC parse errors (C2062, C2059, C2143) that cascade into STL headers.

**Rule:** Use explicit capture lists in any lambda defined inside a template: `[&x, &y]` instead of `[&]`. See AGENTS.md "Lambda Captures in Template Functions (MSVC Compatibility)".

---

## 4. Forward declarations: class vs struct

**Problem:** Forward declarations must match the definition keyword (`struct` vs `class`). MSVC warns with C4099 on mismatches.

**Rule:** Run `python3 scripts/find_class_struct_mismatches.py` when touching headers. Keep forward declarations and definitions consistent.

---

## 5. Safe string functions

**Problem:** MSVC warns about unsafe C string functions (`strncpy`, `strcpy`, `strcat`, etc.).

**Rule:** Use `strcpy_safe` and `strcat_safe` from `StringUtils.h` for fixed-size buffers. See AGENTS.md "Unsafe C String Functions".

---

## Verifying Windows builds

- **CI:** The public repo (FindHelper) runs Windows builds on push; fix any red builds promptly.
- **Local:** Build with Visual Studio or Developer Command Prompt: `cmake -S . -B build -A x64` then `cmake --build build --config Release`.
- **Pre-commit:** No Windows-specific checks run on macOS; rely on CI or a local Windows build before pushing changes that touch hot paths (e.g. `StringSearch.h`, template-heavy code).
