# Quick Performance Optimization Checklist
## Simple Wins Identified - 2026-01-23

## 🎯 Quick Action Items

### ✅ Verify First (No Changes)
- [ ] Profile **extension matching** with profiler to see if SSO is handling allocations
  - If allocations are visible: implement small-buffer optimization
  - If allocations are negligible: keep as-is (SSO is working well)

### 🔧 Implement Immediately (30 minutes total)

#### 1. FileIndexStorage - Use `emplace()` instead of separate find+insert
- **File:** `src/index/FileIndexStorage.cpp:17-54`
- **Change:** Replace `index_.find()` + `index_[]` with `index_.emplace()`
- **Lines to modify:** 17, 54, 95
- **Impact:** 2-3% faster Insert/Rename operations

#### 2. SearchContext - Use `std::string_view` instead of `std::string`
- **Files:** `src/search/SearchContext.h`, `src/index/FileIndex.cpp:328-329`
- **Change:** Store `filename_query` and `path_query` as `string_view` in SearchContext
- **Lines to modify:** Header struct + FileIndex.cpp lines 328-329
- **Impact:** Zero allocations during search initialization, 1-2% faster search startup

#### 3. GeminiApiUtils - Add vector pre-allocation
- **File:** `src/api/GeminiApiUtils.cpp` (find ParseExtensionsFromGeminiResponse)
- **Change:** Add `extensions.reserve(estimated_count)` before parsing loop
- **Impact:** 5-10% faster extension parsing (infrequent operation)

### 💭 Consider After Profiling

#### 4. Extension Matching - Small buffer optimization
- **File:** `src/search/SearchPatternUtils.h:395-420`
- **Action:** Profile first to see if this is actually allocating (SSO might be handling it)
- **If profiling shows allocations:** Implement small-buffer optimization
- **Impact:** 1-3% if not using SSO properly

#### 5. String Concatenation - Use append() instead of operator+
- **Files:** `src/api/GeminiApiHttp_linux.cpp:91`, `src/platform/windows/AppBootstrap_win.cpp:62`
- **Change:** Use `.append()` or fmt library instead of string operator+
- **Impact:** <1% (infrequent operations)

### 📋 Nice to Have (Low Priority)

#### 6. Exception Strings - Use stream operators
- **File:** `src/core/Settings.cpp:422-431`
- **Change:** Replace `"message" + std::string(e.what())` with stream operator
- **Impact:** Negligible (error path only), but cleaner code

---

## 📊 Expected Results

| Optimization | Effort | Impact | Priority |
|--------------|--------|--------|----------|
| FileIndexStorage emplace() | 10 min | 2-3% | ⭐⭐⭐ |
| SearchContext string_view | 15 min | 1-2% | ⭐⭐⭐ |
| Vector pre-allocation | 5 min | 5-10% (parsing only) | ⭐⭐ |
| Extension matching | 30 min | 1-3% (if needed) | ⭐⭐ |
| String concatenation | 10 min | <1% | ⭐ |
| Exception strings | 5 min | <1% | ⭐ |

**Total estimated effort:** 30-60 minutes  
**Total estimated improvement:** 3-8% performance gain

---

## ✅ Verification Checklist

After implementing changes:

- [ ] Run test suite: `scripts/build_tests_macos.sh`
- [ ] All tests pass
- [ ] No new compiler warnings
- [ ] Profile shows reduced allocation count
- [ ] Search performance is same or better
- [ ] Memory usage is same or better
- [ ] Code compiles on all platforms (Windows/Mac/Linux)

---

## 📝 Reference Documents

- Full analysis: `docs/review/2026-01-23-PERFORMANCE_OPTIMIZATIONS_REVIEW.md`
- Previous optimization work: `docs/analysis/HOTPATH_SIMPLE_OPTIMIZATIONS.md`
- Hot path analysis: `SEARCH_PERFORMANCE_OPTIMIZATIONS.md`

---

## 🚀 Next Steps

1. **Today:** Review this checklist with team
2. **Tomorrow:** Implement Phase 1 changes (FileIndexStorage, SearchContext, Vector allocation)
3. **After that:** Profile extension matching to determine if additional optimization is needed
4. **Final:** Validate performance improvements

