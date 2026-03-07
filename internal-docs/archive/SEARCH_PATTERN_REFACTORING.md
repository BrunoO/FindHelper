# Search Pattern Handling Refactoring

## Problem: Code Duplication

There is significant duplication between `SearchWorker.cpp` and `FileIndex.cpp` in how they handle search patterns:

### Duplicated Logic

1. **Pattern Type Detection** (appears 4+ times):
   - Detection of `rs:` prefix (std::regex)
   - Detection of `re:` prefix (simple regex)
   - Detection of glob patterns (`*` or `?`)
   - Detection of substring patterns (default)

2. **Matcher Creation** (appears 3+ times in FileIndex):
   - Creating filename matchers with case sensitivity
   - Creating path matchers with case sensitivity
   - Same branching logic: std::regex → simple regex → glob → substring

3. **Case Sensitivity Handling**:
   - Same branching logic repeated in multiple places
   - Calls to `RegExMatch` vs `RegExMatchI`
   - Calls to `GlobMatch` vs `GlobMatchI`

### Current Locations

- `FileIndex.cpp::Search()` - lines 490-493, 617-656, 659-697
- `FileIndex.cpp::SearchAsync()` - lines 841-844, 957-1019
- `FileIndex.cpp::SearchSync()` - lines 1182-1184, 1244-1304
- `SearchWorker.cpp::Matches` lambda - lines 168-199

## Solution: Extract to Utility Module

Create `SearchPatternUtils.h` that provides:

1. **Pattern Type Detection**:
   ```cpp
   enum class PatternType {
     StdRegex,    // rs: prefix
     SimpleRegex, // re: prefix
     Glob,        // contains * or ?
     Substring    // default
   };
   
   PatternType DetectPatternType(const std::string& pattern);
   ```

2. **Matcher Functions**:
   ```cpp
   // For FileIndex (returns function objects)
   std::function<bool(const char*)> CreateFilenameMatcher(
     const std::string& pattern, bool case_sensitive);
   
   std::function<bool(std::string_view)> CreatePathMatcher(
     const std::string& pattern, bool case_sensitive);
   
   // For SearchWorker (direct matching)
   bool MatchPattern(const std::string& pattern, const std::string& text, bool case_sensitive);
   ```

3. **Pattern Parsing**:
   ```cpp
   struct PatternInfo {
     PatternType type;
     std::string pattern;  // Without prefix
     std::string lower;   // Lowercased if case-insensitive
   };
   
   PatternInfo ParsePattern(const std::string& input, bool case_sensitive);
   ```

## Benefits

- ✅ Single source of truth for pattern detection
- ✅ Consistent behavior across all search methods
- ✅ Easier to maintain and extend
- ✅ Reduced code duplication (~200+ lines)
- ✅ Easier to test pattern matching logic

## Implementation Steps

1. Create `SearchPatternUtils.h` with pattern detection and matcher creation
2. Update `FileIndex.cpp` to use the utility functions
3. Update `SearchWorker.cpp` to use the utility functions
4. Test to ensure behavior is unchanged
5. Remove duplicated code
