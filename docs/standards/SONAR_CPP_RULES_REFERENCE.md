# SonarQube C++ Code Quality Rules (reference)

Full reference for SonarQube Code Quality Rules applied in this project. For the **Quick Reference** table and workflow (e.g. when to run Sonar, clang-tidy alignment), see **AGENTS.md**. For current open issues and rule distribution run `scripts/fetch_sonar_results.sh --open-only`.

---

## SonarQube Code Quality Rules

These rules prevent common SonarQube violations that degrade code quality. These rules are based on actual issues found in the codebase and should be strictly followed. For a current snapshot of open issues and rule distribution, run `scripts/fetch_sonar_results.sh --open-only`.

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

**Real-world example:** SettingsWindow.cpp—reduce nesting by replacing else-if with separate `if` blocks so the condition chain stays at 3 levels.

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

**Real-world example:** main_common.h `HandleExceptions`—complexity reduced from 30 to ~15 by extracting `ExecuteCleanup` and `HandleExceptionWithCleanup` so each catch delegates to the helper instead of duplicating cleanup and logging.

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

### Use Const References for Read-Only Parameters (cpp:S995, cpp:S5350)

**Problem:** Non-const references for read-only parameters are misleading and prevent optimizations. This is a very frequent issue (15+ occurrences).

**Solution:** Use `const T&` for read-only reference parameters.

```cpp
// ❌ WRONG - Non-const reference but parameter is not modified
void ProcessData(SearchThreadPool& thread_pool) {
  size_t count = thread_pool.GetThreadCount();  // Only reading
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

**When to Apply:** When a reference parameter is only read, never modified; when reviewing function signatures; when writing new functions use const references by default for read-only params.

**Parameter Guidelines:** `const T&` (read-only, large objects), `T&` (modify in place), `T*` / `const T*` (optional), `T` by value (small, cheap-to-copy).

**Enforcement:** clang-tidy `readability-non-const-parameter`; run clang-tidy on modified files; check const correctness in code review.

**Action:** Review all reference parameters and add `const` if read-only. High-priority fix (15+ issues found).

---

### Strengthen Exception Handling (cpp:S1181)

See **Specific Exception Handling** above; Sonar S1181 enforces the same—catch specific exception types, not generic `catch (std::exception&)`.

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

**Real-world example:** GeminiApiHttp_win.cpp—move `WinHttpQueryDataAvailable` and `bytes_available` into the loop condition, single break for read failure; reduces multiple breaks to one.

**Benefits:**
- Clearer code intent
- Easier to understand control flow
- Less error-prone (no need to track which loop breaks)
- Better maintainability
- Meets SonarQube requirements (maximum 1 break per loop)

**Action:** When writing loops with multiple break statements, prefer combining conditions in the loop header or extracting functions with early returns. Use `goto` only when necessary and document why. Maximum 1-2 break statements per loop.

---

### Prefer `std::array` and `std::vector` Over C-Style Arrays (cpp:S5945)

**Problem:** C-style arrays are unsafe, don't know their size, and don't work well with modern C++. Frequent issue (32 occurrences).

**Solution:** Use `std::array` for fixed size, `std::vector` for dynamic, `std::string` for string buffers.

```cpp
// ❌ WRONG
char buffer[256];
strcpy(buffer, "Hello");

// ✅ CORRECT - fixed size
std::array<char, 256> buffer;
strcpy_safe(buffer.data(), buffer.size(), "Hello");

// ✅ CORRECT - strings
std::string path;
path.resize(MAX_PATH);
GetPath(path.data(), path.size());
path.resize(strlen(path.c_str()));
```

**When to Apply:** Fixed-size → `std::array`; dynamic → `std::vector`; string buffer → `std::string`; C API → `std::vector`/`std::string` and pass `.data()`.

**Action:** Replace C-style arrays with `std::array` or `std::vector`. High-priority (32 issues found).

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
