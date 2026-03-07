# Case-Insensitive Search Analysis: Avoiding Lowercase Path Storage

## Current Implementation

### Storage Strategy
- **ContiguousStringBuffer** stores paths in **lowercase** (line 80 in ContiguousStringBuffer.cpp)
- **Rationale**: "Store lowercase path for faster case-insensitive search"
- **Memory cost**: No extra memory (just different casing of same data)

### Search Operations

#### 1. Substring Search (strstr)
```cpp
// Line 425, 692 in ContiguousStringBuffer.cpp
filenameMatch = (strstr(filename, queryLower.c_str()) != nullptr);
```
- **Current**: Uses `strstr()` (case-sensitive)
- **Works because**: Both `filename` (from storage) and `queryLower` are lowercase
- **Performance**: Very fast (optimized C library function)

#### 2. Path Substring Search (ContainsSubstring)
```cpp
// Line 445, 708 in ContiguousStringBuffer.cpp
pathMatch = ContainsSubstring(dirPath, pathQueryLower);
```
- **Current**: Uses `ContainsSubstring()` (case-sensitive)
- **Works because**: Both `dirPath` (from storage) and `pathQueryLower` are lowercase
- **Performance**: Optimized with prefix check and reverse comparison

#### 3. Regex Matching (SimpleRegex::RegExMatch)
```cpp
// Line 420, 688 in ContiguousStringBuffer.cpp
filenameMatch = SimpleRegex::RegExMatch(queryStr.substr(3), filename);
```
- **Current**: Case-sensitive character comparison (line 45 in SimpleRegex.h: `re[0] == text[0]`)
- **Works because**: Both pattern and text are lowercase
- **Performance**: Fast (simple recursive matcher)

#### 4. Glob Matching (SimpleRegex::GlobMatch)
```cpp
// Line 422, 690 in ContiguousStringBuffer.cpp
filenameMatch = SimpleRegex::GlobMatch(queryStr, filename);
```
- **Current**: Case-sensitive character comparison (line 102 in SimpleRegex.h: `pattern[0] == text[0]`)
- **Works because**: Both pattern and text are lowercase
- **Performance**: Fast (simple recursive matcher)

## Problem: Cannot Support Case-Sensitive Search

**Current limitation**: Because paths are stored in lowercase, we **cannot** support case-sensitive search even if we wanted to.

**User requirement**: Enable choice between case-sensitive and case-insensitive search in the future.

## Solution Options

### Option 1: Store Original Paths + Case-Insensitive Search Functions ⭐ (Recommended)

**Approach**: 
- Store original-cased paths in ContiguousStringBuffer
- Implement case-insensitive versions of search functions
- Lowercase query only when case-insensitive search is enabled

**Implementation**:

#### 1.1 Update ContiguousStringBuffer::Insert()
```cpp
// REMOVE lowercase conversion
void ContiguousStringBuffer::Insert(uint64_t id, const std::string &path, bool isDirectory) {
  // ... existing code ...
  
  // REMOVE: std::string lowerPath = ToLower(path);
  // USE: path directly
  
  size_t filenameStartOffset = 0;
  size_t lastSlash = path.find_last_of("\\/");  // Use original path
  // ... rest of parsing uses original path ...
  
  size_t offset = AppendString(path);  // Store original path
  // ... rest unchanged ...
}
```

#### 1.2 Create Case-Insensitive String Functions

**Add to StringUtils.h**:
```cpp
// Case-insensitive character comparison
inline char ToLowerChar(unsigned char c) {
  return static_cast<char>(std::tolower(c));
}

// Case-insensitive version of strstr
inline const char* StrStrI(const char* haystack, const char* needle) {
  if (!needle || !*needle) return haystack;
  
  const char* h = haystack;
  while (*h) {
    const char* n = needle;
    const char* h2 = h;
    while (*n && *h2 && ToLowerChar(*n) == ToLowerChar(*h2)) {
      n++;
      h2++;
    }
    if (!*n) return h;  // Found match
    h++;
  }
  return nullptr;
}

// Case-insensitive substring search
inline bool ContainsSubstringI(const std::string_view &text,
                               const std::string_view &pattern) {
  if (pattern.empty()) return true;
  if (text.empty() || pattern.length() > text.length()) return false;

  // Fast path: Check if pattern is a prefix
  if (text.length() >= pattern.length()) {
    bool isPrefix = true;
    for (size_t i = 0; i < pattern.length(); ++i) {
      if (ToLowerChar(text[i]) != ToLowerChar(pattern[i])) {
        isPrefix = false;
        break;
      }
    }
    if (isPrefix) return true;
  }

  // For longer patterns, use reverse comparison
  if (pattern.length() > 5) {
    size_t patternLen = pattern.length();
    size_t textLen = text.length();
    size_t maxStart = textLen - patternLen;

    for (size_t i = 0; i <= maxStart; ++i) {
      bool match = true;
      for (size_t j = patternLen; j > 0; --j) {
        if (ToLowerChar(text[i + j - 1]) != ToLowerChar(pattern[j - 1])) {
          match = false;
          break;
        }
      }
      if (match) return true;
    }
    return false;
  } else {
    // For short patterns, use case-insensitive find
    for (size_t i = 0; i <= text.length() - pattern.length(); ++i) {
      bool match = true;
      for (size_t j = 0; j < pattern.length(); ++j) {
        if (ToLowerChar(text[i + j]) != ToLowerChar(pattern[j])) {
          match = false;
          break;
        }
      }
      if (match) return true;
    }
    return false;
  }
}
```

#### 1.3 Update SimpleRegex for Case-Insensitive Matching

**Add case-insensitive parameter to SimpleRegex functions**:
```cpp
namespace SimpleRegex {

// Case-insensitive character comparison helper
inline bool CharEqualI(char a, char b) {
  return std::tolower(static_cast<unsigned char>(a)) == 
         std::tolower(static_cast<unsigned char>(b));
}

inline bool RegExMatchI(std::string_view pattern, std::string_view text) {
  // Same logic as RegExMatch, but use CharEqualI instead of ==
  // ... implementation ...
}

inline bool GlobMatchI(std::string_view pattern, std::string_view text) {
  // Same logic as GlobMatch, but use CharEqualI instead of ==
  // ... implementation ...
}

// Keep original case-sensitive versions for future use
inline bool RegExMatch(std::string_view pattern, std::string_view text) {
  // ... existing implementation ...
}

inline bool GlobMatch(std::string_view pattern, std::string_view text) {
  // ... existing implementation ...
}

} // namespace SimpleRegex
```

#### 1.4 Update ContiguousStringBuffer::Search() to Support Case Sensitivity

**Add caseSensitive parameter**:
```cpp
std::vector<uint64_t> Search(
    const std::string &query, 
    int threadCount = -1,
    SearchStats *stats = nullptr,
    const std::string &pathQuery = "",
    const std::vector<std::string> *extensions = nullptr,
    bool foldersOnly = false,
    bool caseSensitive = false) const;  // NEW PARAMETER
```

**Update search logic**:
```cpp
// In search loop:
if (!queryStr.empty()) {
  if (useRegex) {
    if (caseSensitive) {
      filenameMatch = SimpleRegex::RegExMatch(queryStr.substr(3), filename);
    } else {
      filenameMatch = SimpleRegex::RegExMatchI(queryStr.substr(3), filename);
    }
  } else if (useGlob) {
    if (caseSensitive) {
      filenameMatch = SimpleRegex::GlobMatch(queryStr, filename);
    } else {
      filenameMatch = SimpleRegex::GlobMatchI(queryStr, filename);
    }
  } else {
    // Simple substring search
    if (caseSensitive) {
      filenameMatch = (strstr(filename, queryStr.c_str()) != nullptr);
    } else {
      // Only lowercase query if case-insensitive
      std::string queryLower = ToLower(queryStr);
      filenameMatch = (StrStrI(filename, queryLower.c_str()) != nullptr);
    }
  }
}
```

**Benefits**:
- ✅ Enables case-sensitive search option
- ✅ No duplicate path storage (saves memory)
- ✅ Original paths preserved (better for display)
- ✅ Backward compatible (default to case-insensitive)

**Costs**:
- ⚠️ Case-insensitive search slightly slower (tolower() per character)
- ⚠️ More code complexity (two versions of each function)

**Performance Impact**:
- **Case-insensitive**: ~10-20% slower than current (tolower() overhead)
- **Case-sensitive**: Same speed as current (direct comparison)
- **Memory**: Saves ~0 bytes (same path length, just different casing)

### Option 2: Store Both Original and Lowercase Paths (Not Recommended)

**Approach**: Store both original and lowercase paths

**Implementation**:
```cpp
std::vector<char> storage_;           // Original paths
std::vector<char> storage_lower_;    // Lowercase paths
```

**Benefits**:
- ✅ Fast case-insensitive search (use lowercase storage)
- ✅ Fast case-sensitive search (use original storage)
- ✅ Original paths available for display

**Costs**:
- ❌ **Double memory usage** (~100-200 bytes per entry)
- ❌ **Double storage operations** (Insert, Rebuild, etc.)
- ❌ **Synchronization complexity** (keep both in sync)

**Verdict**: **Not recommended** - Memory cost too high

### Option 3: Use Windows API Case-Insensitive Functions

**Approach**: Use Windows `CompareStringEx()` or `lstrcmpi()` for case-insensitive comparison

**Implementation**:
```cpp
#include <windows.h>
#include <stringapiset.h>

// Use CompareStringEx for case-insensitive comparison
inline bool StrStrI_WinAPI(const char* haystack, const char* needle) {
  // Convert to wide string, use CompareStringEx
  // ... implementation ...
}
```

**Benefits**:
- ✅ Uses optimized Windows API
- ✅ Handles Unicode correctly
- ✅ Locale-aware

**Costs**:
- ⚠️ UTF-8 to UTF-16 conversion overhead
- ⚠️ More complex implementation
- ⚠️ Windows-specific (not portable)

**Verdict**: **Could be considered** for Windows-specific optimization

### Option 4: Hybrid Approach - Lazy Lowercasing

**Approach**: Store original paths, lowercase on-demand during search

**Implementation**:
- Store original paths
- During search, lowercase query and compare character-by-character
- Cache lowercase version if needed (but defeats purpose)

**Verdict**: **Not viable** - Would be slower than Option 1

## Recommended Implementation: Option 1

### Step-by-Step Implementation

1. **Remove lowercase conversion in Insert()**
   - Store original paths directly
   - Update parsing to use original path

2. **Add case-insensitive string functions**
   - `StrStrI()` - case-insensitive strstr
   - `ContainsSubstringI()` - case-insensitive substring search

3. **Add case-insensitive regex/glob functions**
   - `RegExMatchI()` - case-insensitive regex
   - `GlobMatchI()` - case-insensitive glob

4. **Update Search() and SearchAsync()**
   - Add `bool caseSensitive = false` parameter
   - Use appropriate function based on caseSensitive flag
   - Only lowercase query when case-insensitive

5. **Update SearchWorker**
   - Add `caseSensitive` to SearchParams
   - Pass to ContiguousStringBuffer::Search()

6. **Update UI**
   - Add case-sensitive checkbox
   - Pass setting to SearchParams

### Performance Comparison

| Operation | Current (Lowercase Storage) | Option 1 (Case-Insensitive) | Option 1 (Case-Sensitive) |
|-----------|------------------------------|----------------------------|---------------------------|
| Substring search | Very Fast (strstr) | Fast (StrStrI) | Very Fast (strstr) |
| Path search | Fast (ContainsSubstring) | Fast (ContainsSubstringI) | Fast (ContainsSubstring) |
| Regex | Fast | Fast (RegExMatchI) | Fast (RegExMatch) |
| Glob | Fast | Fast (GlobMatchI) | Fast (GlobMatch) |
| Memory per entry | ~100-200 bytes | ~100-200 bytes | ~100-200 bytes |
| Insert overhead | ToLower() call | None | None |

**Performance Impact**:
- **Case-insensitive search**: ~10-20% slower (tolower() per character)
- **Case-sensitive search**: Same speed (no tolower() needed)
- **Memory**: No change (same path length)

### Code Changes Summary

**Files to modify**:
1. `ContiguousStringBuffer.cpp` - Remove ToLower() in Insert(), update Search()
2. `ContiguousStringBuffer.h` - Add caseSensitive parameter
3. `StringUtils.h` - Add case-insensitive string functions
4. `SimpleRegex.h` - Add case-insensitive regex/glob functions
5. `SearchWorker.cpp` - Pass caseSensitive to Search()
6. `SearchWorker.h` - Add caseSensitive to SearchParams
7. UI files - Add case-sensitive checkbox

**Estimated LOC changes**: ~200-300 lines

## Conclusion

**Recommendation**: **Implement Option 1** (Store Original Paths + Case-Insensitive Functions)

**Rationale**:
- ✅ Enables case-sensitive search option (user requirement)
- ✅ No memory overhead (same path storage)
- ✅ Backward compatible (default case-insensitive)
- ✅ Acceptable performance impact (~10-20% slower for case-insensitive)
- ✅ Clean architecture (separate functions for case sensitivity)

**Performance trade-off is acceptable**:
- Case-insensitive search: ~10-20% slower (but still very fast)
- Case-sensitive search: Same speed as current
- Memory: No change

**Future benefits**:
- User can choose case-sensitive vs case-insensitive search
- Original paths preserved (better for display/extraction)
- More flexible search options

























