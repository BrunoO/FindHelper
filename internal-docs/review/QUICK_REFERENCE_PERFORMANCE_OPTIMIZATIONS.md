# Performance Optimization Quick Reference
## One-Page Developer Guide - 2026-01-23

---

## 🎯 Three Optimizations to Implement Today (30 minutes)

### 1️⃣ FileIndexStorage: Use emplace() instead of find()+insert
**File:** `src/index/FileIndexStorage.cpp:17-54`  
**Change:** `index_.find() + index_[] = ...` → `index_.emplace(...)`  
**Impact:** 2-3% faster Insert/Rename  
**Effort:** 10 min  

**Before:**
```cpp
bool isNewEntry = (index_.find(id) == index_.end());
index_[id] = {parent_id, std::string(name), ext, ...};
```

**After:**
```cpp
auto [it, inserted] = index_.emplace(
    std::piecewise_construct,
    std::forward_as_tuple(id),
    std::forward_as_tuple(parent_id, std::string(name), ext, ...)
);
bool isNewEntry = inserted;
```

---

### 2️⃣ SearchContext: Use string_view instead of string
**Files:** `src/search/SearchContext.h` + `src/index/FileIndex.cpp:328-329`  
**Change:** `std::string` → `std::string_view` in SearchContext struct  
**Impact:** 1-2% faster search initialization  
**Effort:** 15 min  

**Before:**
```cpp
struct SearchContext {
  std::string filename_query;
  std::string path_query;
};
// In FileIndex.cpp:
context.filename_query = std::string(query);
context.path_query = std::string(path_query);
```

**After:**
```cpp
struct SearchContext {
  std::string_view filename_query;
  std::string_view path_query;
};
// In FileIndex.cpp:
context.filename_query = query;
context.path_query = path_query;
```

---

### 3️⃣ Vector Pre-allocation: Reserve space for extensions
**File:** `src/api/GeminiApiUtils.cpp` (find parsing function)  
**Change:** Add `extensions.reserve(estimated_count)` before loop  
**Impact:** 5-10% faster in extension parsing  
**Effort:** 5 min  

**Before:**
```cpp
std::vector<std::string> extensions;
for (auto token : tokens) {
  extensions.push_back(token);  // May reallocate
}
```

**After:**
```cpp
std::vector<std::string> extensions;
extensions.reserve(response.size() / 10 + 5);  // Estimate + safety margin
for (auto token : tokens) {
  extensions.push_back(token);  // Pre-allocated space
}
```

---

## 🧪 Testing After Changes

```bash
# Build and run tests
scripts/build_tests_macos.sh

# Verify:
✅ All tests pass
✅ No compiler warnings
✅ No linker errors
```

---

## 📊 Expected Results

| Optimization | Expected Improvement |
|--------------|----------------------|
| FileIndexStorage | 2-3% faster insert/rename |
| SearchContext | 1-2% faster search startup |
| Vector pre-allocation | 5-10% faster extension parsing |
| **Total** | **3-8% overall** |

---

## ⚠️ Things NOT to Optimize

❌ **Extension matching** - Profile first to see if SSO is working  
❌ **Search hot path** - Already well-optimized with SIMD/prefetching  
❌ **Memory layout** - SoA pattern already optimal  
❌ **Threading** - Load balancing strategy already tuned  

---

## 📋 Safety Checklist

Before committing changes:

- [ ] Code still compiles
- [ ] All tests pass
- [ ] No new compiler warnings
- [ ] Behavior unchanged (no functional modifications)
- [ ] Lifetime safety verified (especially for string_view change)
- [ ] Code reviewed by team

---

## 🔍 Quick Debugging

If something breaks after changes:

1. **Compilation error?**
   - Check template syntax in emplace() call
   - Verify string_view lifetimes

2. **Tests failing?**
   - Likely a behavioral change (shouldn't happen)
   - Revert and review change carefully

3. **Performance regression?**
   - Profile to measure actual impact
   - May be worth keeping change anyway if cleaner code

---

## 📚 Full Documentation

Need more details? See:
- **Detailed analysis:** `docs/review/2026-01-23-PERFORMANCE_OPTIMIZATIONS_REVIEW.md`
- **Implementation guide:** `docs/review/PERFORMANCE_OPTIMIZATION_IMPLEMENTATION_GUIDE.md`
- **Summary:** `docs/review/2026-01-23-PERFORMANCE_REVIEW_SUMMARY.md`

---

## ✅ Completion Checklist

**Day 1 (Today):**
- [ ] Read this guide
- [ ] Implement optimization #1 (FileIndexStorage)
- [ ] Implement optimization #2 (SearchContext)
- [ ] Implement optimization #3 (Vector pre-allocation)
- [ ] Run tests: `scripts/build_tests_macos.sh`
- [ ] Commit changes

**Day 2 (Tomorrow):**
- [ ] Profile to verify improvements
- [ ] Review impact metrics
- [ ] Consider remaining optimizations (if time permits)

---

## 🎓 Key Concepts

**emplace() benefit:** Single hash lookup instead of two (find + operator[])

**string_view benefit:** Zero allocation, non-owning reference to existing data

**reserve() benefit:** Single allocation upfront instead of multiple reallocations

---

**Created:** 2026-01-23  
**Status:** Ready to implement  
**Estimated Time:** 30 minutes total  
**Expected Improvement:** 3-8%

