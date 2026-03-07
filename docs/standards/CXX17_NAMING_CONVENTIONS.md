# C++17 Naming Conventions - Recommended Best Practices

## Overview

For C++17, the most widely adopted naming conventions are based on:
- **Google C++ Style Guide** (most popular in open source)
- **Microsoft C++ Core Guidelines** (common in Windows development)
- **LLVM Style Guide** (used by many C++ projects)

This document recommends a **hybrid approach** that works well for modern C++17 codebases.

---

## Recommended Naming Convention

### 1. **Types (Classes, Structs, Enums, Typedefs, Type Aliases)**

**Convention:** `PascalCase` (also called `UpperCamelCase`)

```cpp
// Classes
class UsnMonitor { };
class FileIndex { };
class SearchWorker { };

// Structs
struct MonitoringConfig { };
struct SearchResult { };
struct UsnMonitorMetrics { };

// Enums
enum class ErrorCode { };
enum class FileType { };

// Type aliases
using VolumeHandle = HANDLE;
using BufferPtr = std::unique_ptr<std::vector<char>>;
```

**Rationale:** Clear distinction from variables, widely recognized standard.

---

### 2. **Functions and Methods**

**Convention:** `PascalCase` (matching types)

```cpp
// Public methods
void StartMonitoring();
void StopMonitoring();
size_t GetQueueSize() const;
bool IsActive() const;

// Private methods
void ReaderThread();
void ProcessorThread();

// Free functions
bool PopulateInitialIndex(HANDLE volumeHandle, FileIndex& fileIndex);
std::string WideToUtf8(const std::wstring& wide);
```

**Rationale:** Consistent with types, clear function calls, matches Google/Microsoft style.

**Alternative (if preferred):** `camelCase` for methods is also acceptable:
```cpp
void startMonitoring();
size_t getQueueSize() const;
```

---

### 3. **Variables**

#### **Local Variables and Parameters**
**Convention:** `snake_case` (lowercase with underscores)

```cpp
void ProcessBuffer(const std::vector<char>& buffer) {
  size_t buffer_size = buffer.size();
  DWORD bytes_returned = 0;
  bool is_valid = true;
  
  for (size_t offset = 0; offset < buffer_size; ++offset) {
    // ...
  }
}
```

#### **Member Variables (Class Data Members)**
**Convention:** `snake_case` with **trailing underscore**

```cpp
class UsnMonitor {
private:
  FileIndex& file_index_;           // Member variable
  std::thread reader_thread_;       // Member variable
  std::atomic<bool> is_active_;     // Member variable
  mutable std::mutex mutex_;         // Member variable
};
```

**Rationale:** 
- Trailing underscore clearly distinguishes members from local variables
- Avoids `this->` everywhere
- Prevents shadowing parameters
- Widely adopted (Google, LLVM style)

#### **Static Member Variables**
**Convention:** `snake_case` with trailing underscore (same as members)

```cpp
class MyClass {
private:
  static size_t instance_count_;
  static constexpr int default_size_ = 100;
};
```

#### **Global Variables** (use sparingly)
**Convention:** `g_` prefix with `snake_case`

```cpp
// Only for truly global state (avoid when possible)
FileIndex g_file_index;
std::atomic<bool> g_monitoring_active{false};
```

**Rationale:** `g_` prefix makes globals immediately obvious, discourages overuse.

---

### 4. **Constants**

#### **Compile-time Constants (`constexpr`)**
**Convention:** `k` prefix with `PascalCase` (Google style) OR `UPPER_SNAKE_CASE`

**Option A (Google style - recommended):**
```cpp
constexpr int kBufferSize = 64 * 1024;
constexpr size_t kMaxQueueSize = 1000;
constexpr DWORD kTimeoutMs = 1000;
```

**Option B (Traditional - also acceptable):**
```cpp
constexpr int BUFFER_SIZE = 64 * 1024;
constexpr size_t MAX_QUEUE_SIZE = 1000;
constexpr DWORD TIMEOUT_MS = 1000;
```

**Recommendation:** Use `k` prefix for constants (Option A) to avoid confusion with macros.

#### **Runtime Constants (`const`)**
**Convention:** Same as compile-time constants

```cpp
const std::string kDefaultVolumePath = "\\\\.\\C:";
const char kSystemFilePrefix = '$';
```

#### **Local Constants (Tolerance)**
**Convention:** Both `kPascalCase` and `snake_case` are accepted for local constants (const/constexpr in function scope).

- **Named constants** (magic number replacement): Prefer `kPascalCase` (e.g., `kGroupSpacing`, `kBufferSize`).
- **Const local variables** (computed values): `snake_case` is acceptable (e.g., `save_ok`, `base_has_sep`).

This tolerance avoids ~150 renames and NOLINT proliferation. clang-tidy is configured with `LocalConstantCase: aNy_CasE` to accept both styles. See `.clang-tidy` CheckOptions.

---

### 5. **Namespaces**

**Convention:** `snake_case` (lowercase with underscores)

```cpp
namespace find_helper {
  // ...
}

namespace file_operations {
  // ...
}
```

**Rationale:** Matches file naming, avoids conflicts with types.

---

### 6. **Macros** (avoid when possible, use `constexpr` instead)

**Convention:** `UPPER_SNAKE_CASE`

```cpp
#define MAX_RETRY_ATTEMPTS 3
#define LOG_ERROR(msg) /* ... */
```

**Note:** Prefer `constexpr` variables and inline functions over macros in C++17.

---

### 7. **Template Parameters**

**Convention:** `PascalCase` (single letter for simple cases, full name for complex)

```cpp
// Simple cases
template<typename T>
class Container { };

// Complex cases
template<typename Iterator, typename Predicate>
void for_each_if(Iterator first, Iterator last, Predicate pred);
```

---

### 8. **Private Implementation Details**

**Convention:** Same as public, but in `private:` section

```cpp
class UsnMonitor {
public:
  void Start();  // Public method
  
private:
  void ReaderThread();  // Private method (same style)
  FileIndex& file_index_;  // Private member
};
```

---

## Special Cases

### **Windows API Types (HANDLE, DWORD, etc.)**
**Convention:** Keep Windows naming as-is (all caps)

```cpp
HANDLE volume_handle;  // Windows type, keep as-is
DWORD bytes_returned;  // Windows type, keep as-is
```

### **RAII Wrappers for Windows Types**
**Convention:** Use your project's naming (PascalCase for class)

```cpp
class VolumeHandle {  // Your wrapper class
  HANDLE handle_;     // Member variable (trailing underscore)
};
```

### **Acronyms in Names**
**Convention:** Treat as single word

```cpp
class UsnMonitor { }      // Not: USNMonitor
void ReadUsnJournal() { } // Not: ReadUSNJournal
size_t buffer_size;       // Not: bufferSize (if using snake_case)
```

---

## Comparison: Current vs. Recommended

### Current Codebase Issues:
```cpp
// ❌ Inconsistent styles
FileIndex g_fileIndex;              // Global: g_ prefix OK, but camelCase
static UsnJournalQueue* g_usnQueue; // Static: mixing styles
HANDLE hVol;                        // Hungarian notation (h = handle)
int bufferSize;                     // camelCase for local variable
const int BUFFER_SIZE = 64 * 1024;  // UPPER_SNAKE_CASE (OK for constants)
```

### Recommended:
```cpp
// ✅ Consistent styles
FileIndex g_file_index;                    // Global: g_ + snake_case
static UsnJournalQueue* g_usn_queue;      // Static: g_ + snake_case
VolumeHandle volume_handle;               // No Hungarian, descriptive name
int buffer_size;                           // snake_case for local
constexpr int kBufferSize = 64 * 1024;     // k prefix + PascalCase
```

---

## Complete Example

```cpp
// Types: PascalCase
class UsnMonitor {
public:
  // Public methods: PascalCase
  void Start();
  void Stop();
  size_t GetQueueSize() const;
  
  // Constants: k prefix + PascalCase
  static constexpr size_t kDefaultMaxQueueSize = 1000;
  
private:
  // Private methods: PascalCase
  void ReaderThread();
  void ProcessorThread();
  
  // Member variables: snake_case + trailing underscore
  FileIndex& file_index_;
  std::thread reader_thread_;
  std::atomic<bool> is_active_{false};
  mutable std::mutex mutex_;
};

// Free functions: PascalCase
bool PopulateInitialIndex(HANDLE volume_handle, FileIndex& file_index);

// Local variables: snake_case
void ProcessBuffer(const std::vector<char>& buffer) {
  size_t buffer_size = buffer.size();
  DWORD bytes_returned = 0;
  
  for (size_t offset = 0; offset < buffer_size; ++offset) {
    // Process...
  }
}

// Namespace: snake_case
namespace find_helper_utils {
  // ...
}
```

---

## Migration Strategy

1. **New Code:** Use recommended conventions immediately
2. **Existing Code:** Refactor gradually, file by file
3. **Critical Paths:** Prioritize frequently modified files
4. **Tools:** Use clang-format/clang-tidy to enforce style

---

## Tools for Enforcement

The project includes configuration files for automated enforcement:
- **`.clang-format`**: Code formatting configuration (Google style with customizations)
- **`.clang-tidy`**: Naming convention and code quality checks

### Quick Start

**Format code:**
```bash
clang-format -i file.cpp
```

**Check naming conventions:**
```bash
clang-tidy file.cpp -checks='readability-identifier-naming'
```

For detailed usage instructions, see `docs/TOOLING_ENFORCEMENT.md`.

### clang-format Configuration
```yaml
BasedOnStyle: Google
IndentWidth: 2
ColumnLimit: 100
NamespaceIndentation: None
AllowShortFunctionsOnASingleLine: Empty
```

### clang-tidy Checks
- `readability-identifier-naming` - Enforces naming conventions
- `readability-named-parameter` - Requires named parameters

**Note:** Some naming rules (like trailing underscores for members, `g_` prefix for globals, `k` prefix for constants) require manual review as clang-tidy cannot enforce prefixes automatically.

---

## Summary: Quick Reference

| Element | Convention | Example |
|---------|-----------|---------|
| Classes/Structs | `PascalCase` | `UsnMonitor`, `FileIndex` |
| Functions/Methods | `PascalCase` | `StartMonitoring()`, `GetSize()` |
| Local Variables | `snake_case` | `buffer_size`, `offset` |
| Member Variables | `snake_case_` | `file_index_`, `reader_thread_` |
| Global Variables | `g_snake_case` | `g_file_index` |
| Constants | `kPascalCase` (local: both `kPascalCase` and `snake_case` accepted) | `kBufferSize`, `save_ok` |
| Namespaces | `snake_case` | `find_helper`, `file_ops` |
| Macros | `UPPER_SNAKE_CASE` | `MAX_SIZE`, `LOG_ERROR` |
| Template Params | `PascalCase` | `T`, `Iterator`, `Predicate` |

---

## Why This Convention?

1. **Widely Adopted:** Google style is the most popular C++ style guide
2. **Clear Distinctions:** Easy to tell types, functions, and variables apart
3. **Member Variables:** Trailing underscore prevents shadowing and makes members obvious
4. **Consistency:** One style throughout, no mixing
5. **Tool Support:** Excellent clang-format/clang-tidy support
6. **Modern C++:** Works well with C++17 features (constexpr, auto, etc.)

---

## References

- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- [Microsoft C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [LLVM Coding Standards](https://llvm.org/docs/CodingStandards.html)
