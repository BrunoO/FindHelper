# Code Coverage Improvement Plan

## Current Coverage Status

**Overall Coverage:**
- Regions: 53.08%
- Functions: 49.08%
- Lines: 46.41%
- Branches: 49.33%

## Priority Files for Coverage Improvement

### 🔴 Critical Priority (0-10% coverage)

#### 1. **Settings.cpp** - 5.03% coverage
**Current Status:** Only 5.03% line coverage, 2.24% branch coverage
**Impact:** Critical - Settings management is core functionality

**What's Missing:**
- `LoadSettings()` - File I/O, JSON parsing, error handling
- `SaveSettings()` - File I/O, JSON serialization
- `GetSettingsFilePath()` - Platform-specific path resolution
- Error handling paths (corrupted files, missing files)
- Validation logic for numeric values

**Recommendations:**
1. **Create `tests/SettingsTests.cpp`** with:
   - Test `LoadSettings()` with valid JSON
   - Test `LoadSettings()` with corrupted JSON (error handling)
   - Test `LoadSettings()` with missing file (defaults)
   - Test `SaveSettings()` round-trip (save then load)
   - Test `test_settings` namespace (in-memory mode)
   - Test validation of thread pool size, window dimensions
   - Test platform-specific path resolution

2. **Use in-memory settings mode** for tests:
   ```cpp
   // Use test_settings namespace to avoid file I/O
   test_settings::SetInMemorySettings(test_settings);
   // Test code here
   test_settings::ClearInMemorySettings();
   ```

**Expected Impact:** +15-20% overall coverage improvement

---

#### 2. **StdRegexUtils.h** - 0.00% coverage
**Current Status:** Completely untested
**Impact:** High - Regex matching is used throughout the application

**What's Missing:**
- `RegexMatch()` - All code paths (literal, simple, complex patterns)
- `ThreadLocalRegexCache` - Cache behavior, thread safety
- `PatternAnalysis` - Pattern classification logic
- `IsLiteralPattern()` - Edge cases
- `IsSimplePattern()` - Edge cases
- `RequiresFullMatch()` - Anchor detection logic
- Error handling (invalid patterns, exceptions)

**Recommendations:**
1. **Create `tests/StdRegexUtilsTests.cpp`** with:
   - Test literal pattern matching (fast path)
   - Test simple pattern matching (SimpleRegex path)
   - Test complex pattern matching (std::regex path)
   - Test case-sensitive vs case-insensitive
   - Test regex cache (thread-local behavior)
   - Test invalid patterns (error handling)
   - Test pattern analysis functions
   - Test edge cases (empty patterns, escaped characters)

2. **Test all optimization paths:**
   - Literal patterns → string search
   - Simple patterns → SimpleRegex
   - Complex patterns → std::regex
   - Full match vs partial match

**Expected Impact:** +5-8% overall coverage improvement

---

### 🟡 High Priority (20-35% coverage)

#### 3. **CpuFeatures.cpp** - 26.92% coverage
**Current Status:** 26.92% region coverage, 14.42% line coverage
**Impact:** Medium - CPU feature detection affects performance

**What's Missing:**
- Windows-specific `CheckAVX2Support()` paths
- Linux/macOS `CheckAVX2Support()` paths
- `GetPhysicalCoreCount()` - Windows implementation
- `GetLogicalCoreCount()` - Windows implementation
- `IsHyperthreadingEnabled()` - All platforms
- Error handling paths
- Non-x86 architectures (ARM, etc.)

**Recommendations:**
1. **Enhance `tests/CpuFeaturesTests.cpp`** with:
   - Test AVX2 detection on different platforms
   - Test core count detection (mock CPUID if possible)
   - Test hyperthreading detection
   - Test caching behavior (multiple calls)
   - Test non-x86 architectures (stub behavior)

2. **Consider mocking CPUID** for deterministic tests:
   - Use test doubles for CPUID calls
   - Test different CPU feature combinations

**Expected Impact:** +3-5% overall coverage improvement

---

#### 4. **PathUtils.cpp** - 28.21% coverage
**Current Status:** 28.21% region coverage, 38.39% line coverage
**Impact:** Medium - Path utilities are used throughout

**What's Missing:**
- Windows-specific paths (`GetUserDesktopPath()`, etc.)
- Error handling (missing environment variables)
- Path normalization functions
- Cross-platform path separator handling

**Recommendations:**
1. **Enhance `tests/PathUtilsTests.cpp`** with:
   - Test all user directory functions (Desktop, Downloads, etc.)
   - Test Windows-specific paths (when on Windows)
   - Test macOS/Linux paths (when on non-Windows)
   - Test error handling (missing HOME, USERPROFILE)
   - Test path normalization
   - Test path separator conversion

2. **Use platform-specific test cases:**
   ```cpp
   #ifdef _WIN32
   TEST_CASE("Windows paths") { ... }
   #else
   TEST_CASE("Unix paths") { ... }
   #endif
   ```

**Expected Impact:** +3-5% overall coverage improvement

---

#### 5. **FileIndex.cpp** - 33.88% coverage
**Current Status:** 33.88% region coverage, 35.68% line coverage
**Impact:** High - Core data structure

**What's Missing:**
- `Insert()` - Edge cases, error handling
- `Remove()` - Edge cases, error handling
- `Search()` - Complex query combinations
- Thread safety paths (concurrent access)
- Memory management edge cases
- Large dataset handling

**Recommendations:**
1. **Enhance `tests/FileIndexSearchStrategyTests.cpp`** with:
   - Test `Insert()` with various file types
   - Test `Remove()` operations
   - Test concurrent insert/remove operations
   - Test search with complex filters
   - Test edge cases (empty index, single file, etc.)
   - Test memory limits (very large indices)

2. **Add dedicated `tests/FileIndexTests.cpp`** for:
   - Insert/remove operations
   - Thread safety
   - Memory management
   - Edge cases

**Expected Impact:** +5-8% overall coverage improvement

---

#### 6. **SearchPatternUtils.h** - 33.51% coverage
**Current Status:** 33.51% region coverage, 24.22% line coverage
**Impact:** Medium - Pattern matching utilities

**What's Missing:**
- Pattern compilation functions
- Pattern validation
- Extension matching
- Complex pattern combinations

**Recommendations:**
1. **Create `tests/SearchPatternUtilsTests.cpp`** with:
   - Test pattern compilation
   - Test pattern validation
   - Test extension matching
   - Test pattern combinations
   - Test error handling

**Expected Impact:** +2-3% overall coverage improvement

---

### 🟢 Medium Priority (35-70% coverage)

#### 7. **LoadBalancingStrategy.cpp** - 64.10% coverage
**Current Status:** 64.10% region coverage, 87.28% line coverage
**Impact:** Medium - Already well-tested, but could improve

**Recommendations:**
- Test edge cases (0 threads, 1 thread, very large datasets)
- Test cancellation paths
- Test error handling in worker threads

**Expected Impact:** +1-2% overall coverage improvement

---

## Implementation Plan

### Phase 1: Critical Files (Week 1)
1. ✅ Create `tests/SettingsTests.cpp`
2. ✅ Create `tests/StdRegexUtilsTests.cpp`
3. ✅ Run coverage and verify improvements

**Expected Result:** Coverage increases from 46% to 55-60%

### Phase 2: High Priority Files (Week 2)
1. ✅ Enhance `tests/CpuFeaturesTests.cpp`
2. ✅ Enhance `tests/PathUtilsTests.cpp`
3. ✅ Enhance `tests/FileIndexSearchStrategyTests.cpp`
4. ✅ Create `tests/SearchPatternUtilsTests.cpp`

**Expected Result:** Coverage increases from 55-60% to 65-70%

### Phase 3: Medium Priority (Week 3)
1. ✅ Enhance `tests/LoadBalancingStrategyTests.cpp` (if exists)
2. ✅ Add edge case tests for well-covered files
3. ✅ Test error handling paths

**Expected Result:** Coverage increases from 65-70% to 70-75%

---

## Quick Wins (Easy to Implement)

### 1. Settings Tests (2-3 hours)
- Use in-memory settings mode
- Test JSON serialization/deserialization
- Test validation logic

### 2. StdRegexUtils Tests (3-4 hours)
- Test all three code paths (literal, simple, complex)
- Test cache behavior
- Test error handling

### 3. PathUtils Enhancement (1-2 hours)
- Add tests for missing user directory functions
- Test error handling paths

**Total Quick Wins:** 6-9 hours for ~15-20% coverage improvement

---

## Testing Best Practices

### 1. Use Test Doubles
- Mock file I/O for Settings tests
- Mock CPUID for CpuFeatures tests
- Use in-memory settings mode

### 2. Test Error Paths
- Corrupted files
- Missing files
- Invalid input
- Edge cases (empty, null, very large)

### 3. Test Platform-Specific Code
- Use `#ifdef` for platform-specific tests
- Test both Windows and non-Windows paths when possible

### 4. Test Thread Safety
- Concurrent operations
- Race conditions
- Lock contention

---

## Coverage Goals

### Short-term (1 month)
- **Target:** 65-70% overall coverage
- **Focus:** Critical and high-priority files

### Medium-term (3 months)
- **Target:** 75-80% overall coverage
- **Focus:** All core functionality

### Long-term (6 months)
- **Target:** 80-85% overall coverage
- **Focus:** Edge cases, error handling, platform-specific code

---

## Monitoring

### Regular Coverage Checks
1. Run coverage after each major feature
2. Set minimum coverage thresholds (e.g., 70% for new code)
3. Track coverage trends over time

### Coverage Reports
- Review HTML reports weekly
- Identify new gaps as code evolves
- Prioritize based on risk and impact

---

## Notes

- **Header-only files** (`.h`) with 0% coverage may be template/header-only code that's only instantiated in specific contexts
- **Platform-specific code** may show low coverage if tests only run on one platform
- **Error handling paths** are often under-tested but critical for robustness
- **Thread safety** code paths are harder to test but important for correctness

---

## Next Steps

1. **Start with Settings tests** (highest impact, relatively easy)
2. **Add StdRegexUtils tests** (completely untested, high usage)
3. **Enhance existing tests** (PathUtils, CpuFeatures, FileIndex)
4. **Set up coverage monitoring** (CI integration, thresholds)

