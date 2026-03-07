# Copy vs Reference Analysis

## Summary

This document identifies places in the codebase where data is copied when it could be referenced using `std::string_view` or `const` references.

## Categories

### 1. String Utility Functions (StringUtils.h)

These functions only read their input strings and could accept `std::string_view` instead of `const std::string&`:

#### High Priority (Frequently Called)
- **`ToLower(const std::string &str)`** (line 43)
  - Only reads the string
  - Called frequently in search operations
  - **Fix**: Change to `ToLower(std::string_view str)`

- **`Trim(const std::string &str)`** (line 51)
  - Only reads the string
  - Called when parsing user input
  - **Fix**: Change to `Trim(std::string_view str)`

- **`GetExtension(const std::string &filename)`** (line 73)
  - Only reads the string
  - Called during file indexing and search
  - **Fix**: Change to `GetExtension(std::string_view filename)`

- **`GetFilename(const std::string &path)`** (line 64)
  - Only reads the string
  - **Fix**: Change to `GetFilename(std::string_view path)`

#### Medium Priority (File I/O Operations)
- **`GetFileAttributes(const std::string &path)`** (line 95)
  - Only reads the string
  - Called for lazy loading file sizes/modification times
  - **Fix**: Change to `GetFileAttributes(std::string_view path)`
  - **Note**: Will need to convert to `std::string` internally for Windows API calls

- **`GetFileSize(const std::string &path)`** (line 144)
  - Only reads the string
  - Delegates to `GetFileAttributes`
  - **Fix**: Change to `GetFileSize(std::string_view path)`

- **`GetFileModificationTime(const std::string &path)`** (line 152)
  - Only reads the string
  - Delegates to `GetFileAttributes`
  - **Fix**: Change to `GetFileModificationTime(std::string_view path)`

- **`Utf8ToWide(const std::string &str)`** (line 30)
  - Only reads the string
  - Called frequently for Windows API conversions
  - **Fix**: Change to `Utf8ToWide(std::string_view str)`

#### Lower Priority (Less Frequently Called)
- **`ParseExtensions(const std::string &input)`** (line 265)
  - Only reads the string
  - Called once per search operation
  - **Fix**: Change to `ParseExtensions(std::string_view input)`
  - **Note**: Uses `std::stringstream` which requires `std::string`, but can convert internally

### 2. Search Pattern Utilities (SearchPatternUtils.h)

These functions only read their input strings and could accept `std::string_view`:

- **`DetectPatternType(const std::string& pattern)`** (line 21)
  - Only reads the string
  - Called once per search to determine pattern type
  - **Fix**: Change to `DetectPatternType(std::string_view pattern)`

- **`ExtractPattern(const std::string& input)`** (line 35)
  - Only reads the string
  - Called when creating matchers
  - **Fix**: Change to `ExtractPattern(std::string_view input)`
  - **Note**: Returns `std::string`, but can work with `string_view` internally

- **`MatchPattern(const std::string& pattern, const std::string& text, ...)`** (line 43)
  - Only reads both strings
  - Called during pattern matching
  - **Fix**: Change to `MatchPattern(std::string_view pattern, std::string_view text, ...)`

- **`CreateFilenameMatcher(const std::string& pattern, ...)`** (line 94)
  - Only reads the pattern string
  - Called once per search to create matcher
  - **Fix**: Change to `CreateFilenameMatcher(std::string_view pattern, ...)`
  - **Note**: Captures pattern in lambda, but can convert to string when needed

- **`CreatePathMatcher(const std::string& pattern, ...)`** (line 206)
  - Only reads the pattern string
  - Called once per search to create matcher
  - **Fix**: Change to `CreatePathMatcher(std::string_view pattern, ...)`

### 3. File Operations (FileOperations.cpp)

These functions only read their input strings and could accept `std::string_view`:

- **`ValidatePath(const std::string& path, ...)`** (line 51)
  - Only reads the string
  - Called before all file operations
  - **Fix**: Change to `ValidatePath(std::string_view path, ...)`

- **`OpenFileDefault(const std::string &full_path)`** (line 65)
  - Only reads the string
  - **Fix**: Change to `OpenFileDefault(std::string_view full_path)`
  - **Note**: Converts to `std::wstring` internally for Windows API

- **`OpenParentFolder(const std::string &full_path)`** (line 95)
  - Only reads the string
  - **Fix**: Change to `OpenFileDefault(std::string_view full_path)`

- **`CopyPathToClipboard(..., const std::string &full_path)`** (line 123)
  - Only reads the string
  - **Fix**: Change to `CopyPathToClipboard(..., std::string_view full_path)`

- **`DeleteFileToRecycleBin(const std::string &full_path)`** (line 176)
  - Only reads the string
  - **Fix**: Change to `DeleteFileToRecycleBin(std::string_view full_path)`

### 4. FileIndex Methods (FileIndex.h)

These methods only read their input strings and could accept `std::string_view`:

- **`BuildFullPath(uint64_t parentID, const std::string &name)`** (line 909)
  - Only reads the name string
  - Called during Insert/Rename/Move operations
  - **Fix**: Change to `BuildFullPath(uint64_t parentID, std::string_view name)`

### 5. StringPool (FileIndex.h)

- **`Intern(const std::string &str)`** (line 42)
  - Currently takes `const std::string&` but needs to store the string
  - **Status**: Copy is necessary (needs to store in hash set)
  - **Note**: Could potentially accept `std::string_view` and convert internally, but the conversion is necessary anyway

## Functions That Correctly Need Copies

These functions correctly take `const std::string&` because they need to store the string:

- `FileIndex::Insert(..., const std::string &name, ...)` - Stores name in FileEntry
- `FileIndex::Rename(..., const std::string &newName)` - Stores new name
- `FileIndex::InsertPathLocked(..., const std::string &path, ...)` - Stores path in buffer
- `FileIndex::AppendString(const std::string &str)` - Stores string in buffer
- `StringPool::Intern(const std::string &str)` - Stores string in hash set

## Impact Assessment

### High Impact (Frequently Called in Hot Paths)
1. **StringUtils.h functions** - Called millions of times during search operations
2. **SearchPatternUtils.h functions** - Called for every search operation

### Medium Impact (Called During File Operations)
1. **FileOperations.cpp functions** - Called on user actions (open file, delete, etc.)
2. **FileIndex::BuildFullPath** - Called during indexing operations

### Low Impact (Called Infrequently)
1. **ParseExtensions** - Called once per search

## Implementation Notes

### When to Use `std::string_view`
- ✅ Function only reads the string
- ✅ Function doesn't need to store the string
- ✅ Function doesn't need null-termination (or can handle it)
- ✅ Caller might pass `const char*`, `std::string`, or `std::string_view`

### When NOT to Use `std::string_view`
- ❌ Function needs to store the string (must copy)
- ❌ Function needs guaranteed null-termination for C APIs
- ❌ Function needs to modify the string
- ❌ Lifetime of the string_view might outlive the source string

### Conversion Requirements

Some functions will need to convert `std::string_view` to `std::string` internally:
- Windows API calls (require `std::wstring` or `const char*`)
- `std::stringstream` operations
- Functions that need null-terminated strings

This is acceptable as long as the conversion happens internally and callers can pass `string_view`.

## Recommended Priority Order

1. **StringUtils.h** - High frequency, easy wins
2. **SearchPatternUtils.h** - Called for every search
3. **FileOperations.cpp** - User-facing operations
4. **FileIndex::BuildFullPath** - Indexing operations

## Example Conversion

### Before:
```cpp
inline std::string ToLower(const std::string &str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}
```

### After:
```cpp
inline std::string ToLower(std::string_view str) {
  std::string result(str);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}
```

This allows callers to pass `const char*`, `std::string`, or `std::string_view` without creating intermediate `std::string` objects.
