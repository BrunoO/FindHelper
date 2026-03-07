# Prioritized Development Plan

**Generated:** 2025-12-25  
**Based on:** Review of existing documentation, production readiness checklists, and feature roadmap

---

## 🚨 CRITICAL PRIORITY (Must Fix Before Production)

### 1. Complete DRY Refactoring in LoadBalancingStrategy ⚠️ **HIGHEST PRIORITY**

**Status:** ⚠️ **INCOMPLETE** - Helper methods created but not integrated  
**Impact:** ~400 lines of duplicated code, maintenance nightmare  
**Estimated Time:** 2-3 hours

**Task:**
- Replace all `ProcessChunkRange` lambdas in Static, Hybrid, and Dynamic strategies with calls to `ProcessChunkRangeForSearch`
- Replace timing calculation code with `RecordThreadTiming` calls
- Replace bytes calculation with `CalculateChunkBytes` calls
- **Location:** `FileIndex.cpp` - `SearchAsyncWithData()` method

**Why Critical:**
- Bug fixes must be applied 3x (high risk of inconsistency)
- Code maintenance nightmare
- Increases testing burden

**Reference:** `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` #1

---

### 2. Exception Handling in Search Lambdas

**Status:** ❌ **MISSING** - No try-catch blocks in search lambdas  
**Impact:** Unhandled exceptions can crash the application  
**Estimated Time:** 1 hour

**Task:**
- Add try-catch blocks to all three search strategy lambdas (Static, Hybrid, Dynamic) in `SearchAsyncWithData`
- Wrap `ProcessChunkRangeForSearch` calls and timing code in try-catch blocks
- Log exceptions using `LOG_ERROR_BUILD`
- Return empty results vector on exception
- **Location:** `FileIndex.cpp` - `SearchAsyncWithData()` method

**Why Critical:**
- Unhandled exceptions in futures can crash the app
- Search failures should be graceful, not fatal

**Reference:** `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` #2

---

### 3. Thread Pool Error Handling

**Status:** ⚠️ **INCOMPLETE** - Returns invalid future on shutdown, but no logging  
**Impact:** Silent failures are hard to debug, worker thread crashes can deadlock  
**Estimated Time:** 1 hour

**Task:**
- Improve error handling in `SearchThreadPool::Enqueue()`
- When `shutdown_` is true, log a warning using `LOG_WARNING_BUILD` before returning invalid future
- Add exception handling in worker thread loop to catch and log exceptions thrown by tasks
- **Location:** `SearchThreadPool.cpp`

**Why Critical:**
- Silent failures are hard to debug
- Worker thread crashes can deadlock the pool

**Reference:** `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` #3

---

## 🎯 HIGH PRIORITY (Feature Parity & Production Readiness)

### 4. macOS Application Class Parity (Phase 2 - Step 1.6)

**Status:** 🔄 **IN PROGRESS** - Next step in Phase 2  
**Impact:** Enables full cross-platform development workflow  
**Estimated Time:** 5-6 hours

**Tasks:**
1. **Extract Main Window Rendering and Application Logic**
   - Create `ApplicationLogic` namespace (`ApplicationLogic.h`, `ApplicationLogic.cpp`)
   - Extract keyboard shortcuts from `main_gui.cpp`
   - Extract search controller updates and maintenance tasks
   - Add `RenderMainWindow()` to `UIRenderer` class
   - Move main window setup to `RenderMainWindow()`
   - Create `TimeFilterUtils` namespace for cross-platform helpers

2. **Update Platform Files**
   - Simplify `main_gui.cpp` to use new abstractions
   - Update `CMakeLists.txt` to include new source files

**Why High Priority:**
- Enables macOS development workflow (top priority from `ideas for new features.md`)
- Reduces code duplication between Windows and macOS
- Foundation for Step 2 (full search infrastructure on macOS)

**Reference:** `docs/PHASE_2_IMPLEMENTATION_ORDER.md` Step 1.6, `docs/NEXT_STEPS.md`

---

### 5. macOS Full Search Infrastructure (Phase 2 - Step 2)

**Status:** 📋 **PLANNED** - After Step 1.6  
**Impact:** Complete feature parity with Windows  
**Estimated Time:** 3-4 hours

**Tasks:**
1. Add search state management to `main_mac.mm`:
   - `GuiState`, `SearchWorker`, `SearchController`, `AppSettings`
   - Load settings
   - Window size persistence

2. Replace stub UI with full UI:
   - Use `ApplicationLogic::Update()`
   - Use `UIRenderer::RenderMainWindow()`

3. Update `CMakeLists.txt`:
   - Add `ApplicationLogic.cpp`, `TimeFilterUtils.cpp`, `UIRenderer.cpp` to macOS build

**Why High Priority:**
- Achieves complete feature parity with Windows
- Enables full testing on macOS
- Top priority from `ideas for new features.md`

**Reference:** `docs/NEXT_STEPS.md` Step 2, `docs/PHASE_2_IMPLEMENTATION_ORDER.md` Step 2

---

### 6. Settings Validation

**Status:** ❌ **MISSING** - No validation of `loadBalancingStrategy` value  
**Impact:** Invalid settings can cause undefined behavior  
**Estimated Time:** 30 minutes

**Task:**
- Add validation in `GetLoadBalancingStrategy()` to check if settings value is one of "static", "hybrid", "dynamic", or "interleaved"
- If invalid, log warning using `LOG_WARNING_BUILD` and return default (Hybrid)
- **Location:** `LoadBalancingStrategy.cpp` or `Settings.cpp`

**Why Important:**
- Invalid settings can cause undefined behavior
- User typos in `settings.json` shouldn't crash the app

**Reference:** `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` #5

---

### 7. Bounds Checking in ProcessChunkRangeForSearch

**Status:** ⚠️ **PARTIAL** - Some bounds checks exist, but could be more defensive  
**Impact:** Prevents crashes from edge cases  
**Estimated Time:** 1 hour

**Task:**
- Add defensive bounds checking in `ProcessChunkRangeForSearch` template
- Verify `chunk_start < chunk_end`
- Verify `chunk_end <= path_ids_.size()`
- Validate all array accesses are within bounds
- Use assertions or early returns with logging for invalid ranges
- **Location:** `FileIndex.h` (template function)

**Why Important:**
- Prevents crashes from edge cases
- Helps catch bugs early

**Reference:** `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` #6

---

## 📋 MEDIUM PRIORITY (Quality & Testing)

### 8. macOS File Operations (Phase 2 - Step 4)

**Status:** 📋 **PLANNED** - After Step 2  
**Impact:** Complete macOS feature parity  
**Estimated Time:** 2-3 hours

**Tasks:**
- Create `FileOperations_mac.cpp` or make `FileOperations.cpp` conditional
- Implement `OpenFileDefault()` for macOS (open in default application)
- Implement `OpenParentFolder()` for macOS (reveal in Finder)
- Implement `CopyPathToClipboard()` for macOS
- Test macOS file operations

**Why Medium Priority:**
- Completes macOS feature parity
- Not critical for core functionality

**Reference:** `docs/NEXT_STEPS.md` Step 4, `docs/PHASE_2_IMPLEMENTATION_ORDER.md` Step 4

---

### 9. macOS Application Bootstrap Parity

**Status:** 📋 **PLANNED** - Long-term goal  
**Impact:** Consistent architecture across platforms  
**Estimated Time:** 14-20 hours

**Tasks:**
1. Create macOS Bootstrap Module (`AppBootstrap_mac.h`, `AppBootstrap_mac.mm`)
2. Extend Application Class for macOS Metal Rendering
3. Add Font Configuration Support (`FontUtils_mac.mm`)
4. Update `main_mac.mm` to use Application class

**Why Medium Priority:**
- Large effort, can be done incrementally
- Current architecture works, this is a refinement
- Not blocking for core functionality

**Reference:** `docs/MACOS_APPLICATION_CLASS_PARITY.md`

---

### 10. Search Strategy Testing & Robustness

**Status:** ⚠️ **PARTIAL** - Basic tests exist, but need refinement  
**Impact:** Validates correctness and performance  
**Estimated Time:** 4-6 hours

**Tasks:**
- Add comprehensive tests for Hybrid and Dynamic strategies
- Add strategy comparison tests (verify all strategies return identical results)
- Add edge case tests (empty index, single item, more threads than items)
- Add performance benchmarks under realistic workloads
- Fix any outstanding issues with search strategies

**Why Medium Priority:**
- Important for reliability
- Not blocking for production if basic functionality works

**Reference:** `ideas for new features.md` (Medium priority), `docs/SEARCH_STRATEGY_TESTING_PLAN.md`

---

### 11. PathPatternMatcher Improvements

**Status:** 📋 **PLANNED** - Ongoing work  
**Impact:** Better pattern matching performance and correctness  
**Estimated Time:** 8-12 hours

**Tasks:**
- Continue new `PathPatternMatcher` implementation
- Benchmark against current implementation
- Keep the faster/cleaner design
- Address remaining improvements from `docs/PATHPATTERNMATCHER_REMAINING_IMPROVEMENTS.md`

**Why Medium Priority:**
- Performance improvement
- Not blocking for core functionality

**Reference:** `ideas for new features.md` (Medium priority)

---

## 🔧 LOW PRIORITY (Nice to Have)

### 12. PGO Compatibility for Test Targets

**Status:** ⚠️ **PARTIAL** - Some test targets may not have matching PGO flags  
**Impact:** Build failures when `ENABLE_PGO=ON`  
**Estimated Time:** 1-2 hours

**Task:**
- Verify all test targets that share source files with main executable have matching PGO flags
- Copy PGO logic from main target to test targets
- **Location:** `CMakeLists.txt`

**Why Low Priority:**
- Only affects builds with `ENABLE_PGO=ON`
- Pattern already exists in `file_index_search_strategy_tests`

**Reference:** `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` #4

---

### 13. Thread Pool Shutdown Graceful Handling

**Status:** ⚠️ **PARTIAL** - Tasks enqueued during shutdown return invalid futures  
**Impact:** Better user experience during shutdown  
**Estimated Time:** 1 hour

**Task:**
- Add check in `SearchAsyncWithData` to verify thread pool is not shutting down before enqueueing tasks
- If shutdown detected, log warning and return empty futures vector
- Alternatively, make thread pool shutdown wait for all pending tasks

**Why Low Priority:**
- Edge case during shutdown
- Current behavior is acceptable

**Reference:** `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` #7

---

### 14. Unit Tests for Helper Methods

**Status:** 📋 **PLANNED**  
**Impact:** Catches regressions, documents behavior  
**Estimated Time:** 3-4 hours

**Tasks:**
- Create unit tests for `ProcessChunkRangeForSearch`, `CalculateChunkBytes`, `RecordThreadTiming`
- Test edge cases (empty ranges, boundary conditions, invalid inputs)
- Add integration tests for each load balancing strategy

**Why Low Priority:**
- Important for long-term maintenance
- Not blocking for production

**Reference:** `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` #8

---

### 15. Performance Metrics & Configurable Thread Pool Size

**Status:** 📋 **PLANNED**  
**Impact:** Performance tuning and analysis  
**Estimated Time:** 3-4 hours

**Tasks:**
- Add optional performance metrics to `SearchThreadPool` (average task execution time, queue depth, thread utilization)
- Add `GetMetrics()` method
- Make thread pool size configurable via settings
- Add `searchThreadPoolSize` field to `AppSettings`

**Why Low Priority:**
- Performance optimization
- Not critical for functionality

**Reference:** `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` #8, #9

---

### 16. AI-Assisted Interface Experiments

**Status:** 📋 **EXPERIMENTAL**  
**Impact:** Future feature exploration  
**Estimated Time:** Unknown (experimental)

**Tasks:**
- Explore AI-based assistant interface using local LLMs
- Explore copilot plugin integration
- Explore direct Gemini API integration

**Why Low Priority:**
- Experimental feature
- Not part of core functionality

**Reference:** `ideas for new features.md` (Medium priority, but experimental)

---

### 17. Logger Streaming Interface

**Status:** 📋 **PLANNED**  
**Impact:** Modern logging interface  
**Estimated Time:** 4-6 hours

**Task:**
- Improve logger to support modern streaming-style interface
- Reference: `https://www.cppstories.com/2021/stream-logger/`

**Why Low Priority:**
- Quality of life improvement
- Current logging works fine

**Reference:** `ideas for new features.md` (Low priority)

---

### 18. Streaming Search Results

**Status:** 📋 **PLANNED** - Proceed with caution & optimization  
**Impact:** 10–100x improvement in perceived performance (Time to First Result); eliminates "dead" UI during large searches  
**Estimated Time:** Phase 1 (Core) 1–2 weeks; Phase 2 (UI) ~1 week; Phase 3 (Progressive sort/filter) ~1 week

**Assessment (2026-01-31):** Recommendation **PROMISING – DO NOT DROP.** Architecture is component-based and uses lazy loading; streaming is a natural extension. Risks (threading, partial vs final coordination) have defined mitigations.

**Implementation strategy (from assessment):**
1. **GetAllPendingBatches API:** `StreamingResultsCollector` buffers batches; UI calls `GetAllPendingBatches()` once per frame via `SearchController::PollResults` (no per-batch notification storm).
2. **Single-threaded future consumption:** Use existing SearchWorker thread with back-off loop: `wait_for(0)` on all futures; if any ready, `.get()` and push to collector; if none ready, `wait_for(5ms)` on first pending. Drain remaining futures on cancel/error.
3. **Progressive filtering:** Phase 1 & 2: display partial results with minimal or no sorting. Phase 3: sort only on user interaction or search complete (avoid sorting 1M results while streaming).
4. **Error/cancellation:** On cancel: stop loop, drain futures, keep partial results. On worker throw: catch, mark "Failed," keep partial results.

**Coordination with Large Result Set plan:** Streaming and Result Limiting are complementary. When `max_results` is added, `StreamingResultsCollector` should be aware of it; once limit is hit, signal search to stop and mark complete.

**References:**
- `docs/research/STREAMING_SEARCH_RESULTS_ASSESSMENT.md` (assessment, next steps)
- `docs/research/PARTIAL_RESULTS_STREAMING_ANALYSIS.md` (design)
- `docs/research/2026-01-31_STREAMING_BLIND_SPOTS.md` (blind spots, recommendations, pre-implementation checklist)
- `docs/plans/features/LARGE_RESULT_SET_DISPLAY_PLAN.md` (result limiting coordination)

---

## 📊 Summary & Recommended Order

### Immediate Action Items (This Week)

1. ✅ **Complete DRY Refactoring** (Critical #1) - 2-3 hours
2. ✅ **Exception Handling in Search Lambdas** (Critical #2) - 1 hour
3. ✅ **Thread Pool Error Handling** (Critical #3) - 1 hour
4. ✅ **Settings Validation** (High #6) - 30 minutes
5. ✅ **Bounds Checking** (High #7) - 1 hour

**Total:** ~6 hours - Makes code production-ready

---

### Next Sprint (1-2 Weeks)

6. ✅ **macOS Application Logic Extraction** (High #4) - 5-6 hours
7. ✅ **macOS Full Search Infrastructure** (High #5) - 3-4 hours
8. ✅ **macOS File Operations** (Medium #8) - 2-3 hours

**Total:** ~10-13 hours - Achieves macOS feature parity

---

### Future Work (1-2 Months)

9. ✅ **Search Strategy Testing** (Medium #10) - 4-6 hours
10. ✅ **PathPatternMatcher Improvements** (Medium #11) - 8-12 hours
11. ✅ **macOS Application Bootstrap Parity** (Medium #9) - 14-20 hours
12. ✅ **Streaming Search Results** (Planned #18) - Phased; see assessment and research docs

**Total:** ~26-38+ hours - Quality improvements, architecture refinement, and streaming UX

---

## Quick Start Instructions

**To get production-ready quickly, execute these in order:**

1. **"Complete the DRY refactoring: replace all ProcessChunkRange lambdas in Static, Hybrid, and Dynamic strategies with ProcessChunkRangeForSearch calls. Also use RecordThreadTiming and CalculateChunkBytes helpers."**

2. **"Add exception handling: wrap ProcessChunkRangeForSearch calls in try-catch blocks in all three search strategy lambdas. Log exceptions and return empty results on error."**

3. **"Add error handling in SearchThreadPool: log warnings when tasks are rejected during shutdown, and add exception handling in worker threads to catch task exceptions."**

4. **"Add validation in GetLoadBalancingStrategy: validate the settings value and log warnings for invalid values, defaulting to Hybrid."**

5. **"Add defensive bounds checking in ProcessChunkRangeForSearch: verify chunk_start < chunk_end, chunk_end <= path_ids_.size(), and validate all array accesses."**

These 5 tasks will address all critical and high-priority issues and make the code production-ready.

---

## Notes

- **Build Restriction:** Never attempt to compile/build on macOS (Windows-only target)
- **Testing:** Use `scripts/build_tests_macos.sh` for macOS validation
- **Naming:** All code must follow `docs/standards/CXX17_NAMING_CONVENTIONS.md`
- **Windows API:** Always use `(std::min)` and `(std::max)` with parentheses

---

*Last Updated: 2026-01-31 (streaming assessment findings incorporated)*













