---
description: Use std::string_view for read-only string parameters (C++17, modernize-use-string-view)
globs: "**/*.cpp,**/*.h,**/*.hpp"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
---

# std::string_view for read-only string parameters

Use `std::string_view` instead of `const std::string&` for parameters that only **read** string data. Avoids allocations when callers pass literals, `char*`, or existing `string_view`s.

## Do

```cpp
// ✅ Read-only: use string_view
void ProcessPath(std::string_view path) {
  if (path.empty()) return;
  // use path.data(), path.size(), path.substr(), etc.
}

bool MatchesFilter(std::string_view display_name, std::string_view filter_text);
```

## Don't

- **Storing the string:** If the function needs to keep a copy, use `const std::string&` or `std::string` by value.
- **Null-termination for C APIs:** If you pass to a C API that requires `\0` (e.g. `fopen(path.data())`), ensure the view is null-terminated or convert to `std::string` at the call site.

```cpp
// ❌ Read-only but forces allocation when called with "C:\\file.txt"
void ProcessPath(const std::string& path);
```

**Enforcement:** clang-tidy `modernize-use-string-view`; Sonar. **Reference:** AGENTS.md § Use std::string_view Instead of const std::string&.
