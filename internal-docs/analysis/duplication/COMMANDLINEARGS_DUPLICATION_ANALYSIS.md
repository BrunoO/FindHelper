# CommandLineArgs.cpp Duplication Analysis

**Date:** 2026-01-06  
**File:** `src/core/CommandLineArgs.cpp`  
**Issue:** Significant code duplication in argument parsing

## Summary

The file contains **~100 lines of duplicated code** for parsing command-line arguments. Each argument follows the same pattern:
1. Check for `--arg=value` format using `strncmp`
2. Check for `--arg value` format using `strcmp`

**Duplication Rate:** ~70% of argument parsing code is duplicated.

## Duplication Details

### Arguments Parsed

1. **String Arguments** (no validation):
   - `--load-balancing` (lines 87-97)
   - `--dump-index-to` (lines 140-150)
   - `--index-from-file` (lines 153-163)
   - `--crawl-folder` (lines 166-176)

2. **Integer Arguments** (with validation):
   - `--thread-pool-size` (lines 66-84, with range 0-64)
   - `--window-width` (lines 100-117, with range 640-4096)
   - `--window-height` (lines 120-137, with range 480-2160)

### Duplicated Pattern

Each argument has **two identical blocks**:

#### Pattern 1: `--arg=value` format
```cpp
if (strncmp(arg, "--arg-name=", N) == 0) {
  const char* value = arg + N;
  // Validation/assignment
  continue;
}
```

#### Pattern 2: `--arg value` format
```cpp
if (strcmp(arg, "--arg-name") == 0 && i + 1 < argc) {
  // Validation/assignment from argv[i+1]
  ++i; // Skip next argument
  continue;
}
```

### Code Statistics

- **Total lines:** 261
- **Argument parsing:** ~130 lines
- **Duplicated code:** ~90 lines
- **Unique code:** ~40 lines (help, version, dump function)

## Root Cause

The duplication exists because:
1. **Two formats supported:** `--arg=value` and `--arg value` (for user convenience)
2. **Manual parsing:** Each argument manually checks both formats
3. **No abstraction:** No helper functions to reduce repetition

## Impact

- **Maintenance burden:** Adding new arguments requires duplicating ~15 lines
- **Code size:** ~90 lines of duplicated code
- **Risk:** Inconsistencies can arise if one format is updated but not the other
- **Readability:** Makes the function harder to read and understand

## Refactoring Options

### Option 1: Helper Functions (Recommended)

Create helper functions for common patterns:

```cpp
// Parse string argument (no validation)
bool ParseStringArg(const char* arg, int& i, int argc, char** argv,
                    const char* arg_name, int name_len,
                    std::string& target);

// Parse integer argument (with validation)
bool ParseIntArg(const char* arg, int& i, int argc, char** argv,
                 const char* arg_name, int name_len,
                 int& target, int min_val, int max_val);
```

**Pros:**
- Eliminates all duplication
- Easy to add new arguments
- Single source of truth
- Type-safe

**Cons:**
- Slightly more complex function signatures
- Need to pass argument index by reference

### Option 2: Macro-Based (Not Recommended)

Use macros to generate the parsing code:

```cpp
#define PARSE_STRING_ARG(name, target) \
  if (strncmp(arg, "--" name "=", sizeof("--" name "=") - 1) == 0) { \
    target = arg + sizeof("--" name "=") - 1; \
    continue; \
  } \
  if (strcmp(arg, "--" name) == 0 && i + 1 < argc) { \
    target = argv[++i]; \
    continue; \
  }
```

**Pros:**
- Very concise
- No function call overhead

**Cons:**
- Macros are harder to debug
- Less type-safe
- Can be error-prone

### Option 3: Table-Driven Parsing

Use a table of argument descriptors:

```cpp
struct ArgDescriptor {
  const char* name;
  ArgType type;
  void* target;
  int min_val, max_val; // For integer args
};

// Parse using table
for (const auto& desc : arg_descriptors) {
  if (ParseArg(arg, i, argc, argv, desc)) {
    continue;
  }
}
```

**Pros:**
- Very flexible
- Easy to add new arguments
- Centralized configuration

**Cons:**
- More complex implementation
- May be overkill for current needs

## Recommendation

**Use Option 1 (Helper Functions)** - It's the best balance of:
- Eliminating duplication
- Maintaining readability
- Easy to extend
- Type-safe

## Implementation Steps

1. Create `ParseStringArg` helper function
2. Create `ParseIntArg` helper function
3. Refactor existing arguments to use helpers
4. Test thoroughly (especially edge cases)
5. Remove old duplicated code

## Testing Considerations

- **Format variations:** Test both `--arg=value` and `--arg value`
- **Edge cases:** Empty values, missing values, invalid ranges
- **Error handling:** Invalid integers, out-of-range values
- **Multiple arguments:** Test combinations of arguments

## Estimated Effort

- **Time:** 1-2 hours
- **Risk:** Low (refactoring, not changing behavior)
- **Testing:** 30 minutes
- **Total:** 1.5-2.5 hours

## Related Files

- `src/core/CommandLineArgs.h` - Struct definition
- `src/core/main_gui.cpp` - Uses ParseCommandLineArgs
- `src/core/main_mac.mm` - Uses ParseCommandLineArgs

## Notes

- The duplication is intentional for clarity (each argument is explicit)
- However, the pattern is identical enough that helpers can handle it
- Consider this refactoring as part of code quality improvements
- The refactoring maintains exact same behavior and API

