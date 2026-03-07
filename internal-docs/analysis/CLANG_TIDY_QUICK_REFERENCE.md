# Clang-Tidy Configuration Quick Reference

**Date:** January 24, 2026  
**Purpose:** Quick lookup for developers using clang-tidy

---

## Configuration Status

| Aspect | Status | Notes |
|--------|--------|-------|
| **Overall** | ✅ Excellent | Production-ready, well-aligned with project |
| **Naming Conventions** | ✅ Perfect | All conventions properly enforced |
| **RAII Enforcement** | ✅ Perfect | Smart pointers, no manual memory management |
| **SonarQube Alignment** | ✅ Excellent | Maps to major SonarQube rules |
| **Cross-Platform** | ✅ Good | Works on Windows, macOS, Linux |
| **ImGui Support** | ✅ Good | Varargs and magic numbers handled |
| **Documentation** | ⚠️ Good | Can be enhanced with enforcement phases |

**Rating: 9.2/10** - Excellent configuration

---

## Common Checks by Category

### Code Quality (High Priority)

```
✅ ENABLED (catch high-severity issues):
- bugprone-*                    # Logic errors, crashes
- cppcoreguidelines-pro-type-* # Unsafe casts
- cppcoreguidelines-no-malloc  # No malloc/free allowed
- misc-unused-*                # Dead code
- readability-non-const-parameter # Const correctness
```

### Naming Conventions (Enforced)

```
🔤 PROJECT CONVENTIONS:
- Types (class/struct):      PascalCase     ✅ Enforced
- Functions/Methods:         PascalCase     ✅ Enforced
- Local Variables:           snake_case     ✅ Enforced
- Member Variables:          snake_case_    ✅ Enforced (with suffix)
- Constants:                 kPascalCase    ✅ Enforced (with prefix)
- Global Variables:          g_snake_case   ⚠️  Not auto-enforced
- Namespaces:                snake_case     ✅ Enforced
- Macros:                    UPPER_CASE     ✅ Enforced
```

### Modern C++ (C++17 Focus)

```
✅ ENABLED (enforce modern C++):
- modernize-use-string-view      # Prefer std::string_view
- modernize-make-unique          # Use std::make_unique
- modernize-make-shared          # Use std::make_shared
- modernize-use-default-member-init  # In-class initializers
- cppcoreguidelines-avoid-goto   # Goto restrictions
```

### Performance

```
✅ ENABLED (high-performance search engine):
- performance-*                  # Unnecessary copies, etc.
- readability-magic-numbers      # With exceptions for UI
- readability-function-cognitive-complexity  # Threshold: 25
```

### Disabled (Intentionally)

```
❌ DISABLED (and why):
- modernize-use-trailing-return-type  # Not project style
- readability-identifier-length        # Short names OK (i, j, id)
- bugprone-easily-swappable-parameters # High noise
- llvm-header-guard                    # Use #pragma once instead
- cppcoreguidelines-avoid-do-while     # do-while acceptable
```

---

## Running Clang-Tidy

### Basic Usage

```bash
# Generate build database (required)
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Run on single file
clang-tidy -p build src/api/GeminiApiUtils.cpp

# List all enabled checks
clang-tidy -p build --list-checks 2>&1 | head -50

# Test specific check
clang-tidy -p build --checks='readability-identifier-naming' src/api/GeminiApiUtils.cpp

# Auto-fix issues (where possible)
clang-tidy -p build -fix src/api/GeminiApiUtils.cpp
```

### Batch Processing

```bash
# All source files (macOS/Linux)
find src -name "*.cpp" -not -path "*/external/*" | xargs clang-tidy -p build

# With output capture
clang-tidy -p build src/**/*.cpp 2>&1 | tee clang-tidy-results.txt

# Count warnings by type
clang-tidy -p build src/**/*.cpp 2>&1 | grep "warning:" | cut -d: -f5 | sort | uniq -c
```

---

## Suppressing False Positives

### Method 1: NOLINT Comment

```cpp
// Suppress single check
void MyFunction() {  // NOLINT(readability-identifier-naming)
  int my_var = 42;
}

// Suppress multiple checks
void MyFunction() {  // NOLINT(readability-identifier-naming, performance-inefficient-vector-operation)
  // ...
}

// Suppress all clang-tidy warnings
void MyFunction() {  // NOLINT
  // ...
}
```

### Method 2: Line-Level Suppression

```cpp
// NOLINTNEXTLINE(readability-identifier-naming)
void MyFunction() {
  // ...
}
```

### Method 3: Range Suppression (clang >= 12)

```cpp
// NOLINTBEGIN(readability-identifier-naming)
void Func1() { }
void Func2() { }
// NOLINTEND(readability-identifier-naming)
```

---

## Key Enabled Checks for Code Review

### Reliability (Must Fix)

| Check | Example | Fix |
|-------|---------|-----|
| `bugprone-empty-catch` | `try { } catch(...) { }` | Add error handling |
| `bugprone-narrowing-conversions` | `int x = float_var;` | Use explicit cast |
| `cppcoreguidelines-no-malloc` | `char* p = malloc(100);` | Use `std::vector` |
| `bugprone-branch-clone` | Same code in if/else | Consolidate branches |

### Maintainability (Should Fix)

| Check | Example | Fix |
|-------|---------|-----|
| `readability-non-const-parameter` | `void Func(MyClass& obj)` | Use `const MyClass&` |
| `misc-unused-parameters` | `void Func(int unused)` | Remove or mark `[[maybe_unused]]` |
| `readability-identifier-naming` | `int fileIndex` | Rename to `file_index_` |

### Performance (Often Important)

| Check | Example | Fix |
|-------|---------|-----|
| `performance-unnecessary-copy-initialization` | `auto v = heavy_func();` | Use reference or move |
| `modernize-use-string-view` | `func(const std::string&)` | Use `std::string_view` |

### Code Quality (Nice to Have)

| Check | Example | Fix |
|-------|---------|-----|
| `readability-magic-numbers` | `if (x > 640)` | Use named constant |
| `modernize-use-default-member-init` | Constructor init list | Use in-class init |

---

## Understanding Failures

### "Configuration not found"
```bash
# Solution: Generate compile_commands.json first
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

### Too many warnings
```bash
# Check what's enabled
clang-tidy --list-checks | wc -l

# Disable specific checks
clang-tidy -p build --checks='-readability-identifier-naming' src/file.cpp

# Or modify .clang-tidy and add to disabled list
```

### False positives
```bash
# Add NOLINT comment to suppress
void MyFunction() {  // NOLINT(check-name)
  // ...
}

# Or check if warning is valid (often is!)
```

---

## Documentation References

| Document | Purpose |
|----------|---------|
| `.clang-tidy` | Configuration file (this project's root) |
| `AGENTS.md` | Development guidelines, coding standards |
| `docs/standards/CXX17_NAMING_CONVENTIONS.md` | Official naming conventions |
| `docs/analysis/CLANG_TIDY_GUIDE.md` | Detailed usage instructions |
| `docs/analysis/CLANG_TIDY_CLASSIFICATION.md` | Check categorization and rationale |
| `docs/analysis/CLANG_TIDY_CONFIGURATION_REVIEW.md` | Comprehensive configuration review |
| `docs/analysis/CLANG_TIDY_CONFIGURATION_ENHANCEMENTS.md` | Enhancement recommendations |

---

## Enforcement Philosophy

**Current Phase:** Phase 1 - **Warnings Only**
- clang-tidy runs on all code
- Warnings reported but not treated as errors
- Developers address voluntarily or during code review

**Future Phases:**
- Phase 2: Warnings as errors for high-severity checks
- Phase 3: Zero-warning state

---

## Common Developer Questions

### Q: Do I have to fix clang-tidy warnings?
**A:** Not immediately. Phase 1 is warnings-only. But:
- Code review process considers them
- RAII and naming conventions are higher priority
- SonarQube violations should be fixed
- Performance issues should be addressed

### Q: Can I ignore this check?
**A:** Use `// NOLINT(check-name)` if:
1. It's a false positive (document why)
2. Check is intentionally disabled for your use case
3. Performance/design decision requires it

### Q: Why is my code flagged?
**A:** Most flags are valid. Top reasons:
- Naming convention violation (easy fix: rename)
- Const correctness (add `const`)
- Performance issue (unnecessary copy)
- Dead code (remove it)

### Q: How do I run on my changes only?
**A:** Use pre-commit hook:
```bash
# Automatically runs on staged files
git add file.cpp
git commit  # clang-tidy runs automatically
```

### Q: Which checks are most important?
**A:** In order:
1. Naming conventions (project style)
2. RAII (smart pointers, no manual memory)
3. Const correctness (API clarity)
4. Unused parameters (dead code)
5. Performance warnings (optional but encouraged)

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| "file not found" errors | Run `cmake` to generate compile_commands.json |
| Warnings in external/ | Expected; those files are excluded |
| Different results on different platforms | Normal; run on each platform for full verification |
| Clang-tidy crashes | Try single file instead of batch |
| Too many warnings on first run | Use NOLINT sparingly; address root causes |

---

## Tips for Clean Code

1. ✅ **Naming:** Use snake_case_, PascalCase, kPascalCase correctly
2. ✅ **Memory:** Always use smart pointers, never new/delete
3. ✅ **Const:** Add const to functions and parameters that don't modify
4. ✅ **Magic:** Extract magic numbers to named constants
5. ✅ **Unused:** Remove unused parameters or mark [[maybe_unused]]
6. ✅ **Modern:** Use std::string_view, std::make_unique, auto
7. ✅ **Comments:** Add NOLINT comments when suppressing checks
8. ✅ **Testing:** Run clang-tidy before committing

---

## Pre-Commit Integration

If pre-commit hook is installed:
```bash
# Automatically runs clang-tidy on staged files
git add src/MyFile.cpp
git commit
# Pre-commit hook runs clang-tidy, shows warnings
# Fix warnings and try again
```

To manually run:
```bash
scripts/pre-commit-clang-tidy.sh
```

---

**Last Updated:** January 24, 2026  
**Configuration Version:** Current (see .clang-tidy)  
**For Questions:** See AGENTS.md or docs/analysis/CLANG_TIDY_GUIDE.md

