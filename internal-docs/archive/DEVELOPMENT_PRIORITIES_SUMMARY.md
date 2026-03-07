# Development Priorities - Quick Reference

**Last Updated:** 2025-12-25  
**Full Details:** See `PRIORITIZED_DEVELOPMENT_PLAN.md`

---

## 🚨 CRITICAL (Do First - ~6 hours)

1. **Complete DRY Refactoring** (2-3h)
   - Replace duplicated `ProcessChunkRange` lambdas with `ProcessChunkRangeForSearch`
   - Use `RecordThreadTiming` and `CalculateChunkBytes` helpers
   - **File:** `FileIndex.cpp`

2. **Exception Handling in Search Lambdas** (1h)
   - Add try-catch to all three search strategy lambdas
   - **File:** `FileIndex.cpp`

3. **Thread Pool Error Handling** (1h)
   - Log warnings on shutdown, catch exceptions in worker threads
   - **File:** `SearchThreadPool.cpp`

4. **Settings Validation** (30m)
   - Validate `loadBalancingStrategy` value
   - **File:** `LoadBalancingStrategy.cpp` or `Settings.cpp`

5. **Bounds Checking** (1h)
   - Add defensive checks in `ProcessChunkRangeForSearch`
   - **File:** `FileIndex.h`

---

## 🎯 HIGH PRIORITY (Next Sprint - ~10-13 hours)

6. **macOS Application Logic Extraction** (5-6h)
   - Extract to `ApplicationLogic` namespace
   - Add `RenderMainWindow()` to `UIRenderer`
   - Create `TimeFilterUtils`
   - **Files:** New files + `main_gui.cpp`

7. **macOS Full Search Infrastructure** (3-4h)
   - Add search state to `main_mac.mm`
   - Replace stub UI with full UI
   - **File:** `main_mac.mm`, `CMakeLists.txt`

8. **macOS File Operations** (2-3h)
   - Implement macOS file operations (open, reveal, copy)
   - **File:** `FileOperations_mac.cpp` or conditional `FileOperations.cpp`

---

## 📋 MEDIUM PRIORITY (Future - ~26-38 hours)

9. **Search Strategy Testing** (4-6h)
10. **PathPatternMatcher Improvements** (8-12h)
11. **macOS Application Bootstrap Parity** (14-20h)

---

## Quick Start Command

**To make code production-ready:**
```
1. Complete DRY refactoring in FileIndex.cpp
2. Add exception handling to search lambdas
3. Add error handling to SearchThreadPool
4. Add settings validation
5. Add bounds checking to ProcessChunkRangeForSearch
```

---

## Key References

- **Full Plan:** `docs/PRIORITIZED_DEVELOPMENT_PLAN.md`
- **Production Checklist:** `docs/PRODUCTION_READINESS_CHECKLIST.md`
- **Phase 2 Steps:** `docs/NEXT_STEPS.md`
- **Feature Ideas:** `ideas for new features.md`












