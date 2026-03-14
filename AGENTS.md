# AI Agent Guidelines for USN_WINDOWS Project

## this is a cross platform project
- main development platform is MacOs
- main target is Windows
- secondary target is Linux (Ubuntu)

the application and tests are expected to be built and run on the three platforms: MacOs, Windows, Linux

---

## Platform-Specific Code Blocks

When modifying code, pay close attention to platform-specific preprocessor directives like `#ifdef _WIN32`, `#ifdef __APPLE__`, and `#ifdef __linux__`.

**Do not modify code within these blocks to make it cross-platform.** For example, do not change Windows-style backslashes (`\\`) to forward slashes (`/`) inside a `#ifdef _WIN32` block. This code is intentionally platform-specific.

If you identify platform-specific code that needs to be made cross-platform, the correct approach is to refactor it into a platform-agnostic abstraction with separate implementations for each platform, not to change the existing platform-specific code directly.

### Comment `#endif` Directives

**Problem:** Bare `#endif` lines (with no trailing comment) make it hard to see which `#ifdef` / `#if` / `#ifndef` they close, especially in files with nested conditionals.

**Solution:** Always add a matching comment after every `#endif` that identifies the opening directive. Use two spaces before `//`.

```cpp
// ❌ WRONG - Bare #endif, unclear which block it closes
#ifdef _WIN32
  // ...
#endif

// ✅ CORRECT - Comment matches the opening directive
#ifdef _WIN32
  // ...
#endif  // _WIN32

// ✅ CORRECT - Same style for #if, #ifndef, feature flags
#ifdef LOGGING_ENABLED
  // ...
#endif  // LOGGING_ENABLED
#ifndef NOMINMAX
#define NOMINMAX
#endif  // NOMINMAX
#if STRING_SEARCH_AVX2_AVAILABLE
  // ...
#endif  // STRING_SEARCH_AVX2_AVAILABLE
```

**When to Apply:**
- Every time you add or modify a preprocessor block (`#ifdef`, `#if`, `#ifndef`, `#elif`).
- When touching a file that still has bare `#endif` lines, add the matching comment (Boy Scout Rule).

**Style:** Use two spaces before `//` (e.g. `#endif  // _WIN32`). Keep the comment short: the condition or macro name (e.g. `_WIN32`, `__APPLE__`, `LOGGING_ENABLED`, `FAST_LIBS_BOOST`).

---

## Linux VM - how to set a new environment
- see instructions in docs/guides/building/BUILDING_ON_LINUX.md

## Windows-Specific Coding Rules

### Standard Library Functions vs Windows Macros

**Problem:** `Windows.h` defines `min` and `max` as macros that conflict with `std::min` and `std::max`.

**Solution:** Always use parentheses around the function name to prevent macro expansion.

```cpp
// ❌ WRONG - Will cause compilation errors on Windows
int result = std::min(a, b);
int max_val = std::max(x, y);

// ✅ CORRECT - Prevents macro expansion
int result = (std::min)(a, b);
int max_val = (std::max)(x, y);
```

**When to Apply:** Every time you use `std::min` or `std::max` in code that includes `Windows.h` (which is common in this Windows project).

---

### Unsafe C String Functions

**Problem:** MSVC warns about unsafe C string functions like `strncpy`, `strcpy`, `strcat`, etc. These functions don't guarantee null-termination and can cause buffer overruns.

**Solution:** Always use `strcpy_safe` and `strcat_safe` from `StringUtils.h` instead of unsafe C functions.

```cpp
// ❌ WRONG - MSVC warning C4996: 'strncpy': This function or variable may be unsafe
std::strncpy(dest, src, size - 1);
dest[size - 1] = '\0';

// ✅ CORRECT - Safe, cross-platform, guaranteed null-termination
#include "StringUtils.h"
strcpy_safe(dest, size, src);
```

**Available Functions:**
- `strcpy_safe(dest, dest_size, src)` - Safe string copy with automatic truncation and null-termination
- `strcat_safe(dest, dest_size, src)` - Safe string concatenation with bounds checking

**When to Apply:** 
- Every time you need to copy strings to fixed-size buffers
- When MSVC warns about unsafe string functions
- When working with ImGui input buffers or other fixed-size character arrays

**Note:** On Windows, `strcpy_safe` maps to `strcpy_s` (the secure CRT function). On other platforms, it uses a safe implementation with `memcpy` and explicit null-termination.

---

### Safe Use of `strlen` (cpp:S1081)

**Problem:** `std::strlen(const char*)` reads until it finds `'\0'`. If the pointer does not point into a null-terminated buffer, `strlen` can read past the end of valid memory (buffer overread). Sonar cpp:S1081 flags potentially unsafe uses of `strlen`.

**Solution:** Use `strlen` only when the pointer is guaranteed to point at a null-terminated string within a known buffer. Otherwise prefer length-aware APIs or document the guarantee.

```cpp
// ❌ RISKY - ptr might not be null-terminated (e.g. slice of a larger buffer)
const char* ptr = GetPointerToSlice();  // No guarantee of '\0' after the slice
size_t len = std::strlen(ptr);         // May overread

// ✅ SAFE - std::string and .c_str() are null-terminated
std::string s = GetString();
size_t len = s.size();                  // Prefer .size()
// or  size_t len = std::strlen(s.c_str());  // Safe: s owns null-terminated storage

// ✅ SAFE - Pointer into storage that is guaranteed null-terminated per entry
// (e.g. path_storage_ where each path is appended with '\0' in AppendString)
const char* path = &path_storage_[path_offsets_[i]];
if (const size_t path_len = std::strlen(path); path_len < old_len) { ... }
// Add: // NOSONAR(cpp:S1081) - Safe: path_storage_ entries are null-terminated (see AppendString)

// ✅ PREFER - When you have or can use length
std::string_view view = GetView();
size_t len = view.size();
// or use a length that was stored/returned with the buffer
```

**When `strlen` is acceptable:**
- Pointer is `std::string::c_str()` or into a `std::string`'s buffer.
- Pointer is into a buffer that your code (or a documented invariant) guarantees is null-terminated (e.g. `path_storage_` with one `'\0'` per logical string).
- Pointer comes from a C API that documents null-terminated output (e.g. `getenv`).

**When to avoid `strlen`:**
- Pointer points into the middle of a buffer or a slice without a guaranteed `'\0'` after it.
- Pointer comes from untrusted or unvalidated input.
- You already have the length (use it; prefer `std::string_view`, `std::string::size()`, or stored length).

**If you must use `strlen` on storage you control:** Ensure every stored string is written with a terminating `'\0'` (e.g. via `strcpy_safe` or explicit `'\0'` after the last character). Document the invariant and add `// NOSONAR(cpp:S1081)` on the same line with a short justification (e.g. "Safe: … is null-terminated (see …)").

**Enforcement:** Sonar cpp:S1081. Before adding or keeping a `strlen` call, confirm the pointer is to a null-terminated string; otherwise use length-aware types or document and suppress with NOSONAR on the same line.

---

### Forward Declaration Type Mismatch (class vs struct)

**Problem:** Forward declarations must match the actual type definition. If a type is defined as `struct`, the forward declaration must also be `struct` (not `class`), and vice versa. MSVC will warn with C4099 on mismatches.

**Solution:** Always match the forward declaration keyword to the actual definition keyword.

```cpp
// ❌ WRONG - Forward declared as class, defined as struct (C4099 warning)
class AppSettings;  // Forward declaration

struct AppSettings {  // Actual definition
  // ...
};

// ✅ CORRECT - Both use struct
struct AppSettings;  // Forward declaration

struct AppSettings {  // Actual definition
  // ...
};
```

**How to Check:**
- Run the detection script: `python3 scripts/find_class_struct_mismatches.py`
- The script scans all C++ files and reports mismatches between forward declarations and definitions
- Use `--verbose` flag to see all forward declarations and definitions

**When to Apply:**
- Before committing header file changes
- When adding new forward declarations
- When refactoring type definitions (changing `class` to `struct` or vice versa)
- When seeing C4099 compiler warnings

**Common Mismatches to Check:**
- `AppSettings` - often forward declared as `class` but defined as `struct`
- `SearchResult` - often forward declared as `class` but defined as `struct`
- `SavedSearch` - should be `struct` in both places
- `CommandLineArgs` - should be `struct` in both places

**Note:** While `class` and `struct` are mostly interchangeable in C++, forward declarations must match exactly. This is a common source of compilation warnings on Windows/MSVC.

---

### Lambda Captures in Template Functions (MSVC Compatibility)

**Problem:** MSVC fails to parse implicit lambda captures (`[&]`, `[=]`) in template function contexts, causing severe compilation errors that cascade to standard library headers.

**Solution:** Always use explicit capture lists for lambdas defined inside template functions.

```cpp
// ❌ WRONG - Causes MSVC compilation errors (C2062, C2059, C2143)
template<typename ResultsContainer>
void ProcessChunkRange(...) {
  auto ProcessBatch = [&](size_t start_idx) {  // MSVC fails to parse this
    // ... use captured variables ...
  };
}

// ✅ CORRECT - Explicit captures work on all compilers including MSVC
template<typename ResultsContainer>
void ProcessChunkRange(...) {
  auto ProcessBatch = [&soaView, &context, &local_results, &items_matched](size_t start_idx) {
    // ... use captured variables ...
  };
}
```

**MSVC Error Messages:**
- `error C2062: type 'void' unexpected`
- `error C2059: syntax error: '}'`
- `error C2143: syntax error: missing ';' before '}'`
- Cascading errors in standard library headers (e.g., `<deque>`)

**When to Apply:**
- **CRITICAL:** Every lambda defined inside a template function must use explicit captures
- When refactoring code that adds lambdas to template functions
- Before committing any code with lambdas in templates (especially performance-critical code)

**Why This Happens:**
MSVC's template parsing is more sensitive to implicit captures than GCC/Clang. The compiler cannot properly resolve what variables are captured by `[&]` or `[=]` in template contexts, leading to parsing failures.

**Related Rule:** See "Explicit Lambda Captures" in Modern C++ Practices section for general guidance on lambda captures.

---

### C++ Standard Include Order

**Problem:** Includes not following standard C++ order can cause compilation issues, make dependencies unclear, and trigger SonarQube warnings.

**Solution:** Always follow the standard C++ include order:

1. `#pragma once` (or header guards)
2. System includes (`<iostream>`, `<string>`, `<memory>`, etc.)
3. Platform-specific system includes (`<windows.h>`, etc.) - if needed
4. Project includes (`"MyClass.h"`, `"Utils.h"`, etc.)
5. Forward declarations (if needed)

```cpp
// ✅ CORRECT - Standard C++ include order
#pragma once

#include <memory>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "MyClass.h"
#include "Utils.h"

// Forward declarations
struct SomeType;
class AnotherType;
```

**When to Apply:**
- Every time you add or modify `#include` directives
- When SonarQube reports include order issues
- When refactoring header files
- Before committing code changes

**Enforcement:**
- Configured in `.clang-tidy` with `readability-include-order` check
- SonarQube rule `cpp:S954` flags includes that should be moved to the top
- Manual review during code review process

**Special Cases:**
- Platform-specific includes (e.g., `<windows.h>`) should be after standard library includes but before project includes
- Forward declarations can be used to reduce compile-time dependencies, but actual includes should still follow the order above
- **All `#include` directives must appear in the top block of the file.** Do not place includes in the middle of the file (e.g. before inline function definitions). Sonar S954 flags includes that should be moved to the top.
- Use **lowercase** for all system and library include paths for portability on case-sensitive systems. Sonar S3806 flags non-portable path case. Examples: `<windows.h>` (not `<Windows.h>`), `<shlobj_core.h>`, `<shobjidl_core.h>` (Windows SDK); avoid mixed-case like `<ShlObj_core.h>`.

---

### C++17 Init-Statement Pattern

**Problem:** Variables declared before `if` statements have broader scope than necessary, reducing code clarity and potentially causing bugs if the variable is accidentally used outside the intended scope.

**Solution:** Use C++17 init-statements to declare variables directly in the `if` condition when the variable is only used within that `if` block.

```cpp
// ❌ WRONG - Variable has broader scope than needed
int width = std::atoi(value);
if (width >= 640 && width <= 4096) {
  args.window_width_override = width;
}

// ✅ CORRECT - Variable scope limited to if statement
if (int width = std::atoi(value); width >= 640 && width <= 4096) {
  args.window_width_override = width;
}
```

**When to Apply:**
- When a variable is declared immediately before an `if` statement
- When the variable is only used within the `if` block (including `else` branches)
- When the variable initialization and condition check are logically related

**When NOT to Apply:**
- When the variable is used after the `if` statement (outside the block)
- When the variable is used in multiple unrelated `if` statements
- When the variable is used in both the `if` block AND after the `if` statement
- When the variable is used in an `else` block AND also used after the `if-else` chain
- When using init-statement would make the code less readable (e.g., very complex conditions)
- **ImGui pattern:** When variables are modified by reference in the condition and then used after the `if` block (common with `ImGui::SliderFloat`, `ImGui::InputInt`, etc.)
- **Exception handling:** When variables are used in catch blocks or exception handling after the if
- **Loop variables:** When variables are used in loop conditions or after loops

**Examples:**
```cpp
// ✅ Good: Single use in if statement
if (size_t pool_thread_count = thread_pool.GetThreadCount(); pool_thread_count == 0) {
  LOG_ERROR_BUILD("Thread pool has 0 threads");
  return;
}

// ✅ Good: Used in else-if chain (same variable, related conditions)
if (std::string validated_name = ValidateAndNormalizeStrategyName(strategy_name); 
    validated_name == "static") {
  return std::make_unique<StaticChunkingStrategy>();
} else if (validated_name == "hybrid") {
  return std::make_unique<HybridStrategy>();
}

// ✅ Good: File operations - variable only used in if block
if (int stderr_fd = open("/tmp/findhelper_stderr.log", O_WRONLY | O_CREAT | O_TRUNC, 0644); stderr_fd != -1) {
  dup2(stderr_fd, STDERR_FILENO);
  close(stderr_fd);
}

// ✅ Good: JSON parsing - variable only used in if block
if (json config_json = json::parse(text); !ParseSearchConfigFromJson(config_json, result)) {
  return result; // Error message already set
}

// ✅ Good: String operations - variable only used in if block
if (size_t last_slash = path.find_last_of("\\/"); last_slash != std::string::npos) {
  directory_path = path.substr(0, last_slash);
  filename = path.substr(last_slash + 1);
}

// ✅ Good: Nested if with init-statement
if (auto it = id_to_path_index_.find(id); it != id_to_path_index_.end()) {
  if (size_t idx = it->second; is_deleted_[idx] == 0) {
    is_deleted_[idx] = 1;
  }
}

// ❌ Don't apply: Variable used after if statement
int width = std::atoi(value);
if (width >= 640 && width <= 4096) {
  args.window_width_override = width;
}
LOG_INFO_BUILD("Width processed: " << width);  // width used here, so keep declaration outside

// ❌ Don't apply: Variable used in multiple unrelated if statements
HRESULT hr = SomeFunction();
if (SUCCEEDED(hr)) {
  // ...
}
// ... other code ...
if (FAILED(hr)) {  // hr used here too
  // ...
}

// ❌ Don't apply: Variable used in else block AND after if
bool has_size_filter = state.sizeFilter != SizeFilter::None;
if ((has_time_filter || has_size_filter) && state.searchActive) {
  // ... use has_size_filter ...
} else {
  // ... use has_size_filter ...
}
size_t displayed_count = has_size_filter ? state.sizeFilteredCount : state.filteredCount;  // Used after if

// ❌ Don't apply: Variable used after if in same scope
uint64_t last_total_time = search_metrics.last_search_time_ms + search_metrics.last_postprocess_time_ms;
if (last_total_time < 1000) {
  ImGui::Text("Search: %llums", static_cast<unsigned long long>(last_total_time));
} else {
  ImGui::Text("Search: %.2fs", last_total_time / 1000.0);  // Used in else AND after if
}

// ❌ Don't apply: ImGui pattern - variable modified by reference, then used after if
float font_size = settings.fontSize;
if (ImGui::SliderFloat("Font Size", &font_size, 10.0f, 24.0f, "%.1f")) {
  settings.fontSize = font_size;  // font_size used here (modified by ImGui)
  changed = true;
}
// Note: font_size is modified by ImGui::SliderFloat via reference parameter,
// then assigned back to settings. This is a false positive from SonarCloud.
// The variable MUST be declared outside the if statement.
```

**Benefits:**
- Reduces variable scope, preventing accidental use outside intended context
- Improves code clarity by showing the variable is only relevant within the condition
- Aligns with C++17 best practices and modern C++ style
- Reduces SonarCloud code smell warnings (cpp:S6004)

**Common Patterns to Apply:**
- File I/O operations: `if (int fd = open(...); fd != -1) { ... }`
- JSON parsing: `if (json obj = json::parse(text); !obj.empty()) { ... }`
- String operations: `if (size_t pos = str.find(':'); pos != std::string::npos) { ... }`
- Map/container lookups: `if (auto it = map.find(key); it != map.end()) { ... }`
- Environment variables: `if (const char* home = std::getenv("HOME"); home != nullptr) { ... }`
- Windows API calls: `if (DWORD result = GetModuleFileNameA(...); result > 0) { ... }`

**SonarQube Note:**
- SonarQube rule `cpp:S6004` flags variables that could use init-statements
- Many flagged issues may already use init-statements (false positives from stale data)
- Always verify the actual code before applying fixes
- If a variable is legitimately used after the if block, the SonarQube warning is a false positive

**Enforcement:**
- **SonarQube check:** `cpp:S6004` automatically flags variables that could use init-statements
- **Code review:** Always check for variables declared before `if` statements that are only used within the `if` block
- **Manual verification:** Before applying init-statement pattern, verify the variable is not used after the `if` block
- **Related patterns:** For complex boolean logic, consider `readability-use-anyofallof` clang-tidy check (for `std::any_of`, `std::all_of` patterns)

**Real-World Example (from GeminiApiUtils.cpp):**
```cpp
// ❌ Before: Variable declared before if, only used within if
const std::string ext_pattern = std::string("/") + "*." + ext;
if (TryRemoveExtensionPattern(path_content, ext, ext_pattern)) {
  return true;
}

// ✅ After: Using init-statement pattern
if (std::string ext_pattern = std::string("/") + "*." + ext; TryRemoveExtensionPattern(path_content, ext_pattern)) {
  return true;
}
```

**Action:** When writing new code or modifying existing code, prefer init-statements for variables that are only used within a single `if` statement or related `if-else` chain. Always verify the variable is not used after the if block before applying the pattern.

---

## Modern C++ Practices

This section enforces modern C++17 practices to avoid common SonarQube violations and improve code quality, safety, and maintainability.

For a concise overview of C++17 language and library features we rely on, see Anthony Calandra’s **modern-cpp-features – C++17** reference: `https://github.com/AnthonyCalandra/modern-cpp-features/blob/master/CPP17.md`. Treat it as background reading only; the rules in this `AGENTS.md` file are the authoritative guardrails for this project.

### Memory Management: Use RAII, Not Manual Memory Management

**Problem:** Manual memory management (`malloc`/`free`, `new`/`delete`) is error-prone, can cause memory leaks, and violates modern C++ principles.

**Solution:** Always use RAII (Resource Acquisition Is Initialization) with smart pointers and standard containers.

```cpp
// ❌ WRONG - Manual memory management
char* buffer = (char*)malloc(size);
// ... use buffer ...
free(buffer);  // Easy to forget, leaks on exceptions

// ❌ WRONG - Manual delete
int* data = new int[100];
// ... use data ...
delete[] data;  // Easy to forget, leaks on exceptions

// ✅ CORRECT - Use std::vector (automatic RAII)
std::vector<char> buffer(size);
// ... use buffer.data() ...
// Automatic cleanup, exception-safe

// ✅ CORRECT - Use std::unique_ptr for dynamic arrays
auto data = std::make_unique<int[]>(100);
// ... use data.get() ...
// Automatic cleanup, exception-safe

// ✅ CORRECT - Use std::unique_ptr with custom deleter for C APIs
struct EnvKeyDeleter {
  void operator()(char* ptr) const noexcept {
    if (ptr != nullptr) free(ptr);
  }
};
using EnvKeyPtr = std::unique_ptr<char, EnvKeyDeleter>;
EnvKeyPtr env_key(allocated_by_c_api);
// Automatic cleanup via custom deleter
```

**When to Apply:**
- Every time you need dynamic memory allocation
- When interfacing with C APIs that return raw pointers (use `unique_ptr` with custom deleter)
- When replacing existing `malloc`/`free` or `new`/`delete` code

**Benefits:**
- Automatic cleanup, even on exceptions
- No memory leaks
- Type-safe memory management
- Aligns with C++17 best practices

---

### Const Correctness

**Problem:** Missing `const` qualifiers reduce code safety, make APIs unclear, and prevent compiler optimizations.

**Solution:** Apply `const` consistently throughout your code.

#### 1. Const Member Functions

```cpp
// ❌ WRONG - Function doesn't modify state but isn't const
class FileIndex {
  size_t GetSize() {  // Should be const
    return files_.size();
  }
};

// ✅ CORRECT - Mark non-modifying functions as const
class FileIndex {
  size_t GetSize() const {  // Now const-correct
    return files_.size();
  }
};
```

#### 2. Const Parameters

```cpp
// ❌ WRONG - Parameter isn't modified but isn't const
void ProcessData(std::string& data) {  // Should be const&
  std::cout << data.length();
}

// ✅ CORRECT - Use const reference for read-only parameters
void ProcessData(const std::string& data) {
  std::cout << data.length();
}

// ❌ WRONG - Pointer parameter isn't modified but isn't const
void PrintPath(char* path) {  // Should be const char*
  std::cout << path;
}

// ✅ CORRECT - Use const pointer for read-only parameters
void PrintPath(const char* path) {
  std::cout << path;
}
```

**When to Apply:**
- Member functions that don't modify object state → add `const`
- Function parameters that are only read → use `const T&` or `const T*`
- Local variables that aren't modified → use `const`

**Benefits:**
- Clearer API contracts (const = won't modify)
- Compiler can optimize better
- Prevents accidental modifications
- Better code documentation

---

### Prefer Braced Initializer List in Return Statements (modernize-return-braced-init-list)

**Problem:** Repeating the return type in a `return` statement (e.g. `return std::string(...);` or `return Type{...};`) duplicates the declared type and triggers clang-tidy `modernize-return-braced-init-list`. If the return type changes, both the declaration and the return must be updated.

**Solution:** Use a braced initializer list in the return so the type is deduced from the function's declared return type.

```cpp
// ❌ WRONG - Repeats return type (triggers modernize-return-braced-init-list)
std::string GetApiKey() {
  if (const char* key = std::getenv("API_KEY"); key != nullptr) {
    return std::string(key);
  }
  return std::string();
}

// ✅ CORRECT - Braced initializer list; type comes from declaration
std::string GetApiKey() {
  if (const char* key = std::getenv("API_KEY"); key != nullptr) {
    return {key};
  }
  return {};
}
```

```cpp
// ❌ WRONG - Repeats type in return
std::string_view AsView() const { return std::string_view(buffer_.data()); }
std::string AsString() const { return std::string(buffer_.data()); }

// ✅ CORRECT - Braced init list
std::string_view AsView() const { return {buffer_.data()}; }
std::string AsString() const { return {buffer_.data()}; }
```

**When to Apply:**
- When writing or modifying `return` statements that explicitly name the return type (e.g. `return std::string();`, `return std::pair<int, bool>(a, b);`).
- Prefer `return { ... };` or `return {};` so the type is not repeated.

**When NOT to Apply:**
- When the return expression is not a simple construction (e.g. `return std::string(a) + b;` keeps the expression; only the redundant `return Type(...)` form is in scope for this rule).

**Enforcement:**
- **clang-tidy check:** `modernize-return-braced-init-list` is enabled in `.clang-tidy`.
- **Code review:** Prefer braced init in returns to avoid duplicate type names.

**Benefits:**
- Single source of truth for the return type (DRY).
- Easier refactoring when the return type changes.
- Consistent, modern C++ style.

---

### Use `std::string_view` Instead of `const std::string&`

**Problem:** `const std::string&` forces unnecessary string allocations when passing string literals or `char*`.

**Solution:** Use `std::string_view` for read-only string parameters (C++17 feature).

```cpp
// ❌ WRONG - Forces allocation for string literals
void ProcessPath(const std::string& path) {
  // ...
}
ProcessPath("C:\\Users\\File.txt");  // Allocates temporary string

// ✅ CORRECT - No allocation, works with string literals, std::string, char*
void ProcessPath(std::string_view path) {
  // ... use path.data(), path.length(), etc.
}
ProcessPath("C:\\Users\\File.txt");  // No allocation
ProcessPath(std::string("..."));      // Works with std::string too
ProcessPath(char_ptr);                // Works with char* too
```

**When to Apply:**
- Function parameters that only read string data (don't need to modify or store)
- When the function doesn't need to own the string
- Performance-critical paths where avoiding allocations matters

**When NOT to Apply:**
- When you need to store the string (use `const std::string&` or `std::string` by value)
- When you need to modify the string (use `std::string&`)
- When interfacing with APIs that require `std::string` (use `const std::string&`)

**Benefits:**
- Better performance (no unnecessary allocations)
- More flexible (works with string literals, `std::string`, `char*`)
- Modern C++17 best practice

**Enforcement:**
- **clang-tidy check:** `modernize-use-string-view` is enabled in `.clang-tidy`
  - Automatically flags `const std::string&` parameters that could use `std::string_view`
  - Run `clang-tidy` on modified files before committing
- **Pre-commit hook:** The pre-commit hook (`scripts/pre-commit-clang-tidy.sh`) will catch these issues
- **Code review:** Always prefer `std::string_view` for read-only string parameters during review
- **Priority:** High - This is a performance and modern C++ best practice

**Action:** When writing or modifying functions, prefer `std::string_view` over `const std::string&` for read-only string parameters. Review existing code and refactor when appropriate.

---

### Prefer `auto` When Type Is Obvious

**Problem:** Repeating a long or redundant type name (e.g. from a cast or iterator) adds noise and can get out of sync when the right-hand side type changes.

**Solution:** Prefer `auto` when the type is clear from the initializer (e.g. iterator from `find`, result of `static_cast`, `make_unique`/`make_shared`, range-for).

```cpp
// ❌ WRONG - Redundant type; clang-tidy modernize-use-auto / hicpp-use-auto
const int64_t total_seconds = static_cast<int64_t>(duration.count());
std::unordered_map<Key, Value>::iterator it = map.find(key);

// ✅ CORRECT - Type obvious from initializer
const auto total_seconds = static_cast<int64_t>(duration.count());
auto it = map.find(key);
```

**When to Apply:**
- Iterator and container `find`/`begin`/`end` results
- Results of `static_cast`/`dynamic_cast`/`const_cast` when the cast already names the type
- `make_unique`/`make_shared` and similar factory returns
- Range-for: `for (const auto& item : container)`
- Complex nested or template types where the type is evident from context

**When NOT to Apply:**
- When the type is important for readability (e.g. numeric precision: `int32_t` vs `int64_t` in arithmetic)
- When brace-initialization would deduce the wrong type (e.g. `auto x = {1, 2};` is `std::initializer_list<int>`)
- When an explicit type documents an invariant (e.g. storing a size in a specific width type)

**Enforcement:**
- **clang-tidy checks:** `modernize-use-auto` and `hicpp-use-auto` are enabled in `.clang-tidy`.
- **Sonar S5827:** "Replace the redundant type with auto" — same intent; use `auto` when the type is redundant from the initializer.
- **Code review:** Prefer `auto` when the type is obvious from the initializer; avoid redundant long type names.

**Action:** When writing or modifying declarations, use `auto` where the type is clear from the right-hand side. Fix `modernize-use-auto` / `hicpp-use-auto` warnings when touching the surrounding code.

---

### Handle Unused Parameters (cpp:S1172)

**Problem:** Unused parameters clutter function signatures, make APIs unclear, and can indicate incomplete implementations or API design issues.

**Solution:** Always mark unused parameters with `[[maybe_unused]]` or remove them entirely.

```cpp
// ❌ WRONG - Unused parameter without annotation
void ProcessData(const std::string& data, int unused_flag) {
  // data is used, but unused_flag is never used
  Process(data);
}

// ✅ CORRECT - Mark unused parameter explicitly
void ProcessData(const std::string& data, [[maybe_unused]] int unused_flag) {
  // Parameter kept for API compatibility but not used
  Process(data);
}

// ✅ CORRECT - Remove unused parameter if not needed
void ProcessData(const std::string& data) {
  Process(data);
}

// ✅ CORRECT - Unnamed parameter (C++17)
void ProcessData(const std::string& data, int /*unused_flag*/) {
  Process(data);
}
```

**When to Apply:**
- When a parameter is required by an interface but not used in the implementation
- When a parameter is kept for backward compatibility but no longer needed
- When refactoring code and discovering unused parameters

**When to Keep:**
- When the parameter is part of a public API that external code depends on
- When the parameter is part of a callback/function pointer signature that must match
- When the parameter will be used in future implementations (document with TODO)

**Parameter Handling Options:**
1. **Remove the parameter** - Best option if not needed and API can be changed
2. **Mark with `[[maybe_unused]]`** - C++17 attribute, preferred for modern code
3. **Unnamed parameter** - Use `/*comment*/` or just omit the name: `void func(int)`
4. **Comment the parameter** - Use `int /*unused*/` for clarity

**Enforcement:**
- **clang-tidy check:** `readability-unused-parameters` is enabled in `.clang-tidy` (via `misc-unused-*`)
  - Automatically flags unused parameters
  - Run `clang-tidy` on modified files before committing
- **Compiler warning:** Consider using `-Wunused-parameter` in debug builds
- **Pre-commit hook:** The pre-commit hook (`scripts/pre-commit-clang-tidy.sh`) will catch these issues
- **Code review:** Always check for unused parameters during review

**Benefits:**
- Cleaner function signatures
- Clearer API intent
- Prevents confusion about parameter purpose
- Helps identify incomplete implementations

**Action:** Review all function parameters - remove unused ones or mark them with `[[maybe_unused]]`. This prevents SonarQube violations and improves code clarity.

---

### Explicit Lambda Captures

**Problem:** Implicit lambda captures (`[&]`, `[=]`) can accidentally capture more than intended, leading to bugs and unclear dependencies.

**Solution:** Always use explicit capture lists.

```cpp
// ❌ WRONG - Implicit capture, unclear what's captured
int x = 10;
auto lambda = [&]() {  // Captures everything by reference
  return x + y;  // What is y? Is it captured?
};

// ✅ CORRECT - Explicit capture, clear dependencies
int x = 10;
int y = 20;
auto lambda = [&x, y]() {  // Explicit: x by ref, y by value
  return x + y;
};
```

**When to Apply:**
- Every lambda expression
- Replace all `[&]` and `[=]` with explicit capture lists
- **CRITICAL:** When refactoring code that uses lambdas, verify captures are explicit
- **CRITICAL (MSVC):** Implicit captures in template contexts can cause MSVC compilation errors (C2062, C2059, C2143) and cascade to standard library headers. Always use explicit captures, especially in template functions.

**MSVC Compatibility Issue:**
MSVC can fail to parse implicit lambda captures (`[&]`, `[=]`) in complex template contexts, leading to severe compilation errors:
- `error C2062: type 'void' unexpected`
- `error C2059: syntax error: '}'`
- `error C2143: syntax error: missing ';' before '}'`
- Cascading errors in standard library headers (e.g., `<deque>`)

These errors occur because MSVC's template parsing is more sensitive to implicit captures than GCC/Clang. The solution is to always use explicit capture lists, even when the lambda is simple.

**Example of MSVC Failure:**
```cpp
// ❌ WRONG - Causes MSVC compilation errors in template functions
template<typename ResultsContainer>
void ProcessChunkRange(...) {
  auto ProcessBatch = [&](size_t start_idx) {  // MSVC fails here
    // ... use captured variables ...
  };
}

// ✅ CORRECT - Explicit captures work on all compilers
template<typename ResultsContainer>
void ProcessChunkRange(...) {
  auto ProcessBatch = [&soaView, &context, &local_results, &items_matched](size_t start_idx) {
    // ... use captured variables ...
  };
}
```

**Common Violations:**
- Template helper functions that accept lambdas (e.g., `TryAVX2Path`, `TryAVX2PathStrStr`)
- Callback functions passed to utility functions
- Lambda expressions in performance-critical code (e.g., string search)

**Example from StringSearch.h:**
```cpp
// ❌ WRONG - Implicit capture
if (string_search_detail::TryAVX2Path(text, pattern, [&]() {
  return string_search::avx2::ContainsSubstringAVX2(text, pattern);
})) {
  return true;
}

// ✅ CORRECT - Explicit capture
if (string_search_detail::TryAVX2Path(text, pattern, [&text, &pattern]() {
  return string_search::avx2::ContainsSubstringAVX2(text, pattern);
})) {
  return true;
}
```

**Benefits:**
- Prevents accidental captures
- Makes lambda dependencies explicit
- Easier to understand and maintain
- Prevents bugs from unintended captures
- SonarQube compliance (cpp:S3608)

**Action:** 
- Before committing code with lambdas, verify all captures are explicit
- Search for `[&]` and `[=]` patterns and replace with explicit captures
- **CRITICAL for Windows builds:** Implicit captures in templates will cause MSVC compilation failures - always use explicit captures in template functions
- When adding lambdas inside template functions, immediately use explicit captures to prevent MSVC build errors

---

### Specific Exception Handling

**Problem:** Generic exception catches (`catch (...)`, `catch (std::exception&)`) hide specific error types and make debugging harder.

**Solution:** Catch specific exception types when possible.

```cpp
// ❌ WRONG - Too generic, loses specific error information
try {
  // ...
} catch (std::exception& e) {  // Too generic
  // Can't distinguish between different error types
}

// ❌ WRONG - Catches everything, including system exceptions
try {
  // ...
} catch (...) {  // Too broad, dangerous
  // Can't access exception information
}

// ✅ CORRECT - Catch specific exceptions
try {
  // ...
} catch (const std::invalid_argument& e) {
  // Handle invalid argument specifically
} catch (const std::runtime_error& e) {
  // Handle runtime errors
} catch (const std::exception& e) {
  // Catch-all for other standard exceptions
}
```

**When to Apply:**
- When you know what specific exceptions can be thrown
- When you need different handling for different error types
- Replace generic `catch (...)` with specific types when possible

**When to Keep Generic:**
- When interfacing with unknown third-party code
- When you truly need to catch all exceptions (rare)
- Always document why generic catch is necessary

**Benefits:**
- Better error handling and debugging
- More specific error messages
- Clearer code intent
- Prevents catching unexpected exceptions

---

### Avoid C-Style Casts

**Problem:** C-style casts (`(type)value`) are unsafe, can hide bugs, and make code harder to understand.

**Solution:** Use C++ cast operators: `static_cast`, `const_cast`, `reinterpret_cast`, `dynamic_cast`.

```cpp
// ❌ WRONG - C-style cast, unsafe and unclear
int* ptr = (int*)void_ptr;  // What kind of cast is this?

// ✅ CORRECT - Use appropriate C++ cast
int* ptr = static_cast<int*>(void_ptr);  // Clear intent: static conversion
const int* cptr = const_cast<const int*>(ptr);  // Clear: removing const
Base* base = dynamic_cast<Base*>(derived);  // Clear: runtime type check
```

**When to Apply:**
- Replace all C-style casts with appropriate C++ casts
- `static_cast`: Safe conversions (int to float, base to derived when safe)
- `const_cast`: Removing const/volatile (use sparingly, document why)
- `dynamic_cast`: Runtime type checking (polymorphic types)
- `reinterpret_cast`: Low-level bit reinterpretation (rare, document why)

**Benefits:**
- Type-safe conversions
- Clearer code intent
- Compiler can catch more errors
- Easier to search for dangerous casts

---

### Use `explicit` for Conversion Operators and Single-Argument Constructors (cpp:S1709)

**Problem:** Implicit conversions can cause unexpected behavior and bugs. Single-argument constructors (or constructors where all but one parameter have defaults) allow implicit conversion from the argument type.

**Solution:** Mark conversion operators and single-argument constructors as `explicit` to prevent implicit conversions.

```cpp
// ❌ WRONG - Implicit conversion from size_t
class StreamingResultsCollector {
public:
  StreamingResultsCollector(size_t batch_size = 500, uint32_t notification_interval_ms = 50);
};
StreamingResultsCollector c = 100;  // Surprising: implicitly converts 100 to batch_size

// ✅ CORRECT - Explicit constructor
class StreamingResultsCollector {
public:
  explicit StreamingResultsCollector(size_t batch_size = 500, uint32_t notification_interval_ms = 50);
};
StreamingResultsCollector c = 100;  // ❌ Compile error
StreamingResultsCollector c(100);   // ✅ Explicit call
```

```cpp
// ❌ WRONG - Implicit conversion can cause surprises
class FileHandle {
  operator bool() const {  // Implicit conversion
    return handle_ != nullptr;
  }
};

// ✅ CORRECT - Explicit conversion operator
class FileHandle {
  explicit operator bool() const {  // Explicit conversion
    return handle_ != nullptr;
  }
};
```

**When to Apply:**
- Single-argument constructors, or constructors where all parameters except one have defaults (Sonar S1709)
- All conversion operators (`operator bool()`, etc.)
- Prevents accidental implicit conversions

**Benefits:**
- Prevents unexpected conversions
- Makes code intent explicit
- Reduces bugs from implicit conversions

---

### Prefer `std::scoped_lock` Over `std::lock_guard` (cpp:S5997, cpp:S6012)

**Problem:** `std::lock_guard` with an explicit template argument (e.g. `std::lock_guard<std::mutex>`) is redundant when C++17 `std::scoped_lock` with class template argument deduction (CTAD) can be used. Sonar S5997 and S6012 flag this.

**Solution:** Use `std::scoped_lock` when locking a single mutex, and rely on CTAD so no template arguments are needed.

```cpp
// ❌ WRONG - lock_guard with explicit template argument
std::lock_guard<std::mutex> lock(mutex_);

// ✅ CORRECT - scoped_lock with CTAD (no template args)
std::scoped_lock lock(mutex_);
```

**When to Apply:**
- When locking a single mutex (replace `std::lock_guard<std::mutex>` with `std::scoped_lock`)
- For multiple mutexes, `std::scoped_lock` already supports deadlock-free locking; use the same pattern with CTAD

**When NOT to Apply:**
- When the codebase or platform requires C++14 or earlier (scoped_lock is C++17)

**Enforcement:**
- SonarQube rules cpp:S5997 (replace lock_guard with scoped_lock) and cpp:S6012 (avoid explicit template arguments; use CTAD)

---

### Use `[[nodiscard]]` for Important Return Values

**Problem:** Functions that return important values (error codes, results) can be accidentally ignored.

**Solution:** Use `[[nodiscard]]` attribute to warn when return values are ignored.

```cpp
// ❌ WRONG - Return value can be silently ignored
bool SaveFile(const std::string& path) {
  // ... save logic ...
  return success;
}

SaveFile("data.txt");  // ❌ Ignored return value - bug!

// ✅ CORRECT - Compiler warns if return value is ignored
[[nodiscard]] bool SaveFile(const std::string& path) {
  // ... save logic ...
  return success;
}

SaveFile("data.txt");  // ⚠️ Compiler warning: ignoring nodiscard return value
if (SaveFile("data.txt")) { }  // ✅ Correct usage
```

**When to Apply:**
- Functions that return error codes or status
- Functions that return important results that shouldn't be ignored
- Constructors that return handles or resources

**Benefits:**
- Prevents bugs from ignored return values
- Makes important return values explicit
- Compiler enforces proper usage

---

### Prefer `std::array` Over C-Style Arrays

**Problem:** C-style arrays are less safe, don't know their size, and don't work well with modern C++.

**Solution:** Use `std::array` for fixed-size arrays.

```cpp
// ❌ WRONG - C-style array, no size information, unsafe
int buffer[100];
// Can't get size easily, can't use with STL algorithms easily

// ✅ CORRECT - std::array, knows its size, STL-compatible
std::array<int, 100> buffer;
buffer.size();  // Knows its size
std::fill(buffer.begin(), buffer.end(), 0);  // Works with STL
```

**When to Apply:**
- Fixed-size arrays
- When you know the size at compile time
- Replace C-style arrays with `std::array`

**Benefits:**
- Type-safe
- Knows its size
- Works with STL algorithms
- No pointer decay

---

### Remove Redundant Casts (cpp:S1905)

**Problem:** Redundant casts clutter code and can hide type issues. Sonar S1905 flags casts that don't change the type.

**Solution:** Remove casts that don't change the type or are unnecessary.

```cpp
// ❌ WRONG - Redundant cast
int value = static_cast<int>(42);  // 42 is already int

// ✅ CORRECT - No cast needed
int value = 42;

// ❌ WRONG - Redundant cast in template context
template<typename T>
void func(T value) {
  T result = static_cast<T>(value);  // value is already T
}

// ✅ CORRECT - No cast needed
template<typename T>
void func(T value) {
  T result = value;
}
```

**When to Apply:**
- Remove casts that don't change the type
- Simplify code by removing unnecessary casts

**Benefits:**
- Cleaner code
- Easier to read
- Compiler can optimize better

**Enforcement:** Sonar S1905. Before committing, remove unnecessary `static_cast`/C-style casts when the expression already has the target type.

---

### Keep Lambdas Short (cpp:S1188)

**Problem:** Lambdas longer than ~20 lines are hard to read and test. Sonar S1188 flags lambdas that exceed the authorized line count.

**Solution:** If a lambda is longer than 20 lines, extract it to a named function or split into smaller lambdas.

```cpp
// ❌ WRONG - Lambda too long (>20 lines)
auto callback = [&]() {
  // ... 25+ lines ...
};

// ✅ CORRECT - Named function or shorter lambda
void OnCallback() { /* ... */ }
auto callback = [this]() { OnCallback(); };
```

**When to Apply:** When adding or refactoring lambdas; before committing test or UI code that uses large lambdas (e.g. ImGui test engine).

---

### Use std::size for Array Size (cpp:S7127)

**Problem:** Using `sizeof(arr)/sizeof(arr[0])` or manual size for arrays is error-prone and doesn't work with decayed pointers. Sonar S7127 requires using `std::size` for array size.

**Solution:** Use `std::size(arr)` for arrays and standard containers (C++17).

```cpp
// ❌ WRONG - Manual size
int arr[10];
for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); ++i) { }

// ✅ CORRECT - std::size
std::array<int, 10> arr;
for (size_t i = 0; i < std::size(arr); ++i) { }
```

**When to Apply:** Whenever you need the size of an array or std::array; prefer `std::array` + `std::size` over C-style arrays.

---

### Remove Unused Functions (cpp:S1144)

**Problem:** Unused functions add noise and can mislead maintainers. Sonar S1144 flags unused functions.

**Solution:** Remove functions that are never called, or use them. If kept for API/forward compatibility, document why and consider `[[maybe_unused]]` or a single use (e.g. in tests).

```cpp
// ❌ WRONG - Unused function left in place
static void RenderPopupCloseButton() { /* ... */ }  // Never called

// ✅ CORRECT - Remove or use
// (Remove RenderPopupCloseButton, or call it from the popup rendering path.)
```

**When to Apply:** Before committing; when refactoring, remove or repurpose functions that no longer have callers. See also Code Quality Checklist: "Dead Code: Remove unused functions".

---

### Prefer Private Members; Avoid protected Data (cpp:S3656)

**Problem:** Protected member variables broaden the coupling surface and make invariants harder to enforce. Sonar S3656 flags protected data members.

**Solution:** Prefer private members; use protected only when you intentionally expose state to derived classes and document the contract. Consider accessors or template method pattern instead.

**When to Apply:** When adding or refactoring class members; when reviewing inheritance hierarchies (e.g. COM-style or Windows API wrappers where protected may be required, document with NOSONAR if justified).

---

### Rule of Five (cpp:S3624)

**Problem:** If a class manages resources (custom destructor, copy/move), omitting or defaulting copy/move constructor or assignment can lead to double-free or leaks. Sonar S3624 flags incomplete resource management.

**Solution:** If you define or delete any of destructor, copy constructor, copy assignment, move constructor, move assignment, consider all five (Rule of Five). Prefer `= default` or `= delete` where appropriate; use RAII types (e.g. `std::unique_ptr`) to avoid manual resource code.

**When to Apply:** When adding a custom destructor or copy/move operations; when wrapping C/COM APIs that use raw pointers (prefer smart pointers with custom deleters).

---

### Use inline for File-Scope Variables (cpp:S6018)

**Problem:** Pre-C++17, file-scope variables required a separate definition in a .cpp file to avoid ODR issues. Sonar S6018 recommends inline variables for globals (C++17).

**Solution:** For constants or single-instance objects at namespace or file scope, prefer `inline` or `inline constexpr` where C++17 is available.

```cpp
// ❌ AVOID - External definition required in .cpp
// header
extern int kGlobal;

// ✅ CORRECT (C++17) - inline variable
inline constexpr int kGlobal = 42;
```

**When to Apply:** When adding or refactoring file-scope or namespace-scope variables in C++17 code.

---

### Prefer std::remove_pointer_t (cpp:S6020)

**Problem:** The `typename std::remove_pointer<T>::type` pattern is verbose and C++14 provides `std::remove_pointer_t<T>`. Sonar S6020 flags the old form.

**Solution:** Use `std::remove_pointer_t<T>` instead of `typename std::remove_pointer<T>::type`.

**When to Apply:** When writing or refactoring template code that strips pointer from a type.

---

## Coding Conventions

### Naming Conventions

**Follow the naming conventions defined in:** `docs/standards/CXX17_NAMING_CONVENTIONS.md`

**Quick Reference:**
- **Classes/Structs:** `PascalCase` (e.g., `UsnMonitor`, `FileIndex`)
- **Functions/Methods:** `PascalCase` (e.g., `StartMonitoring()`, `GetSize()`)
- **Local Variables:** `snake_case` (e.g., `buffer_size`, `offset`)
- **Member Variables:** `snake_case_` with trailing underscore (e.g., `file_index_`, `reader_thread_`)
- **Global Variables:** `g_snake_case` with `g_` prefix (e.g., `g_file_index`)
- **Constants:** `kPascalCase` with `k` prefix (e.g., `kBufferSize`, `kMaxQueueSize`)
- **Namespaces:** `snake_case` (e.g., `find_helper`, `file_operations`)

**Action:** Before submitting code, verify all identifiers follow these conventions. See `docs/standards/CXX17_NAMING_CONVENTIONS.md` for complete details and examples.

---

## Core Development Principles

### 1. Boy Scout Rule (Leave Code Better Than You Found It)

**Definition:** When modifying code, improve it slightly even if the improvement is unrelated to your immediate task.

**How to Apply:**
- Fix obvious bugs or issues you encounter
- Remove dead code you discover
- Improve variable names for clarity
- Add missing const correctness
- Fix formatting inconsistencies
- Simplify complex expressions
- Add brief comments where logic is non-obvious

**Example:**
```cpp
// Before: You're adding a new feature, but notice this:
void ProcessData(int* data, int size) {
  for (int i = 0; i < size; i++) {  // ❌ Inconsistent style, no const
    // ...
  }
}

// After: Apply Boy Scout Rule while making your change
void ProcessData(const int* data, size_t size) {  // ✅ Added const, better type
  for (size_t i = 0; i < size; ++i) {  // ✅ Consistent style, pre-increment
    // ...
  }
}
```

**Requirement:** When applying the Boy Scout Rule, **explain in your response** what improvements you made and why.

### 2. DRY (Don't Repeat Yourself)

**Principle:** Eliminate code duplication. One source of truth per concept.

**Action:** If you find yourself copying code, extract it into a reusable function, class, or utility.

#### Undesired Duplication of Constants

**Do not** define the same constant (same value or same semantic) in multiple places. That leads to drift when one copy is updated and others are forgotten.

- **Before adding a new constant:** Search the repo for the same numeric value or the same concept (e.g. "max recent searches", "window width max"). If it already exists, reuse it or move it to a shared location and use it everywhere.
- **One constant per purpose:** Storage limit and display limit for the same thing (e.g. recent searches) must use a single constant. Validation bounds (min/max) used in multiple files (e.g. Settings, UI, command-line) must use a single constant.
- **Where to define shared constants:**
  - **App/settings defaults and bounds:** `settings_defaults` in `core/Settings.h` (window size, recrawl interval, recent list size, etc.).
  - **FILETIME / time conversion:** `file_time_constants` in `utils/FileTimeTypes.h` (e.g. Windows vs Unix epoch difference).
  - **File-system / attribute helpers:** `file_system_utils_constants` in `utils/FileSystemUtils.h` where appropriate.
- **Same file:** If the same value is used in more than one place in a single file, define one named constant at file or namespace scope and use it everywhere in that file (e.g. do not repeat `300` for the same debounce in two functions).

See **`docs/analysis/2026-02-20_DRY_CONSTANTS_ANALYSIS.md`** for examples of consolidations and remaining opportunities.

#### Other Types of Duplication to Avoid

- **Magic numbers:** Use named constants instead of repeating the same literal (e.g. `4096`, `60`) in multiple call sites. Search for the value; if it already has a name elsewhere, reuse it.
- **String literals:** UI labels, error messages, tooltips, and help text that say the same thing should not be duplicated with small variations. Reuse a single string or constant, or centralize in one place (e.g. a message or UI constants header) so wording stays in sync.
- **Repeated code blocks:** Identical or near-identical sequences of statements in multiple branches or files should be extracted into a function or shared helper. Same validation logic in multiple places should live in one function and be called from all call sites.
- **Documentation:** Do not repeat the same explanation in multiple files. Prefer one canonical place (e.g. a design doc or the main header) and reference it (e.g. "See AGENTS.md §…" or "See MyModule.h for the contract") instead of copying paragraphs.
- **Type aliases:** When a type is shared across headers, define one canonical alias (e.g. in a central header) and reuse it; do not introduce a second alias for the same type (see "Shared Alias Consistency Across Headers" above).

### 3. Single Responsibility Principle

**Apply at three levels:**

1. **Classes:** Each class should have one clear purpose
2. **Functions:** Each function should do one thing well
3. **Files:** Each file should have a focused, cohesive purpose

**Benefit:** Easier to understand, test, and maintain.

### 4. Performance Priority

**Guideline:** Favor execution speed over memory usage.

**Rationale:** This is a performance-critical Windows application. Optimize for speed unless memory constraints are severe.

### 5. Simplicity Over Complexity

**Guideline:** Favor simple and readable code over clever or complex solutions.

**Test:** If you need to explain complex code with extensive comments, consider simplifying it.

### 6. Add Assertions for Debug Builds

**ALWAYS** add assertions to document and verify invariants:

1. **Preconditions**: Check inputs at function entry.
2. **Loop Invariants**: Assert at top/bottom of loops (e.g., sum parity, bounds).
3. **Postconditions**: Verify outputs/ state changes.
4. **State Transitions**: Guard data mutations.

### 7. Type Aliases for Readable, Consistent Types

**Always use `using` aliases for complex, nested, or frequently used types** to improve clarity and maintainability. Prefer placing type aliases in the relevant header file or in a common `types.h` for shared use.

#### Type Alias Naming Conventions
- Use **PascalCase** (e.g., `UserId`, `StringMap`, `PathOffsetVec`) for all type aliases (see: `docs/standards/CXX17_NAMING_CONVENTIONS.md`).
- Make names descriptive and concise: prefer `ExtensionSet` over `ExtSet` or single-letter names.
- Use **precision suffixes** (`Ptr`, `Ref`, `Vec`, `Iter`, `ConstIter`) as needed for clarity, especially with pointers, containers, or iterator types.

#### Examples
```cpp
using UserId = std::uint64_t;
using PathList = std::vector<std::string>;
using ExtensionSet = std::unordered_set<std::string>;
using PathOffsetVec = std::vector<size_t>;
using StringPoolPtr = std::shared_ptr<StringPool>;
using FileEntryMap = std::unordered_map<uint64_t, FileEntry>;

template <typename T>
using VecIter = typename std::vector<T>::iterator;

template <typename Container>
using ValueType = typename Container::value_type;
```

#### Placement and Scope
- Place type aliases at **namespace scope** in headers for widespread reuse.
- For types local to only one source file, define aliases near their usage and keep their scope limited.
- **Document the intent** of aliases if their usage or necessity isn't obvious, especially for template or pointer aliases.

#### Shared Alias Consistency Across Headers

- When a type alias is used across multiple headers (for example an `ExtensionSet` shared by `SearchContext` and `SearchPatternUtils`), define a **single canonical alias** in the most central header and reuse that name everywhere else, rather than re‑introducing ad‑hoc aliases like `extension_set_t` in other files.
- When you rename or fix a shared alias (including clang-tidy naming changes), **search for and update all its usages in the codebase in the same change** so that all translation units (including Windows builds) stay in sync and you avoid "unknown type name" errors on other platforms.

Always follow these guidelines for all PRs. Enforce formatting and naming with `clang-format` and project linters.

---

## Remove Unused Backward Compatibility Code

**Problem:** Code kept for "backward compatibility" that isn't actually used adds maintenance overhead, increases code complexity, and can mislead developers about what APIs are available.

**Solution:** Remove backward compatibility code that isn't actually used. This project has no external consumers, so backward compatibility is not needed.

**When to Remove:**
- Code marked as "kept for backward compatibility" but not actually called anywhere
- Legacy methods/constants that have been replaced by newer APIs
- Deprecated flags or fields that are no longer read or written
- Overloads marked "for backward compatibility" that aren't used

**How to Verify:**
1. Search the codebase for actual usage of the backward compatibility code
2. Check if any external code (tests, examples) uses it
3. If unused, remove it (don't just mark as deprecated)

**Examples of Unused Backward Compatibility Code Found:**
- `FileIndex::BuildFullPath()` - Not called, all code uses `PathBuilder::BuildFullPath()` directly
- `DirectXManager::Initialize(HWND)` - Not called, all code uses `Initialize(GLFWwindow*)`
- `path_constants::kDefaultVolumeRootPath` / `kDefaultUserRootPath` - Not used, replaced by functions
- `SearchContext::use_regex` / `use_regex_path` - Legacy flags, not used

**When to Keep:**
- Code that is actually used (even if marked for backward compatibility)
- Code that is part of a public API with external consumers (not applicable to this project)

**Benefits:**
- Cleaner, more maintainable codebase
- Reduced confusion about which APIs to use
- Less code to test and maintain
- Clearer intent (only current APIs are available)

**Action:** When refactoring or reviewing code, identify and remove unused backward compatibility code. Don't add backward compatibility code unless there's a clear, immediate need.

---

## Code Quality Checklist

When adding or modifying code, evaluate and improve the following aspects:

### Technical Debt
- [ ] Does the new code increase technical debt? If yes, refactor to reduce it.
- [ ] Are there quick wins to reduce existing technical debt in the modified area?

### Code Cleanliness
- [ ] **Dead Code:** Remove unused functions, variables, or commented-out code
- [ ] **Duplication:** Eliminate repeated code patterns (apply DRY principle)
- [ ] **Simplicity:** Can complex logic be simplified without losing functionality?

### Code Quality Dimensions
- [ ] **Readability:** Is the code easy to understand? Improve variable names, add brief comments if needed
- [ ] **Maintainability:** Will future developers be able to modify this easily?
- [ ] **Security:** Are there potential security issues (buffer overflows, unvalidated input, etc.)?
- [ ] **Efficiency:** Can algorithms or data structures be optimized?
- [ ] **Scalability:** Will this code handle larger inputs or more concurrent operations?
- [ ] **Extensibility:** Is the design flexible enough for future requirements?
- [ ] **Reliability:** Are error cases handled? Is the code robust?

### Optimization
- [ ] **Performance:** Can execution speed be improved? (Remember: favor speed over memory)
- [ ] **Memory:** If memory usage is excessive, can it be reduced without sacrificing speed?

**Note:** You don't need to address every item for every change. Focus on improvements that are:
- Relevant to the code you're modifying
- Quick wins that don't derail the main task
- Important for the overall codebase health

---

## Encapsulation

**Principle:** Encapsulate internal implementation details.

**Guidelines:**
- Use `private:` sections for implementation details
- Expose only necessary public interfaces
- Prefer passing data through function parameters over global state
- Use namespaces to organize related functionality

---

## ImGui Immediate Mode Paradigm

**This project uses ImGui (Dear ImGui), which follows an immediate mode GUI paradigm. Understanding this paradigm is critical for writing correct UI code.**

### What is Immediate Mode?

ImGui uses an **immediate mode** paradigm, which means:
- **UI is rebuilt every frame** - All UI code runs each frame (typically 60 FPS)
- **No persistent widget objects** - Widgets don't exist as objects you store
- **State is derived from data** - UI automatically reflects your application state
- **Inline event handling** - Events are handled with `if (ImGui::Button(...))` checks

### Key Principles

1. **No Widget Storage**
   ```cpp
   // ❌ WRONG - Widgets don't exist as objects
   ImGui::Button* myButton = ...;
   myButton->SetText("New Text");
   
   // ✅ CORRECT - Call ImGui functions directly each frame
   if (ImGui::Button("Click Me")) {
       // handle click
   }
   ```

2. **Direct Data Binding**
   ```cpp
   // ✅ CORRECT - UI reflects data automatically
   bool is_enabled = (document != nullptr && document->IsDirty());
   if (ImGui::MenuItem("Save", nullptr, false, is_enabled)) {
       document->Save();
   }
   ```

3. **Frame-Based Rendering**
   - `UIRenderer::RenderMainWindow()` is called every frame from the main loop
   - All UI components are re-rendered each frame
   - Performance is handled by ImGui (virtual scrolling, clipping, etc.)

4. **Thread Safety - CRITICAL**
   - **ALL ImGui calls MUST be on the main thread**
   - Background work must synchronize results back to main thread
   - See `docs/design/IMGUI_IMMEDIATE_MODE_PARADIGM.md` for threading and paradigm details

### Common Mistakes to Avoid

1. **Don't store widget references** - Widgets don't exist as persistent objects
2. **Don't try to "update" widgets** - Change your data, UI reflects it next frame
3. **Don't call ImGui from background threads** - ImGui is NOT thread-safe
4. **Don't optimize prematurely** - ImGui is already optimized for frame-based rendering

### Our Implementation Pattern

- **Static rendering methods** - All UI code in `UIRenderer` static methods
- **State passed as parameters** - `GuiState`, `AppSettings`, etc. passed to render methods
- **No instance state in renderer** - Rendering is stateless, state is in parameters
- **Main loop integration** - `RenderMainWindow()` called every frame from `Application::ProcessFrame()`

### When Working with UI Code

1. **Adding new UI elements:**
   - Add rendering code in appropriate `UIRenderer` method
   - Read state from parameters (don't store widget references)
   - Handle events inline with `if (ImGui::...)` checks

2. **Modifying existing UI:**
   - Change the data/state that UI reads from
   - UI will automatically reflect changes next frame
   - Don't try to "update" widgets directly

3. **Background work:**
   - Do work in background thread
   - Store results in application state
   - UI reads from state on main thread (next frame)

### Reference Documentation

- **Full explanation:** `docs/design/IMGUI_IMMEDIATE_MODE_PARADIGM.md`
- **ImGui Wiki:** [About the IMGUI Paradigm](https://github.com/ocornut/imgui/wiki/About-the-IMGUI-paradigm)
- **Threading:** All ImGui calls on main thread; see `docs/design/IMGUI_IMMEDIATE_MODE_PARADIGM.md`

**Remember:** UI is a function of your data, rebuilt every frame. This eliminates widget lifecycle management and state synchronization, making UI code simpler and more maintainable.

---

## ImGui Popup Management Rules

**This section enforces proper popup management to prevent UI freezing, input blocking, and popup closing issues. These rules are based on real bugs found and fixed in the codebase.**

### Window Context Requirement (CRITICAL)

**Problem:** `ImGui::OpenPopup()` and `ImGui::BeginPopupModal()` must be called in the same window context. If `OpenPopup()` is called inside a child window but `BeginPopupModal()` is called at the parent window level, the popup cannot be found and will not appear.

**Solution:** Defer popup opening to the parent window level using state flags.

```cpp
// ❌ WRONG - OpenPopup called in child window, BeginPopupModal at parent level
void RenderChildWindow() {
  if (ImGui::Button("Help")) {
    ImGui::OpenPopup("HelpPopup");  // Called in child window context
  }
}

void RenderParentWindow() {
  RenderChildWindow();
  // Popup opened in child context, but BeginPopupModal called here - MISMATCH!
  if (ImGui::BeginPopupModal("HelpPopup", nullptr, ...)) {
    // Popup will never appear!
  }
}

// ✅ CORRECT - Defer opening to parent window level
void RenderChildWindow(GuiState& state) {
  if (ImGui::Button("Help")) {
    state.openHelpPopup = true;  // Set flag, don't call OpenPopup here
  }
}

void RenderParentWindow(GuiState& state) {
  RenderChildWindow(state);
  
  // Open popup at parent window level (same context as BeginPopupModal)
  if (state.openHelpPopup) {
    ImGui::OpenPopup("HelpPopup");
    state.openHelpPopup = false;
  }
  
  // Now BeginPopupModal can find the popup
  if (ImGui::BeginPopupModal("HelpPopup", nullptr, ...)) {
    // Popup appears correctly
  }
}
```

**When to Apply:**
- When `OpenPopup()` is called inside a child window (`BeginChild()` / `EndChild()` block)
- When `BeginPopupModal()` is called at a different window level
- Always use state flags to defer popup opening to the correct window context

**Enforcement:**
- Check that `OpenPopup()` and `BeginPopupModal()` are called at the same window level
- Use state flags (`openXPopup`) to defer opening from child windows to parent window level
- Verify popup ID strings match exactly between `OpenPopup()` and `BeginPopupModal()`

---

### SetNextWindowPos Ordering (CRITICAL)

**Problem:** `ImGui::SetNextWindowPos()` must be called **every frame** before `BeginPopupModal()`, not just when opening. Calling it only when opening can cause popup state management issues and UI freezing.

**Solution:** Call `SetNextWindowPos()` every frame before `BeginPopupModal()`, matching the working pattern in `ResultsTable.cpp`.

```cpp
// ❌ WRONG - SetNextWindowPos only called when opening
void RenderPopup(GuiState& state) {
  if (state.openPopup) {
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::OpenPopup("MyPopup");
    state.openPopup = false;
  }
  
  // SetNextWindowPos not called here - can cause issues
  if (ImGui::BeginPopupModal("MyPopup", nullptr, ...)) {
    // ...
  }
}

// ✅ CORRECT - SetNextWindowPos called every frame before BeginPopupModal
void RenderPopup(GuiState& state) {
  if (state.openPopup) {
    ImGui::OpenPopup("MyPopup");
    state.openPopup = false;
  }
  
  // CRITICAL: Call SetNextWindowPos EVERY FRAME before BeginPopupModal
  const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  auto center = ImVec2(main_viewport->WorkPos.x + main_viewport->WorkSize.x * 0.5f,
                       main_viewport->WorkPos.y + main_viewport->WorkSize.y * 0.5f);
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  
  if (ImGui::BeginPopupModal("MyPopup", nullptr, ...)) {
    // ...
  }
}
```

**When to Apply:**
- Every popup that uses `SetNextWindowPos()` for centering
- Follow the pattern used in `ResultsTable.cpp` (delete confirmation popup)
- Call `SetNextWindowPos()` every frame, not just when opening

**Why This Matters:**
- ImGui's popup state management depends on proper window positioning
- Calling `SetNextWindowPos()` only when opening can interfere with popup closing mechanism
- This pattern prevents UI freezing when closing popups

---

### CollapsingHeader in Popups (CRITICAL)

**Problem:** `CollapsingHeader` inside `BeginPopupModal` can interfere with input handling, causing `CloseCurrentPopup()` to fail and UI to freeze. The header can consume mouse/keyboard events that should reach the close button.

**Solution:** Ensure close button is outside `CollapsingHeader` and use `ImGui::SetItemDefaultFocus()` to guarantee the close button can receive input.

```cpp
// ❌ WRONG - Close button might be blocked by CollapsingHeader
if (ImGui::BeginPopupModal("MyPopup", nullptr, ...)) {
  if (ImGui::CollapsingHeader("Details")) {
    // content
  }
  if (ImGui::Button("Close")) {
    ImGui::CloseCurrentPopup();  // May not work if CollapsingHeader blocks input
  }
  ImGui::EndPopup();
}

// ✅ CORRECT - Close button outside CollapsingHeader with focus handling
if (ImGui::BeginPopupModal("MyPopup", nullptr, ...)) {
  // Content before CollapsingHeader
  ImGui::Text("Main content");
  
  // CollapsingHeader (can interfere with input)
  if (ImGui::CollapsingHeader("Details")) {
    ImGui::Text("Detailed information");
  }
  
  // CRITICAL: Close button MUST be outside CollapsingHeader
  // Use SetItemDefaultFocus to ensure it can receive input even if
  // CollapsingHeader interferes with modal input handling
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  if (ImGui::Button(ICON_FA_XMARK " Close", ImVec2(120, 0))) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::SetItemDefaultFocus();  // Ensure close button can receive focus
  
  ImGui::EndPopup();
}
```

**When to Apply:**
- Every popup that contains `CollapsingHeader`
- When close button doesn't work reliably
- When UI freezes when trying to close popup with `CollapsingHeader`

**Key Points:**
- Close button must be **outside** `CollapsingHeader` block
- Use `ImGui::SetItemDefaultFocus()` on the close button
- Inline the close button code (don't use helper functions) when `CollapsingHeader` is present
- Add comments explaining why the close button is handled specially

**Alternative:** If possible, avoid `CollapsingHeader` in modal popups. Use simple text or `TreeNode` instead.

---

### CloseCurrentPopup() Usage

**Problem:** `CloseCurrentPopup()` must be called from within the `BeginPopupModal()` / `EndPopup()` block when the popup is open. Calling it outside this block or after `EndPopup()` has no effect.

**Solution:** Always call `CloseCurrentPopup()` from within the `BeginPopupModal()` block, typically in a button click handler.

```cpp
// ❌ WRONG - CloseCurrentPopup called outside BeginPopupModal block
void RenderPopup() {
  if (ImGui::BeginPopupModal("MyPopup", nullptr, ...)) {
    ImGui::Text("Content");
    ImGui::EndPopup();
  }
  
  // Too late - popup already ended
  if (some_condition) {
    ImGui::CloseCurrentPopup();  // Does nothing!
  }
}

// ✅ CORRECT - CloseCurrentPopup called inside BeginPopupModal block
void RenderPopup() {
  if (ImGui::BeginPopupModal("MyPopup", nullptr, ...)) {
    ImGui::Text("Content");
    
    if (ImGui::Button("Close")) {
      ImGui::CloseCurrentPopup();  // Called while popup is open
    }
    
    ImGui::EndPopup();
  }
}
```

**When to Apply:**
- Every time you need to programmatically close a popup
- In button click handlers inside popup content
- Never call `CloseCurrentPopup()` after `EndPopup()`

---

### Popup State Management Pattern

**Problem:** Popups need proper state management to track opening requests and open state separately.

**Solution:** Use two separate flags: one for opening requests (from child windows) and one for tracking open state.

```cpp
// In GuiState.h
struct GuiState {
  // Popup state flags (deferred opening from child windows to parent window level)
  bool openSearchHelpPopup = false;
  bool openKeyboardShortcutsPopup = false;
  
  // Popup open state (managed explicitly for proper closing)
  // Note: Can be added if needed for specific popups
};

// In popup rendering function
void RenderPopup(GuiState& state) {
  // Open popup if requested (only once per request, not every frame)
  if (state.openSearchHelpPopup) {
    ImGui::OpenPopup("SearchHelpPopup");
    state.openSearchHelpPopup = false;
  }
  
  // SetNextWindowPos every frame before BeginPopupModal
  const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  auto center = ImVec2(main_viewport->WorkPos.x + main_viewport->WorkSize.x * 0.5f,
                       main_viewport->WorkPos.y + main_viewport->WorkSize.y * 0.5f);
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  
  // Always try to begin popup - BeginPopupModal returns false if popup isn't open
  if (ImGui::BeginPopupModal("SearchHelpPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    // Popup content
    if (ImGui::Button("Close")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}
```

**When to Apply:**
- Every popup that can be opened from child windows
- When popup opening and rendering are in different window contexts
- For consistent popup state management across the codebase

---

### Popup ID Consistency

**Problem:** The string ID passed to `OpenPopup()` must match exactly (case-sensitive) the ID passed to `BeginPopupModal()`. Mismatched IDs cause popups to not appear.

**Solution:** Use constants or ensure exact string matching.

```cpp
// ✅ CORRECT - Use constant to ensure consistency
namespace {
  constexpr const char* kSearchHelpPopupId = "SearchHelpPopup";
}

void OpenPopup(GuiState& state) {
  if (state.openSearchHelpPopup) {
    ImGui::OpenPopup(kSearchHelpPopupId);  // Same ID
    state.openSearchHelpPopup = false;
  }
}

void RenderPopup(GuiState& state) {
  if (ImGui::BeginPopupModal(kSearchHelpPopupId, nullptr, ...)) {  // Same ID
    // ...
  }
}

// ❌ WRONG - Mismatched IDs
ImGui::OpenPopup("SearchHelpPopup");      // Different case
ImGui::BeginPopupModal("searchhelppopup", ...);  // Won't find popup!
```

**When to Apply:**
- Every popup implementation
- When refactoring popup names
- Use constants to prevent typos and ensure consistency

---

### Checklist for Popup Implementation

Before committing popup code, verify:

- [ ] **Window Context:** `OpenPopup()` and `BeginPopupModal()` are called at the same window level
- [ ] **State Flags:** Use `openXPopup` flags to defer opening from child windows to parent level
- [ ] **SetNextWindowPos:** Called every frame before `BeginPopupModal()`, not just when opening
- [ ] **CollapsingHeader:** If used, close button is outside header and uses `SetItemDefaultFocus()`
- [ ] **CloseCurrentPopup:** Called from within `BeginPopupModal()` block, not after `EndPopup()`
- [ ] **ID Consistency:** Popup ID strings match exactly between `OpenPopup()` and `BeginPopupModal()`
- [ ] **State Management:** Proper flags for opening requests and open state tracking

**Reference Examples:**
- **Working popup:** `ResultsTable.cpp::HandleDeleteKeyAndPopup()` (delete confirmation)
- **Working popup:** `Popups.cpp::RenderKeyboardShortcutsPopup()` (no CollapsingHeader)
- **Problematic pattern (fixed):** `Popups.cpp::RenderSearchHelpPopup()` (has CollapsingHeader, requires special handling)

---

### ImGui Test Engine Regression Tests (Precondition Verification)

Regression tests that drive search via `IRegressionTestHook` (e.g. `RunRegressionTestCase` in `ImGuiTestEngineTests.cpp`) must **verify that settings/params were applied** before triggering the search. Otherwise a test may run with different settings than intended (e.g. null `settings_`, wrong state) and pass or fail for the wrong reason.

**Rule:** After each `Set*` call that affects the next search, assert that the corresponding getter matches what was set, then trigger the search.

- **Load balancing:** After `SetLoadBalancingStrategy(strategy)`, require `GetLoadBalancingStrategy() == strategy` (IM_CHECK) before `TriggerManualSearch()`.
- **Streaming:** After `SetStreamPartialResults(stream)`, require `GetStreamPartialResults() == stream` before triggering search.
- **Search params:** After `SetSearchParams(...)`, require `GetSearchParamFilename()`, `GetSearchParamPath()`, `GetSearchParamExtensions()`, `GetSearchParamFoldersOnly()` to match the values just set before triggering search.

**UI window tests** (Help, Settings, Metrics, Search Syntax): Use exact window title comparison (`std::strcmp(win->Name, kExactTitle) == 0`) and, for optional UI (e.g. Metrics button), a pre-check (e.g. `ItemExists(ref)`) to skip gracefully when the control is not available.

**Reference:** `src/ui/ImGuiTestEngineTests.cpp` (`RunRegressionTestCase`), `src/ui/ImGuiTestEngineRegressionHook.h`.

---

## Workflow Summary

1. **Before Making Changes:**
   - Understand the existing code structure
   - Identify the scope of your changes
   - Check naming conventions in `docs/standards/CXX17_NAMING_CONVENTIONS.md`

2. **While Making Changes:**
   - Apply Windows-specific rules (e.g., `(std::min)`, `(std::max)`)
   - Use C++17 init-statements for variables only used in if statements
   - Use RAII instead of manual memory management
   - Apply const correctness (const functions, const parameters)
   - Use explicit lambda captures
   - Prefer `std::string_view` for read-only string parameters
   - Catch specific exceptions, not generic `catch (...)`
   - Avoid C-style casts
   - Follow naming conventions
   - Apply the Boy Scout Rule (improve code you touch)
   - Check the Code Quality Checklist

3. **After Making Changes:**
   - Verify naming conventions are followed
   - Explain any Boy Scout Rule improvements you made
   - Ensure Windows-specific rules are applied
   - Confirm no compilation commands were attempted

---

## macOS Test Requirement (Build-Tests Exception)

On macOS, any change to C++ source, headers, or CMake **must** be validated by running the cross-platform test suite:

- **Required command (macOS only):**
  - `scripts/build_tests_macos.sh`
- **Scope:** Required when changing:
  - `*.cpp`, `*.c`, `*.h`, `*.hpp`, `*.cxx`, `*.cc`
  - `CMakeLists.txt` or files under `scripts/` that affect builds/tests
- **Exceptions:**
  - Pure documentation-only changes (for example, `*.md` files or comment-only edits) **may** skip this step.
- **Agent note:** When modifying code under macOS, explicitly state in your response whether `scripts/build_tests_macos.sh` was run and whether it passed.

This script is the **only allowed build/test entrypoint on macOS**; do not invoke `cmake`, `make`, `clang++`, or other build tools directly.

---

## Modifying `CMakeLists.txt` Safely

When adding new source or test files, follow these rules so you don't break the CMake configuration:

- **Mirror existing patterns**
  - When adding a new app `.cpp`/`.h`, place it in the appropriate list (for example `APP_SOURCES`, a specific `add_executable(...)` block, or a test sources list) using the **same style, indentation, and ordering** as nearby entries.
  - Keep related files grouped together (e.g., `Foo.cpp` near `Foo.h` and its helpers) so future diffs remain small and readable.

- **Respect conditional logic and options**
  - Do **not** move or duplicate the `if(MSVC)`, `if(WIN32)`, `if(BUILD_TESTS)`, or `if(ENABLE_PGO)` blocks when adding files—just add your entries **inside the correct existing block**.
  - If a new file is Windows-only, add it only to Windows-specific targets or under `if(WIN32)` as appropriate.

- **Keep PGO and optimization flags unchanged**
  - Never change the `ENABLE_PGO` logic, `/GL`, `/Gy`, `/GENPROFILE`, `/USEPROFILE`, or `/LTCG:*` flags when adding new files.
  - **Critical PGO flags:**
    - **Phase 1 (Instrumented)**: `/GL /Gy` compiler flags + `/LTCG:PGINSTRUMENT /GENPROFILE` linker flags
    - **Phase 2 (Optimized)**: `/GL /Gy` compiler flags + `/LTCG:PGOPTIMIZE /USEPROFILE` linker flags
  - New source files should naturally inherit compiler/linker flags from the target; you almost never need per-file flags.
  - **Important**: Missing `/GENPROFILE` or `/USEPROFILE` on the linker command line will cause LNK1269 linker errors.

- **Tests: follow the existing test pattern**
  - When adding a new test executable, copy the pattern used by existing tests: `add_executable`, `target_include_directories`, `target_compile_features`, MSVC/non-MSVC flags, and `add_test`.
  - Make sure any new test uses the same `USN_WINDOWS_TESTS`, `NOMINMAX`, and `WIN32_LEAN_AND_MEAN` definitions where applicable.
  - **CRITICAL: PGO flags for test targets**
    - If a test target includes source files that are also in the main executable (e.g., `LoadBalancingStrategy.cpp`, `FileIndex.cpp`), the test target **MUST** use the same PGO flags as the main target.
    - **Why**: When PGO is enabled, object files compiled with different PGO flags cannot be linked together. This causes LNK1269 errors.
    - **Solution**: Use the same PGO compiler flags as the main target, but use standard linker flags (not PGO-specific) to avoid LNK1266 errors. See `file_index_search_strategy_tests` in CMakeLists.txt for the pattern:
      ```cmake
      if(ENABLE_PGO)
          if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/Release/FindHelper.pgd")
              # Phase 2: Use same compiler flags (/GL /Gy) but standard linker flags
              target_compile_options(test_target PRIVATE 
                  $<$<CONFIG:Release>:/GL /Gy>
              )
              target_link_options(test_target PRIVATE 
                  $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
              )
          else()
              # Phase 1: Use same compiler flags (/GL /Gy) but standard linker flags
              target_compile_options(test_target PRIVATE 
                  $<$<CONFIG:Release>:/GL /Gy>
              )
              target_link_options(test_target PRIVATE 
                  $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
              )
          endif()
      endif()
      ```
    - **Why**: Test targets need matching compiler flags to avoid LNK1269, but don't need PGO optimization (tests don't require performance). Using standard `/LTCG` instead of `/LTCG:PGINSTRUMENT` or `/LTCG:PGOPTIMIZE` avoids LNK1266 errors (missing .pgd file).
    - **Check**: Before committing, verify that any test target sharing source files with the main target has matching PGO flags.

- **Validate CMake changes before committing**
  - Run the CMake configure and build on your **Windows** environment after editing `CMakeLists.txt` to ensure there are no configuration or link errors related to your changes.
  - If you touch complex sections (PGO, tests, or toolchain-related code), keep the change **minimal** and describe it clearly in your commit message.

These practices keep `CMakeLists.txt` predictable, make it easy to review changes, and reduce the risk of subtle Windows-only or PGO-related breakage.

---

## Modifying `.clang-tidy` Safely

When editing `.clang-tidy`, follow these rules so the YAML is valid and checks stay enabled/disabled as intended.

### No Inline `#` Comments in YAML Values

**Problem:** In YAML, `#` starts a comment. Everything from `#` to the end of the line is stripped. If you put a comment on the same line as a check name inside the `Checks:` value, the check name is removed and the check is **not** disabled (or not enabled) as intended.

**Example of what went wrong:**
```yaml
# ❌ WRONG - The check name after # is treated as comment and stripped
Checks: >-
  *,
  -altera-*,
  # We use #pragma once project-wide.
  -llvm-header-guard,      # <- Only "-llvm-header-guard" was intended to be disabled
  -portability-avoid-pragma-once,
```
Result: `-llvm-header-guard` and `-portability-avoid-pragma-once` were **not** in the Checks string (the whole line after `#` was dropped), so those checks stayed enabled and clang-tidy kept warning on `#pragma once`.

**Solution:**
- **Do not** add inline `#` comments inside the `Checks:` block (the multi-line value under `Checks: >-`). Each line must contain only the check pattern (e.g. `-llvm-header-guard,`).
- Put rationale in the **comment block below** the Checks list (the "--- Disabled Checks Rationale ---" section), not on the same line as a check.

```yaml
# ✅ CORRECT - No # on the same line as a check name
Checks: >-
  *,
  -altera-*,
  -llvm-header-guard,
  -portability-avoid-pragma-once,
```

Then document *why* in a separate comment block:
```yaml
# --- Disabled Checks Rationale ---
#   -llvm-header-guard: We use #pragma once project-wide; disabling avoids NOLINT in every header.
#   -portability-avoid-pragma-once: Same; we prefer #pragma once.
```

### Rules Summary

1. **Checks list:** Every line under `Checks: >-` must be only check patterns (e.g. `-check-name,`). No `#` comment on the same line.
2. **Rationale:** Use the "Disabled Checks Rationale" comment block (or a note above `Checks:`) to explain why a check is disabled. Do not use inline `#` inside the Checks value.
3. **Other YAML keys:** For any key whose value is a long string or list (e.g. `CheckOptions:`), avoid inline `#` inside the value; YAML will treat it as start of comment.
4. **Verify after editing:** Run `clang-tidy --config-file=.clang-tidy --dump-config` and confirm the `Checks:` output contains (or omits) the intended check names. Or run clang-tidy on one file and confirm the expected warning does or does not appear.

### When Adding or Removing a Check

- To **disable** a check: add one line `-check-name,` (or `-group-*,`) under `Checks: >-`. No comment on that line.
- To **re-enable** a check: remove the line `-check-name,` from the Checks list.
- Update the "Disabled Checks Rationale" section to document why the check is disabled (for future maintainers and agents).

**Action:** When modifying `.clang-tidy`, never put `#` on the same line as a check name or inside any YAML value. Put explanations in separate comment blocks or lines above the value.

---

## Handling `cppcoreguidelines-pro-bounds-avoid-unchecked-container-access`

This check flags every `container[i]` (`operator[]`) as potentially unsafe because it performs no bounds checking and causes undefined behaviour on out-of-range access. The check is intentionally kept **enabled** in this project to surface real issues; it is **not** disabled globally. Each occurrence must be resolved by one of the following approaches, chosen by context:

### 1. Range-for (preferred refactor)

Use when the loop only needs the value and carries no index arithmetic. Eliminates the warning cleanly and often produces clearer code. This is the default first choice for non-DFA loop bodies.

```cpp
// ❌ flagged
for (size_t i = 0; i < vec.size(); ++i) {
    process(vec[i]);
}

// ✅ range-for — no warning, no overhead
for (const auto& item : vec) {
    process(item);
}
```

### 2. `.at()` (external data boundaries only)

Use **only** where the access reads external or user-supplied data whose structure cannot be verified at compile time and where throwing `std::out_of_range` is an appropriate error response. The canonical case is JSON parsing (`GeminiApiUtils.cpp`). Do **not** use `.at()` in hot search paths — the extra branch and exception mechanism have measurable overhead on millions of iterations.

```cpp
// ✅ appropriate — external JSON data, catching bad API responses
const auto& candidates = response.at("candidates");
const auto& content = candidates.at(0).at("content");
```

### 3. `NOLINT` with justification (performance-critical / proven safe)

Use when:
- The access is inside a hot path (search workers, DFA, SIMD, string search) where the branch overhead of `.at()` is unacceptable.
- The bounds are provably established by surrounding logic (loop condition, prior size check, construction invariant) but cannot be expressed as a range-for because the index is used in arithmetic or multi-array access.

The `NOLINT` comment **must** state why the access is safe:

```cpp
// ✅ NOLINT with invariant documented
const uint16_t next = dfa_table_[state][ch];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - state < states_.size() enforced by DFA construction; ch is uint8_t (0-255)

// ✅ block-level NOLINT for a function where all accesses share the same invariant
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
// All accesses below are guarded by: i < pattern_.size(), established by the loop condition.
... indexed DFA traversal ...
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
```

### Decision summary

| Context | Fix |
|---|---|
| Simple value loop, no index needed | Range-for |
| External / user-supplied data at I/O boundary | `.at()` |
| Hot path (search, DFA, SIMD, regex) with proven bounds | `NOLINT` + justification comment |
| Multi-array indexed access (e.g. `a[i]` and `b[i]`) | `NOLINT` + justification comment |

---

## SonarQube Code Quality Rules

This section provides specific rules to prevent common SonarQube violations that degrade code quality. These rules are based on actual issues found in the codebase and should be strictly followed. For a current snapshot of open issues and rule distribution, run `scripts/fetch_sonar_results.sh --open-only`.

### Avoid `void*` Pointers (cpp:S5008)

**Problem:** `void*` pointers eliminate type safety and make code harder to understand and maintain.

**Solution:** Use strongly-typed pointers or templates instead of `void*`.

```cpp
// ❌ WRONG - void* loses type information
void ProcessData(void* data, size_t size) {
  // What type is data? Unsafe!
}

// ✅ CORRECT - Use templates for type safety
template<typename T>
void ProcessData(T* data, size_t count) {
  // Type-safe, compiler can verify correctness
}

// ✅ CORRECT - Use specific types
void ProcessData(int* data, size_t count) {
  // Clear intent, type-safe
}

// ✅ CORRECT - Use std::variant for multiple types
void ProcessData(std::variant<int*, float*, double*> data) {
  // Type-safe union of pointer types
}
```

**When to Apply:**
- Every time you need to pass generic data - use templates or specific types
- When interfacing with C APIs that use `void*` - wrap them in type-safe C++ interfaces
- When storing heterogeneous data - use `std::variant` or inheritance

**Benefits:**
- Type safety at compile time
- Better code clarity and maintainability
- Prevents runtime type errors
- Enables compiler optimizations

---

### Control Nesting Depth (cpp:S134)

**Problem:** Deeply nested code (more than 3 levels) is hard to read, understand, and maintain.

**Solution:** Refactor nested code using early returns, guard clauses, and extracted functions.

```cpp
// ❌ WRONG - 4 levels of nesting
void ProcessFile(const std::string& path) {
  if (FileExists(path)) {
    if (IsReadable(path)) {
      if (auto file = OpenFile(path)) {
        if (file->IsValid()) {
          // Process file - too deeply nested!
        }
      }
    }
  }
}

// ✅ CORRECT - Early returns reduce nesting
void ProcessFile(const std::string& path) {
  if (!FileExists(path)) return;
  if (!IsReadable(path)) return;
  
  auto file = OpenFile(path);
  if (!file || !file->IsValid()) return;
  
  // Process file - maximum 1 level of nesting
}

// ✅ CORRECT - Extract complex logic to separate function
void ProcessFile(const std::string& path) {
  if (auto file = ValidateAndOpenFile(path)) {
    ProcessValidFile(*file);
  }
}

std::unique_ptr<File> ValidateAndOpenFile(const std::string& path) {
  if (!FileExists(path)) return nullptr;
  if (!IsReadable(path)) return nullptr;
  auto file = OpenFile(path);
  if (!file || !file->IsValid()) return nullptr;
  return file;
}
```

**When to Apply:**
- When nesting exceeds 3 levels (if/for/while/switch/do)
- Before writing deeply nested code - refactor proactively
- When reviewing code - identify and refactor deep nesting

**Refactoring Techniques:**
1. **Early returns** - Return early for error/invalid cases
2. **Guard clauses** - Check preconditions first, then process
3. **Extract functions** - Move nested logic to separate functions
4. **Invert conditions** - Use `if (!condition) return;` instead of `if (condition) { ... }`
5. **Use logical operators** - Combine conditions: `if (a && b && c)` instead of nested ifs

**Maximum Nesting:** 3 levels maximum (including function body as level 1)

**Enforcement:**
- **SonarQube check:** `cpp:S134` automatically flags code nested more than 3 levels
- **Code review:** Always check for deep nesting during review
- **Manual verification:** Before committing, verify nesting doesn't exceed 3 levels
- **clang-tidy check:** `readability-function-cognitive-complexity` is enabled (threshold: 25) to identify complex functions. See [Clang-Tidy Guide](docs/analysis/CLANG_TIDY_GUIDE.md) and [Classification](docs/analysis/CLANG_TIDY_CLASSIFICATION.md) for details.

**Real-World Examples (from fixes):**
```cpp
// ❌ Before: 4 levels of nesting in SettingsWindow.cpp
if (!settings.crawlFolder.empty()) {
  if (ImGui::InputInt(...)) {
    if (recrawl_interval < 1) {
      recrawl_interval = 1;
    } else if (recrawl_interval > 60) {
      recrawl_interval = 60;
    }
    if (recrawl_interval != settings.recrawlIntervalMinutes) {
      // ... 4th level
    }
  }
}

// ✅ After: Reduced to 3 levels by changing else-if to separate if
if (!settings.crawlFolder.empty()) {
  if (ImGui::InputInt(...)) {
    if (recrawl_interval < 1) {
      recrawl_interval = 1;
    }
    if (recrawl_interval > 60) {
      recrawl_interval = 60;
    }
    if (recrawl_interval != settings.recrawlIntervalMinutes) {
      // ... still 3 levels, but clearer
    }
  }
}
```

**Action:** When writing or modifying code, keep nesting to 3 levels maximum. Use early returns, guard clauses, and extracted functions to reduce nesting depth.

---

### Control Cognitive Complexity (cpp:S3776)

**Problem:** High cognitive complexity makes code hard to understand, test, and maintain. SonarQube limit is 25.

**Solution:** Break complex functions into smaller, focused functions with single responsibilities.

```cpp
// ❌ WRONG - Cognitive complexity 147 (way over limit of 25)
void RenderSettingsWindow(Settings& settings) {
  // 200+ lines of nested conditions, loops, and logic
  // Multiple responsibilities mixed together
}

// ✅ CORRECT - Break into focused functions
void RenderSettingsWindow(Settings& settings) {
  RenderGeneralSettings(settings);
  RenderAppearanceSettings(settings);
  RenderAdvancedSettings(settings);
}

void RenderGeneralSettings(Settings& settings) {
  // Single responsibility: general settings only
  // Low cognitive complexity
}

void RenderAppearanceSettings(Settings& settings) {
  // Single responsibility: appearance settings only
  // Low cognitive complexity
}
```

**When to Apply:**
- When function complexity approaches or exceeds 25
- When a function has multiple responsibilities
- When a function is hard to understand or test
- Before writing large functions - design for simplicity

**Complexity Factors:**
- Each control flow statement (if, for, while, switch, catch) adds complexity
- Nested control flow multiplies complexity
- Boolean operators (&&, ||) add complexity
- Ternary operators add complexity

**Refactoring Techniques:**
1. **Extract methods** - Move logical blocks to separate functions
2. **Single responsibility** - Each function should do one thing
3. **Replace conditionals with polymorphism** - Use virtual functions for complex conditionals
4. **Use strategy pattern** - Replace large switch/if chains with strategy objects
5. **Simplify boolean logic** - Extract complex conditions to named functions

**Target Complexity:** Keep functions under 15 complexity for easy maintenance, maximum 25.

**Enforcement:**
- **SonarQube check:** `cpp:S3776` automatically flags functions with complexity over 25
- **Code review:** Always check function complexity during review
- **Manual verification:** Before committing, verify complex functions are broken down
- **clang-tidy check:** `readability-function-cognitive-complexity` can help identify complex functions (currently disabled, but can be enabled for analysis)

**Real-World Example (from main_common.h fix):**
```cpp
// ❌ Before: Cognitive complexity 30 (over limit of 25)
template<typename BootstrapResult, typename Operation, typename Cleanup = std::nullptr_t>
int HandleExceptions(const char* context, const Operation& operation,
                     const Cleanup& cleanup = nullptr) {
  try {
    return operation();
  } catch (const std::bad_alloc& e) {
    // ... duplicated cleanup code in each catch block
    if constexpr (!std::is_same_v<Cleanup, std::nullptr_t>) {
      if (cleanup) { cleanup(); }
    }
    return 1;
  } catch (const std::system_error& e) {
    // ... same pattern repeated
  }
  // ... more catch blocks with duplicated code
}

// ✅ After: Reduced complexity to ~15 by extracting helper functions
template<typename Cleanup>
void ExecuteCleanup(const Cleanup& cleanup) {
  if constexpr (!std::is_same_v<Cleanup, std::nullptr_t>) {
    if (cleanup) { cleanup(); }
  }
}

template<typename Cleanup>
int HandleExceptionWithCleanup(const char* context, const char* error_type, 
                                const char* error_msg, const Cleanup& cleanup) {
  LOG_ERROR_BUILD(error_type << " " << context << ": " << error_msg);
  ExecuteCleanup(cleanup);
  return 1;
}

// Main function now much simpler - complexity reduced from 30 to ~15
template<typename BootstrapResult, typename Operation, typename Cleanup = std::nullptr_t>
int HandleExceptions(const char* context, const Operation& operation,
                     const Cleanup& cleanup = nullptr) {
  try {
    return operation();
  } catch (const std::bad_alloc& e) {
    return HandleExceptionWithCleanup(context, "Memory allocation failure", e.what(), cleanup);
  }
  // ... other catch blocks use same helper
}
```

**Action:** When writing or modifying functions, keep complexity under 25. Break complex functions into smaller, focused functions with single responsibilities.

---

### Merge Nested If Statements (cpp:S1066)

**Problem:** Nested if statements that can be combined reduce readability and add unnecessary complexity.

**Solution:** Merge nested if statements when the inner condition doesn't depend on the outer condition's body.

```cpp
// ❌ WRONG - Nested ifs that can be merged
if (condition1) {
  if (condition2) {
    // Do something
  }
}

// ✅ CORRECT - Merged into single condition
if (condition1 && condition2) {
  // Do something
}

// ❌ WRONG - Nested if with unrelated logic
if (file != nullptr) {
  if (file->IsValid()) {
    ProcessFile(file);
  }
}

// ✅ CORRECT - Merged with logical AND
if (file != nullptr && file->IsValid()) {
  ProcessFile(file);
}

// ✅ CORRECT - When inner if has else, keep separate
if (condition1) {
  if (condition2) {
    // Do A
  } else {
    // Do B - can't merge because of else
  }
}
```

**When to Apply:**
- When inner if statement has no else branch
- When inner condition doesn't depend on outer condition's side effects
- When both conditions are simple boolean checks

**When NOT to Apply:**
- When inner if has an else branch that depends on outer condition
- When outer condition has side effects needed by inner condition
- When merging would make the condition unreadable (too long)

**Benefits:**
- Reduced nesting depth
- Clearer intent (all conditions visible at once)
- Easier to understand and maintain

---

### Simplify Boolean Expressions

**Problem:** Complex or repetitive boolean logic (long condition chains, manual loops that only check a predicate) is hard to read, easy to get wrong, and increases cognitive complexity.

**Solution:** Prefer algorithms and named conditions: use `std::any_of` / `std::all_of` / `std::none_of` for range checks, extract complex conditions into named variables or functions, and keep conditions short and positive.

```cpp
// ❌ WRONG - Manual loop that only checks a predicate (readability-use-anyofallof)
bool has_invalid = false;
for (const auto& item : items) {
  if (!IsValid(item)) {
    has_invalid = true;
    break;
  }
}

// ✅ CORRECT - std::any_of expresses intent
bool has_invalid = std::any_of(items.begin(), items.end(),
  [](const auto& item) { return !IsValid(item); });

// ❌ WRONG - Long, opaque condition
if (index >= 0 && index < static_cast<int>(list.size()) && list[index].active && !list[index].deleted) {
  Process(list[index]);
}

// ✅ CORRECT - Named condition
const bool in_range = index >= 0 && index < static_cast<int>(list.size());
const bool entry_usable = in_range && list[static_cast<size_t>(index)].active && !list[static_cast<size_t>(index)].deleted;
if (entry_usable) {
  Process(list[static_cast<size_t>(index)]);
}

// ✅ CORRECT - Or extract to a small function
if (IsUsableEntry(list, index)) {
  Process(list[static_cast<size_t>(index)]);
}
```

**When to Apply:**
- Loops that only exist to find whether any/all/none of a range satisfy a predicate → use `std::any_of`, `std::all_of`, `std::none_of`
- Long or repeated condition expressions → extract to a named variable or helper function
- Deeply nested or multi-clause conditions → combine with `&&`/`||` where logical, or split into early returns and named checks
- Double negation or confusing logic → prefer positive, clearly named conditions

**When NOT to Apply:**
- When the loop does more than a simple predicate check (e.g. side effects, counting, multiple conditions with different actions)
- When a named variable or function would hide important domain meaning; keep names meaningful

**Enforcement:**
- **clang-tidy check:** `readability-use-anyofallof` suggests replacing manual predicate loops with `std::any_of` / `std::all_of` / `std::none_of`.
- **Code review:** Prefer clear, short boolean expressions and algorithms over raw loops for pure checks.

**Benefits:**
- Lower cognitive complexity and easier maintenance
- Clearer intent (e.g. "any invalid" vs. loop with break)
- Fewer off-by-one or logic mistakes

**Action:** When writing or touching conditionals or loops that only test a condition, prefer `std::any_of`/`all_of`/`none_of` where appropriate and simplify long conditions with named variables or functions.

---

### Complete or Remove TODO Comments (cpp:S1135)

**Problem:** TODO comments indicate incomplete work and technical debt that should be addressed.

**Solution:** Either complete the TODO immediately or remove it if no longer relevant.

```cpp
// ❌ WRONG - TODO left in code
void ProcessData() {
  // TODO: Add error handling
  Process();
}

// ✅ CORRECT - TODO completed
void ProcessData() {
  try {
    Process();
  } catch (const std::exception& e) {
    LOG_ERROR("Failed to process data: " << e.what());
  }
}

// ✅ CORRECT - TODO removed if no longer needed
void ProcessData() {
  Process();  // Error handling added elsewhere
}

// ✅ CORRECT - TODO with issue tracker reference (temporary)
void ProcessData() {
  // TODO(#123): Optimize this when performance becomes an issue
  Process();
}
```

**When to Apply:**
- When you write a TODO - complete it in the same PR or remove it
- When you encounter a TODO - either fix it or remove it if obsolete
- When refactoring - address TODOs in the refactored code

**Acceptable TODOs:**
- TODOs with issue tracker references (e.g., `TODO(#123)`) that are tracked
- TODOs for future enhancements that are documented and planned
- TODOs that are immediately addressed in the same PR

**Unacceptable TODOs:**
- TODOs without any plan or tracking
- TODOs for critical functionality (must be completed)
- TODOs that are years old and clearly abandoned

**Action:** Before committing, search for `TODO` and either complete or remove each one.

---

### Remove Commented-Out Code (cpp:S125)

**Problem:** Commented-out code distracts from the actual executed code, increases maintenance noise, and quickly becomes out of date. SonarQube flags it as a maintainability smell (MAJOR).

**Solution:** Remove commented-out code; use version control to retrieve it if needed.

```cpp
// ❌ WRONG - Commented-out code left in place
void ProcessData() {
  // int old_value = ComputeLegacy();
  int value = ComputeNew();
  return value;
}

// ✅ CORRECT - Commented-out code removed
void ProcessData() {
  int value = ComputeNew();
  return value;
}
```

**When to Apply:**
- Before committing; remove or justify any commented-out code
- When refactoring; do not leave old code commented out
- Exception: Documentation comments (Doxygen, QDoc, markdown, HTML) are not flagged

**Action:** Before committing, remove commented-out code or add justified `// NOSONAR(cpp:S125)` on the same line with a short comment. Prefer removal and version control for history.

---

### Limit Function Parameters (cpp:S107)

**Problem:** Functions with too many parameters (more than 7) are hard to use, test, and maintain.

**Solution:** Group related parameters into structures or use builder/options patterns.

```cpp
// ❌ WRONG - 14 parameters (way over limit of 7)
void ProcessSearch(
  const std::string& query,
  const std::vector<std::string>& paths,
  bool case_sensitive,
  bool use_regex,
  size_t max_results,
  size_t thread_count,
  int timeout_ms,
  bool include_hidden,
  const std::vector<std::string>& extensions,
  SearchCallback callback,
  void* user_data,
  bool verbose,
  const std::string& log_file,
  SearchOptions options
);

// ✅ CORRECT - Parameters grouped into structures
struct SearchConfig {
  std::string query;
  std::vector<std::string> paths;
  bool case_sensitive = false;
  bool use_regex = false;
  size_t max_results = 1000;
  size_t thread_count = 4;
  int timeout_ms = 5000;
  bool include_hidden = false;
  std::vector<std::string> extensions;
  bool verbose = false;
  std::string log_file;
};

struct SearchCallbacks {
  SearchCallback callback;
  void* user_data = nullptr;
};

void ProcessSearch(const SearchConfig& config, const SearchCallbacks& callbacks);

// ✅ CORRECT - Builder pattern for complex configuration
class SearchBuilder {
public:
  SearchBuilder& SetQuery(const std::string& query);
  SearchBuilder& AddPath(const std::string& path);
  SearchBuilder& SetCaseSensitive(bool value);
  SearchBuilder& SetMaxResults(size_t count);
  // ... more setters ...
  
  void Execute(SearchCallback callback);
};
```

**When to Apply:**
- When a function has more than 5 parameters - consider grouping
- When a function has more than 7 parameters - must refactor
- When parameters are related - group them into a struct
- When configuration is complex - use builder pattern

**Grouping Strategies:**
1. **Configuration struct** - Group all configuration parameters
2. **Options struct** - Group optional parameters with defaults
3. **Builder pattern** - For complex, optional configurations
4. **Context object** - For functions that need many related parameters

**Benefits:**
- Easier to call (fewer arguments to remember)
- Easier to extend (add fields to struct, not new parameters)
- Better default values (use struct initialization)
- More maintainable (related data grouped together)

**Maximum Parameters:** 7 parameters maximum, prefer 5 or fewer.

---

### Use Const References for Read-Only Parameters (cpp:S5350)

**Problem:** Non-const references for read-only parameters are misleading and prevent optimizations.

**Solution:** Use `const T&` for read-only reference parameters.

```cpp
// ❌ WRONG - Non-const reference but parameter is not modified
void ProcessData(SearchThreadPool& thread_pool) {
  size_t count = thread_pool.GetThreadCount();  // Only reading
  // thread_pool is not modified
}

// ✅ CORRECT - Const reference for read-only access
void ProcessData(const SearchThreadPool& thread_pool) {
  size_t count = thread_pool.GetThreadCount();  // Reading only
}

// ✅ CORRECT - Non-const reference when modifying
void ConfigureThreadPool(SearchThreadPool& thread_pool) {
  thread_pool.SetThreadCount(8);  // Modifying
}
```

**When to Apply:**
- When a reference parameter is only read, never modified
- When reviewing function signatures - check if references should be const
- When writing new functions - use const references by default for read-only params

**Parameter Guidelines:**
- `const T&` - Read-only, large objects (avoid copying)
- `T&` - Modify in place (output parameter or modify existing object)
- `T*` - Optional parameter or when null is meaningful
- `const T*` - Optional read-only parameter
- `T` (by value) - Small, cheap-to-copy types (int, bool, pointers)

**Benefits:**
- Clearer API contract (const = won't modify)
- Enables compiler optimizations
- Prevents accidental modifications
- Better code documentation

**Action:** When writing or modifying functions, review all reference parameters and add `const` if they're read-only.

---

### Strengthen Exception Handling (cpp:S1181)

**Note:** This is already covered in "Specific Exception Handling" section, but emphasized here for completeness.

**Problem:** Generic exception catches (`catch (std::exception&)`) hide specific error types and make debugging harder.

**Solution:** Catch the most specific exception type possible.

```cpp
// ❌ WRONG - Too generic
try {
  ParseConfig(file);
} catch (std::exception& e) {
  // Can't distinguish between different error types
  LOG_ERROR(e.what());
}

// ✅ CORRECT - Catch specific exceptions
try {
  ParseConfig(file);
} catch (const std::invalid_argument& e) {
  LOG_ERROR("Invalid config format: " << e.what());
} catch (const std::runtime_error& e) {
  LOG_ERROR("Config file error: " << e.what());
} catch (const std::exception& e) {
  LOG_ERROR("Unexpected error: " << e.what());
}
```

**When to Apply:**
- When you know what specific exceptions can be thrown
- When you need different handling for different error types
- Replace generic `catch (std::exception&)` with specific types when possible

**Exception Hierarchy (most specific to least):**
1. Specific exception types (`std::invalid_argument`, `std::out_of_range`, etc.)
2. Standard exception categories (`std::logic_error`, `std::runtime_error`)
3. Base exception (`std::exception`) - use as catch-all
4. `catch (...)` - only for truly unknown exceptions (rare)

---

### Handle Exceptions Properly (cpp:S2486)

**Problem:** Catching exceptions and not handling them (empty catch blocks or catch-and-ignore) hides errors and makes debugging impossible.

**Solution:** Always handle exceptions meaningfully - log errors, propagate them, or document why ignoring is acceptable.

```cpp
// ❌ WRONG - Catch and ignore (hides errors)
try {
  ProcessData();
} catch (const std::exception& e) {
  // Empty - error is silently ignored!
}

// ❌ WRONG - Catch and ignore with comment
try {
  ProcessData();
} catch (const std::exception& e) {
  // Ignore errors - BAD!
}

// ✅ CORRECT - Log the error
try {
  ProcessData();
} catch (const std::exception& e) {
  LOG_ERROR("Failed to process data: " << e.what());
  // Optionally: return error code, set error state, etc.
}

// ✅ CORRECT - Re-throw if can't handle
try {
  ProcessData();
} catch (const std::exception& e) {
  LOG_ERROR("Processing failed: " << e.what());
  throw;  // Re-throw to let caller handle
}

// ✅ CORRECT - Handle specific case, re-throw others
try {
  ProcessData();
} catch (const std::invalid_argument& e) {
  // Handle invalid argument specifically
  LOG_WARNING("Invalid argument: " << e.what());
  return false;  // Recoverable error
} catch (const std::exception& e) {
  // Can't handle other errors, re-throw
  throw;
}
```

**When to Apply:**
- Every catch block must do something meaningful
- If you can't handle an exception, re-throw it or log it
- Never have empty catch blocks
- Never catch exceptions just to ignore them

**Acceptable Patterns:**
- Log the error and return/exit gracefully
- Re-throw the exception to let caller handle it
- Set error state and continue (if appropriate)
- Transform exception to error code/status

**Unacceptable Patterns:**
- Empty catch blocks
- Catch blocks with only comments
- Catch-and-ignore without documentation
- Catching exceptions just to suppress them

**When draining resources (e.g. consuming `std::future::get()` to avoid blocking destructors):** Do not use an empty `catch (...) {}`. At minimum, catch `std::exception` first and log (e.g. `LOG_ERROR`), then `catch (...)` and log a generic message. Sonar S2486 (handle or don't catch) and S108 (empty compound) require meaningful handling.

**Action:** Review all catch blocks - ensure each one handles the exception meaningfully or re-throws it.

---

### Avoid `reinterpret_cast` (cpp:S3630)

**Problem:** `reinterpret_cast` is the most dangerous C++ cast - it can cause undefined behavior, memory corruption, and type safety violations.

**Solution:** Use safer alternatives: `static_cast`, proper type design, or `std::bit_cast` (C++20).

```cpp
// ❌ WRONG - Dangerous reinterpret_cast
void* void_ptr = GetData();
int* int_ptr = reinterpret_cast<int*>(void_ptr);  // Unsafe!

// ❌ WRONG - Type punning with reinterpret_cast
float f = 3.14f;
int i = *reinterpret_cast<int*>(&f);  // Undefined behavior!

// ✅ CORRECT - Use static_cast when types are related
Base* base = GetBase();
Derived* derived = static_cast<Derived*>(base);  // Safer, but still check validity

// ✅ CORRECT - Use std::bit_cast for type punning (C++20)
float f = 3.14f;
int i = std::bit_cast<int>(f);  // Safe type punning

// ✅ CORRECT - Use union for type punning (C++11)
union FloatInt {
  float f;
  int i;
};
FloatInt fi;
fi.f = 3.14f;
int i = fi.i;  // Legal type punning via union

// ✅ CORRECT - Design proper type-safe interfaces
// Instead of void*, use templates or std::variant
template<typename T>
void ProcessData(T* data) {
  // Type-safe, no cast needed
}
```

**When to Apply:**
- Every time you see `reinterpret_cast` - find a safer alternative
- When interfacing with C APIs - wrap them in type-safe C++ interfaces
- When doing type punning - use `std::bit_cast` (C++20) or union (C++11)

**When `reinterpret_cast` Might Be Acceptable:**
- Interfacing with low-level system APIs (document why)
- Serialization/deserialization (use proper serialization library instead)
- Memory-mapped I/O (wrap in type-safe abstraction)

**Alternatives:**
1. **`static_cast`** - For related types (base/derived, numeric conversions)
2. **`std::bit_cast`** (C++20) - For type punning (safe)
3. **Union** (C++11) - For type punning (legal but use carefully)
4. **Templates** - For generic code (type-safe)
5. **`std::variant`** - For multiple types (type-safe)
6. **Proper type design** - Avoid need for casts

**Benefits:**
- Type safety at compile time
- Prevents undefined behavior
- Easier to understand and maintain
- Compiler can catch errors

**Action:** Search for `reinterpret_cast` in codebase and replace with safer alternatives. Document any remaining uses and justify why they're necessary.

---

### Reduce Nested Break Statements (cpp:S924)

**Problem:** Multiple `break` statements in loops (especially nested loops) make code hard to understand and maintain. SonarQube limits to 1 break statement per loop.

**Solution:** Use early returns, combined loop conditions, extracted functions, flags, or `goto` (as last resort) to reduce breaks.

```cpp
// ❌ WRONG - Nested breaks (hard to understand)
for (int i = 0; i < rows; ++i) {
  for (int j = 0; j < cols; ++j) {
    if (matrix[i][j] == target) {
      result = {i, j};
      break;  // Only breaks inner loop
    }
  }
  if (found) break;  // Need flag to break outer loop
}

// ✅ CORRECT - Extract to function with early return
std::pair<int, int> FindInMatrix(const Matrix& matrix, int target) {
  for (int i = 0; i < matrix.rows(); ++i) {
    for (int j = 0; j < matrix.cols(); ++j) {
      if (matrix[i][j] == target) {
        return {i, j};  // Early return breaks both loops
      }
    }
  }
  return {-1, -1};  // Not found
}

// ✅ CORRECT - Use flag with single break
bool found = false;
for (int i = 0; i < rows && !found; ++i) {
  for (int j = 0; j < cols && !found; ++j) {
    if (matrix[i][j] == target) {
      result = {i, j};
      found = true;
      break;  // Only breaks inner loop, but flag stops outer
    }
  }
}

// ✅ CORRECT - Use goto for breaking nested loops (acceptable in C++)
for (int i = 0; i < rows; ++i) {
  for (int j = 0; j < cols; ++j) {
    if (matrix[i][j] == target) {
      result = {i, j};
      goto found;  // Breaks both loops cleanly
    }
  }
}
found:
// Continue after loops
```

**When to Apply:**
- When you have multiple break statements in a single loop (more than 1-2)
- When you have nested loops with break statements
- When you need to break from multiple nested loops
- Before writing multiple breaks - consider alternatives

**Refactoring Techniques:**
1. **Combine loop conditions** - Move break conditions into the loop header
2. **Extract function** - Move nested loops to separate function, use early return
3. **Use flag** - Set flag in inner loop, check in outer loop condition
4. **Use `goto`** - Acceptable in C++ for breaking nested loops (label immediately after loops)
5. **Restructure logic** - Can the nested loops be flattened or restructured?

**When NOT to Apply:**
- Single break in single loop (no issue, but prefer combined conditions)
- Breaking from switch inside loop (different context)

**Maximum Breaks:** 1-2 break statements per loop maximum. Prefer combining conditions in the loop header.

**Enforcement:**
- **SonarQube check:** `cpp:S924` automatically flags loops with more than 1 break statement
- **Code review:** Always check for multiple break statements in loops
- **Manual verification:** Before committing, verify loops don't have excessive breaks

**Real-World Example (from GeminiApiHttp_win.cpp):**
```cpp
// ❌ Before: Multiple breaks in while(true) loop (4 breaks)
while (true) {
  if (!WinHttpQueryDataAvailable(h_request, &bytes_available)) {
    break;
  }
  if (bytes_available == 0) {
    break;
  }
  // ... size check with early return ...
  if (!WinHttpReadData(h_request, buffer.data(), bytes_available, &bytes_read)) {
    break;
  }
  if (bytes_read == 0) {
    break;
  }
  response_body.append(buffer.data(), bytes_read);
}

// ✅ After: Combined conditions in loop header (1 break)
while (WinHttpQueryDataAvailable(h_request, &bytes_available) && bytes_available > 0) {
  // ... size check with early return ...
  if (!WinHttpReadData(h_request, buffer.data(), bytes_available, &bytes_read) || bytes_read == 0) {
    break;
  }
  response_body.append(buffer.data(), bytes_read);
}
```

**Benefits:**
- Clearer code intent
- Easier to understand control flow
- Less error-prone (no need to track which loop breaks)
- Better maintainability
- Meets SonarQube requirements (maximum 1 break per loop)

**Action:** When writing loops with multiple break statements, prefer combining conditions in the loop header or extracting functions with early returns. Use `goto` only when necessary and document why. Maximum 1-2 break statements per loop.

---

### Prefer `std::array` and `std::vector` Over C-Style Arrays (cpp:S5945)

**Problem:** C-style arrays are unsafe, don't know their size, and don't work well with modern C++. This is one of the most frequent issues (32 occurrences).

**Solution:** Always use `std::array` for fixed-size arrays or `std::vector` for dynamic arrays.

```cpp
// ❌ WRONG - C-style array
char buffer[256];
strcpy(buffer, "Hello");
// No size information, unsafe

// ❌ WRONG - C-style char array
char path[MAX_PATH];
GetPath(path, MAX_PATH);
// No bounds checking, unsafe

// ✅ CORRECT - Use std::array for fixed size
std::array<char, 256> buffer;
strcpy_safe(buffer.data(), buffer.size(), "Hello");
buffer.size();  // Knows its size

// ✅ CORRECT - Use std::vector for dynamic size
std::vector<char> buffer(256);
strcpy_safe(buffer.data(), buffer.size(), "Hello");
buffer.resize(512);  // Can resize

// ✅ CORRECT - Use std::string for strings
std::string path;
path.resize(MAX_PATH);
GetPath(path.data(), path.size());
path.resize(strlen(path.c_str()));  // Trim to actual size
```

**When to Apply:**
- Every time you need a fixed-size array - use `std::array`
- Every time you need a dynamic array - use `std::vector`
- Every time you need a string buffer - use `std::string`
- When interfacing with C APIs - use `std::vector` or `std::string`, then pass `.data()`

**Benefits:**
- Type-safe (knows its size)
- Bounds checking (with `at()`)
- Works with STL algorithms
- Automatic memory management
- No pointer decay

**Common Patterns:**
- **Fixed-size buffers:** `std::array<char, SIZE>` instead of `char[SIZE]`
- **String buffers:** `std::string` instead of `char[N]`
- **Dynamic arrays:** `std::vector<T>` instead of `T*` with manual allocation
- **C API interfacing:** Use `std::vector` or `std::string`, pass `.data()` to C functions

**Action:** Search for C-style arrays (`char[...]`, `int[...]`, etc.) and replace with `std::array` or `std::vector`. This is a high-priority fix (32 issues found).

---

### Use Const References for Parameters (cpp:S995, cpp:S5350)

**Problem:** Non-const references for read-only parameters are misleading and prevent optimizations. This is a very frequent issue (15+ occurrences).

**Solution:** Always use `const T&` for read-only reference parameters.

```cpp
// ❌ WRONG - Non-const reference but parameter is not modified
void ProcessData(SearchThreadPool& thread_pool) {
  size_t count = thread_pool.GetThreadCount();  // Only reading
  // thread_pool is not modified
}

void SearchFiles(FileIndex& index) {
  size_t size = index.GetSize();  // Only reading
  // index is not modified
}

// ✅ CORRECT - Const reference for read-only access
void ProcessData(const SearchThreadPool& thread_pool) {
  size_t count = thread_pool.GetThreadCount();  // Reading only
}

void SearchFiles(const FileIndex& index) {
  size_t size = index.GetSize();  // Reading only
}

// ✅ CORRECT - Non-const reference when modifying
void ConfigureThreadPool(SearchThreadPool& thread_pool) {
  thread_pool.SetThreadCount(8);  // Modifying
}
```

**When to Apply:**
- Every reference parameter that is only read, never modified
- When reviewing function signatures - check if references should be const
- When writing new functions - use const references by default for read-only params

**Parameter Guidelines:**
- `const T&` - Read-only, large objects (avoid copying)
- `T&` - Modify in place (output parameter or modify existing object)
- `T*` - Optional parameter or when null is meaningful
- `const T*` - Optional read-only parameter
- `T` (by value) - Small, cheap-to-copy types (int, bool, pointers)

**Benefits:**
- Clearer API contract (const = won't modify)
- Enables compiler optimizations
- Prevents accidental modifications
- Better code documentation
- More flexible (can pass temporaries to const references)

**Enforcement:**
- **clang-tidy check:** `readability-non-const-parameter` is enabled in `.clang-tidy`
  - Automatically flags non-const pointer/reference parameters that are only read
  - Run `clang-tidy` on modified files before committing
- **Pre-commit hook:** Consider adding a pre-commit hook to run clang-tidy on staged files
- **Code review:** Always check function signatures for const correctness during review

**Action:** Review all function signatures - add `const` to reference parameters that are read-only. This is a high-priority fix (15+ issues found).

---

### Use In-Class Initializers Instead of Constructor Initializer Lists (cpp:S3230)

**Problem:** Using constructor initializer lists for members with default values is redundant and not modern C++ style. In-class initializers are preferred for default values.

**Solution:** Use in-class initializers in the class definition instead of constructor initializer lists for default values.

```cpp
// ❌ WRONG - Constructor initializer list for default values
class FolderBrowser {
private:
  bool is_open_;
  bool has_selected_;
  std::string selected_path_;
  int selected_index_;
};

FolderBrowser::FolderBrowser(const char* title)
    : title_(title)
    , is_open_(false)        // ❌ Should use in-class initializer
    , has_selected_(false)  // ❌ Should use in-class initializer
    , selected_path_()       // ❌ Redundant, should use in-class initializer
    , selected_index_(-1)    // ❌ Should use in-class initializer
{
}

// ✅ CORRECT - Use in-class initializers for default values
class FolderBrowser {
private:
  bool is_open_ = false;
  bool has_selected_ = false;
  std::string selected_path_{};
  int selected_index_ = -1;
};

FolderBrowser::FolderBrowser(const char* title)
    : title_(title)  // ✅ Only non-default values in initializer list
{
}
```

**When to Apply:**
- When a member variable has a default value (e.g., `false`, `0`, `nullptr`, empty string)
- When the constructor initializer list only sets default values
- For all new class definitions

**When NOT to Apply:**
- When the value depends on constructor parameters (keep in initializer list)
- When the value is computed from constructor parameters (keep in initializer list)
- When using `std::unique_ptr` or other types that require constructor arguments

**Benefits:**
- Modern C++11+ style
- Reduces constructor boilerplate
- Makes default values visible in class definition
- Prevents redundant initialization
- Aligns with SonarQube best practices

**Common Patterns:**
- **Boolean flags:** `bool is_open_ = false;` instead of `: is_open_(false)`
- **Integers:** `int count_ = 0;` instead of `: count_(0)`
- **Strings:** `std::string name_{};` instead of `: name_()`
- **Pointers:** `int* ptr_ = nullptr;` instead of `: ptr_(nullptr)`

**Action:** When writing new classes or modifying constructors, use in-class initializers for default values. This is a common issue (7+ occurrences found).

---

### Member Initializer List Order (cpp:S3229)

**Problem:** Class members are initialized in the order they are **declared** in the class, not the order in the constructor initializer list. A mismatch can cause order-dependent initialization bugs and undefined behavior.

**Solution:** Write the constructor initializer list in the same order as member declarations (C++ Core Guidelines C.47; CERT OOP53-CPP).

```cpp
// ❌ WRONG - Initializer list order does not match declaration order
class Application {
  IndexBuildState index_build_state_;
  bool deferred_index_build_start_;

  Application() : deferred_index_build_start_(false), index_build_state_() {}
  // index_build_state_ is declared first but initialized second: wrong order
};

// ✅ CORRECT - Initializer list order matches declaration order
class Application {
  IndexBuildState index_build_state_;
  bool deferred_index_build_start_;

  Application() : index_build_state_(), deferred_index_build_start_(false) {}
};
```

**When to Apply:**
- Whenever you write or modify a constructor initializer list
- When adding or reordering member variables; update the initializer list to match declaration order

**Action:** Keep the constructor initializer list in the same order as member declarations. Sonar S3229; see C.47 and CERT OOP53-CPP.

---

### Global Variables Should Be Const (cpp:S5421)

**Problem:** Global variables that aren't modified should be marked `const` to prevent accidental modification and improve code safety.

**Solution:** Mark global variables as `const` when they don't need to be modified after initialization.

```cpp
// ❌ WRONG - Non-const global variable
static std::unique_ptr<FolderBrowser> g_folder_browser = nullptr;

// ✅ CORRECT - Const global variable (if pointer itself doesn't change)
static const std::unique_ptr<FolderBrowser> g_folder_browser = nullptr;

// ✅ CORRECT - Alternative: Use function-local static (preferred for singletons)
FolderBrowser& GetFolderBrowser() {
  static std::unique_ptr<FolderBrowser> instance = nullptr;
  if (!instance) {
    instance = std::make_unique<FolderBrowser>("Select Folder");
  }
  return *instance;
}
```

**When to Apply:**
- When a global variable is initialized once and never modified
- When the variable is only read, never written
- For configuration constants, singletons, or shared resources

**When NOT to Apply:**
- When the variable needs to be modified (e.g., `std::make_unique` assignment)
- When using `std::unique_ptr` or `std::shared_ptr` that need to be reassigned
- For mutable state that changes during program execution

**Alternatives:**
1. **Function-local static** - Use function-local static variables for singletons (thread-safe in C++11+)
2. **Constexpr/constinit** - Use `constexpr` for compile-time constants, `constinit` for runtime constants (C++20)
3. **Namespace-scoped constants** - Use `const` or `constexpr` in namespaces

**Benefits:**
- Prevents accidental modification
- Makes code intent clear (const = won't change)
- Enables compiler optimizations
- Improves code safety and maintainability

**Action:** Review all global variables - mark as `const` when they don't need to be modified. Consider using function-local static for singletons instead of global variables.

---

## Clang-Tidy and SonarQube Alignment

To avoid fixing the same style issues twice (once for clang-tidy, once for SonarQube), the project states the preferred style once and configures both tools to match where possible.

**Preferred style (satisfy both tools in one fix):**

1. **In-class initializers:** Use in-class initializers for default member values; avoid redundant constructor initializer list entries. (Sonar S3230; clang-tidy: `modernize-use-default-member-init` in `.clang-tidy`.)
2. **Read-only parameters:** Use `const T&` for read-only parameters and variables. (Sonar S995/S5350; clang-tidy: `readability-non-const-parameter`.)
3. **If after brace:** Do not put `if` on the same line as a closing `}`. Put the `if` on a new line or use `else if`. (Sonar S3972; no clang-tidy equivalent — see "If/Else Formatting (cpp:S3972)" below.)

**References:**
- **Sonar vs clang-tidy:** `internal-docs/analysis/2026-02-01_CLANG_TIDY_VS_SONAR_AVOID_DOUBLE_WORK.md`
- **clang-tidy config:** `.clang-tidy` (SonarQube Rule Mapping and CheckOptions)

When cleaning clang-tidy warnings or addressing Sonar issues, apply these styles so one change satisfies both where rules overlap; for Sonar-only rules (e.g. S3972), follow the guideline in AGENTS.md and verify in Sonar UI or scanner.

---

### If/Else Formatting (cpp:S3972)

**Problem:** SonarQube rule S3972 flags `} if (` on the same line: either the `if` should be on a new line or an explicit `else` should be used. There is no clang-tidy equivalent, so this is enforced by Sonar and by this guideline.

**Solution:** Put the `if` on its own line after a closing `}`, or use `else if` when the conditions are related.

```cpp
// ❌ WRONG - } if ( on same line (S3972)
if (bytes < k_bytes_per_kb) {
  return std::to_string(bytes) + " B";
} if (bytes < k_bytes_per_mb) {
  return std::to_string(bytes / k_bytes_per_kb) + " KB";
}

// ✅ CORRECT - if on new line
if (bytes < k_bytes_per_kb) {
  return std::to_string(bytes) + " B";
}
if (bytes < k_bytes_per_mb) {
  return std::to_string(bytes / k_bytes_per_kb) + " KB";
}

// ✅ CORRECT - else if when conditions are related
if (bytes < k_bytes_per_kb) {
  return std::to_string(bytes) + " B";
} else if (bytes < k_bytes_per_mb) {
  return std::to_string(bytes / k_bytes_per_kb) + " KB";
}
```

**When to Apply:** Whenever you have a closing `}` immediately followed by `if` on the same line. Break the line or use `else if`.

**Enforcement:** SonarQube only (no clang-tidy check). Enforce in code review or by running Sonar scanner before push.

---

## Quick Reference

| Rule | Priority | Action |
|------|----------|--------|
| Don't compile/build | ⚠️ CRITICAL | Never run build commands |
| `(std::min)` / `(std::max)` | ⚠️ REQUIRED | Always use parentheses |
| C++17 init-statements | ⚠️ REQUIRED | Use init-statement when variable only used in if block |
| RAII (no malloc/free/new/delete) | ⚠️ REQUIRED | Use smart pointers and containers |
| Const correctness | ⚠️ REQUIRED | Add const to functions/parameters that don't modify |
| Explicit lambda captures | ⚠️ REQUIRED | Use explicit `[&x, y]` instead of `[&]` or `[=]` (CRITICAL in templates for MSVC) |
| Naming conventions | ⚠️ REQUIRED | Follow `docs/standards/CXX17_NAMING_CONVENTIONS.md` |
| Avoid void* pointers | ⚠️ REQUIRED | Use templates or specific types instead |
| Control nesting depth | ⚠️ REQUIRED | Maximum 3 levels, use early returns |
| Control cognitive complexity | ⚠️ REQUIRED | Maximum 25, break into smaller functions |
| Limit function parameters | ⚠️ REQUIRED | Maximum 7, group into structs |
| Use const references | ⚠️ REQUIRED | `const T&` for read-only parameters |
| Handle unused parameters | ⚠️ REQUIRED | Remove or mark with `[[maybe_unused]]` |
| Complete or remove TODOs | ⚠️ REQUIRED | Don't leave TODO comments |
| Remove commented-out code (S125) | ⚠️ REQUIRED | Remove or justify; use version control to retrieve if needed |
| Member initializer list order (S3229) | ⚠️ REQUIRED | Initializer list order must match member declaration order (C.47) |
| Handle exceptions properly | ⚠️ REQUIRED | Don't catch and ignore exceptions |
| Avoid reinterpret_cast | ⚠️ REQUIRED | Use static_cast, std::bit_cast, or proper design |
| Prefer std::array/vector | ⚠️ REQUIRED | Never use C-style arrays |
| Use in-class initializers | ⚠️ REQUIRED | Prefer in-class initializers over constructor initializer lists for defaults |
| Global variables should be const | ⚠️ REQUIRED | Mark global variables as const when not modified |
| Explicit single-argument constructors | ⚠️ REQUIRED | Mark as `explicit` to prevent implicit conversion (S1709) |
| Prefer std::scoped_lock | ⚠️ REQUIRED | Use `std::scoped_lock` (CTAD) instead of `std::lock_guard<std::mutex>` (S5997/S6012) |
| Include order: all at top | ⚠️ REQUIRED | No #include in middle of file; use lowercase for all includes e.g. `<windows.h>` (S954, S3806) |
| Safe use of strlen (S1081) | ⚠️ REQUIRED | Only on guaranteed null-terminated buffers; prefer .size()/string_view; document with NOSONAR if safe |
| Remove redundant casts (S1905) | ✅ RECOMMENDED | Remove casts that don't change the type |
| Lambda length (S1188) | ✅ RECOMMENDED | Keep lambdas ≤20 lines; extract to named function if longer |
| std::size for arrays (S7127) | ✅ RECOMMENDED | Use std::size(arr) for array/container size |
| Remove unused functions (S1144) | ✅ RECOMMENDED | Remove or use; see Dead Code in checklist |
| Prefer private over protected (S3656) | ✅ RECOMMENDED | Avoid protected data members unless intentional |
| Rule of Five (S3624) | ✅ RECOMMENDED | Custom copy/move/dtor: consider all five |
| Inline variables for globals (S6018) | ✅ RECOMMENDED | Use inline/constexpr for file-scope vars (C++17) |
| std::remove_pointer_t (S6020) | ✅ RECOMMENDED | Prefer over typename std::remove_pointer<T>::type |
| If/else formatting (S3972) | ⚠️ REQUIRED | No `} if (` on one line; put `if` on new line or use `else if` |
| Comment `#endif` directives | ⚠️ REQUIRED | Add matching comment after every `#endif` (e.g. `#endif  // _WIN32`); two spaces before `//` |
| Reduce nested breaks | ✅ RECOMMENDED | Extract functions or use flags/goto |
| Merge nested if statements | ✅ RECOMMENDED | When inner if has no else |
| Simplify boolean expressions | ✅ RECOMMENDED | Use std::any_of/all_of/none_of; extract complex conditions to named vars/functions |
| Use `std::string_view` | ✅ RECOMMENDED | Replace `const std::string&` for read-only params |
| Prefer `auto` | ✅ RECOMMENDED | Use `auto` when type is obvious from initializer (modernize-use-auto, hicpp-use-auto) |
| Return braced init list | ✅ RECOMMENDED | Use `return { ... };` / `return {};` instead of repeating return type (modernize-return-braced-init-list) |
| Specific exception handling | ✅ RECOMMENDED | Catch specific exceptions, not `catch (...)` |
| Avoid C-style casts | ✅ RECOMMENDED | Use `static_cast`, `const_cast`, etc. |
| Popup window context | ⚠️ REQUIRED | OpenPopup and BeginPopupModal must be in same window context |
| Popup SetNextWindowPos | ⚠️ REQUIRED | Call every frame before BeginPopupModal, not just when opening |
| Popup CollapsingHeader | ⚠️ REQUIRED | Close button outside header, use SetItemDefaultFocus |
| Popup CloseCurrentPopup | ⚠️ REQUIRED | Call from within BeginPopupModal block, not after EndPopup |
| Boy Scout Rule | ✅ RECOMMENDED | Improve code you touch |
| DRY principle | ✅ RECOMMENDED | Eliminate duplication; one constant per purpose (see §DRY) |
| Single responsibility | ✅ RECOMMENDED | One purpose per class/function/file |
| Remove unused backward compat | ✅ RECOMMENDED | Remove code kept for compatibility that isn't used |
| **.clang-tidy YAML** | ⚠️ REQUIRED | No inline `#` in Checks (or other YAML values); put rationale in comment blocks |
| Code quality checklist | ✅ RECOMMENDED | Evaluate relevant aspects |

---

## Questions to Ask Yourself

Before submitting code changes, ask:

1. ✅ Does this code compile on Windows? (Don't test, but verify Windows API usage is correct)
2. ✅ Are all identifiers following the naming conventions?
3. ✅ Did I avoid `void*` pointers? (Use templates or specific types)
4. ✅ Is nesting depth 3 or less? (Use early returns to reduce nesting)
5. ✅ Is cognitive complexity under 25? (Break complex functions into smaller ones)
6. ✅ Do functions have 7 or fewer parameters? (Group related params into structs)
7. ✅ Did I use `const T&` for read-only reference parameters?
8. ✅ Did I complete or remove all TODO comments?
9. ✅ Did I remove or justify any commented-out code? (S125)
10. ✅ Did I handle all exceptions properly? (No empty catch blocks)
11. ✅ Did I avoid `reinterpret_cast`? (Use safer alternatives)
12. ✅ Did I use `std::array` or `std::vector` instead of C-style arrays?
13. ✅ Did I use in-class initializers for default member values? (Not constructor initializer lists)
14. ✅ Did I keep initializer list order matching declaration order? (S3229)
15. ✅ Did I mark global variables as `const` when they don't need to be modified?
16. ✅ Did I mark single-argument (or defaulted) constructors `explicit`? (S1709)
17. ✅ Did I use `std::scoped_lock` instead of `std::lock_guard<std::mutex>`? (S5997/S6012)
18. ✅ Are all `#include` directives at the top of the file? Did I use `<windows.h>` lowercase? (S954, S3806)
19. ✅ Did I add matching `#endif` comments for every preprocessor block I added or modified? (e.g. `#endif  // _WIN32`)
20. ✅ Did I reduce nested break statements? (Extract functions when possible)
21. ✅ Did I merge nested if statements where possible?
22. ✅ Did I simplify boolean expressions? (std::any_of/all_of/none_of, named conditions, avoid long opaque conditions)
23. ✅ Did I use C++17 init-statements for variables only used in if statements?
24. ✅ Did I use RAII instead of manual memory management (no `malloc`/`free`/`new`/`delete`)?
25. ✅ Did I add `const` to functions and parameters that don't modify state?
26. ✅ Did I use explicit lambda captures instead of `[&]` or `[=]`?
27. ✅ Did I use `std::string_view` for read-only string parameters where appropriate?
28. ✅ Did I prefer `auto` when the type is obvious from the initializer? (iterators, casts, make_unique, etc.)
29. ✅ Did I catch specific exceptions instead of generic `catch (...)`? Did I avoid empty catch blocks (log or rethrow)? (S2486, S108)
30. ✅ Did I use braced initializer list in return statements instead of repeating the return type? (modernize-return-braced-init-list)
31. ✅ Did I avoid C-style casts (use `static_cast`, `const_cast`, etc.)? Did I remove redundant casts? (S1905)
32. ✅ Are lambdas under ~20 lines, or extracted to named functions? (S1188)
33. ✅ Did I use `std::size` for array/container size where applicable? (S7127)
34. ✅ Did I remove or use any unused functions? (S1144)
35. ✅ Did I apply the Boy Scout Rule? What improvements did I make?
36. ✅ Is there any code duplication I should eliminate? (DRY: constants, strings, code blocks, docs)
37. ✅ **Constants:** Did I search for an existing constant before adding a new one? Am I reusing `settings_defaults` / `file_time_constants` / shared headers where the same value or bound is needed?
38. ✅ Are Windows-specific rules (like `(std::min)`) applied? Are include paths lowercase? (S3806)
39. ✅ **Popup management:** Are `OpenPopup()` and `BeginPopupModal()` in the same window context?
40. ✅ **Popup positioning:** Is `SetNextWindowPos()` called every frame before `BeginPopupModal()`?
41. ✅ **Popup closing:** If using `CollapsingHeader`, is close button outside it with `SetItemDefaultFocus()`?
42. ✅ **Popup IDs:** Do popup ID strings match exactly between `OpenPopup()` and `BeginPopupModal()`?
43. ✅ **If I edited `.clang-tidy`:** Did I avoid inline `#` comments inside the Checks list (or any YAML value)? Rationale belongs in separate comment blocks.
44. ✅ Is the code simple and readable?
45. ✅ Does each class/function have a single, clear responsibility?
46. ✅ **Sonar prevention:** For recurring open issues (lambda length S1188, redundant casts S1905, include case S3806, unused functions S1144, Rule of Five S3624, etc.), see SonarQube Code Quality Rules in this file (and run `scripts/fetch_sonar_results.sh --open-only` for current open issues).
47. ✅ Is every `strlen` use on a guaranteed null-terminated buffer (or documented with NOSONAR(cpp:S1081))? Prefer `.size()` / `std::string_view` when length is known.

---

## Additional Resources

- **Naming Conventions:** See `docs/standards/CXX17_NAMING_CONVENTIONS.md` for complete details
- **Project Overview:** See `README.md` for build instructions and project structure
- **Design Decisions:** See `docs/design/ARCHITECTURE_COMPONENT_BASED.md` for architectural overview
- **Production Readiness:** 
  - Production checklist: `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md`
    - Quick section: 5-10 min review for every commit
    - Comprehensive section: Full review for major features/releases

---

## Documentation Placement Rules

When creating new documentation, place it in the correct folder based on audience and purpose.

### `docs/` — external contributors only

Put a document in `docs/` only if an external contributor would need it to build, contribute code, or understand the architecture. All content in `docs/` must be stable enough to be publicly useful.

| Subfolder | What goes here |
|-----------|---------------|
| `docs/design/` | Stable architecture and design documents (e.g. component design, data flow, algorithm design). **Not** in-progress reviews or analysis notes. |
| `docs/guides/` | How-to guides: building on each platform, profiling, running clang-tidy, logging standards. |
| `docs/standards/` | Coding standards enforced on all contributors (naming, multiplatform coherence). |
| `docs/analysis/` | Tool guides applicable to any contributor (clang-tidy guide, check classification). Not dated analysis results. |
| `docs/security/` | Security model description. Not implementation status tracking. |
| `docs/platform/` | Platform-specific build/runtime notes useful to any contributor targeting that platform. |
| `docs/plans/production/` | Quality checklists that contributors should apply before submitting work. |

### `internal-docs/` — maintainer-only

Put a document in `internal-docs/` for anything that is not useful to external contributors: AI agent tools, dated analyses, task notes, implementation plans, and status tracking.

| Subfolder | What goes here |
|-----------|---------------|
| `internal-docs/prompts/` | AI agent prompt templates, review prompts, task prompts, workflow instructions (Taskmaster, AGENT_STRICT_CONSTRAINTS, clang-tidy/Sonar workflows, etc.) |
| `internal-docs/analysis/` | Dated code reviews, performance analyses, improvement backlogs, tooling investigations, Sonar/clang-tidy results |
| `internal-docs/design/` | In-progress design reviews, library upgrade analyses, UI/UX investigation notes |
| `internal-docs/plans/` | Maintainer checklists, open-source publish workflows, roadmap and milestone docs |
| `internal-docs/Historical/` | Completed work, resolved issue analyses |
| `internal-docs/archive/` | Superseded analyses and proposals |

### `specs/` — specification-driven development

Formal specs (using `specs/SPECIFICATION_DRIVEN_DEVELOPMENT_PROMPT.md`) that describe a feature before implementation. These are contributor-accessible but not general contributor docs.

### Decision rule

> **Ask: "Would a first-time external contributor need this to build or contribute?"**
> - Yes → `docs/`
> - No → `internal-docs/`

Dated analysis documents (with a `YYYY-MM-DD_` prefix), task prompts, Sonar/clang-tidy result files, performance opportunity trackers, and AI agent workflow files always go in `internal-docs/`.
