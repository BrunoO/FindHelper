# UI Main Thread Analysis

## Summary

**Question:** Is the UI handled by the main thread, and is this an issue?

**Answer:** 
- ✅ **YES**, the UI is handled by the main thread
- ✅ **NO**, this is **NOT an issue** - it's the **correct and required** pattern for ImGui
- ✅ The implementation follows best practices with proper mitigations for blocking operations

## Current Implementation

### Thread Architecture

All UI rendering happens on the main thread:

```319:424:src/core/Application.cpp
int Application::Run() {
  try {
    // Power-saving mode configuration
    // - Minimized: Sleep until event (0% CPU when not visible)
    // - Unfocused: ~10 FPS (user is in another app)
    // - Idle: ~10 FPS after 2 seconds of no interaction
    // - Active: Full speed (VSync-limited, typically 60 FPS)
    constexpr double kIdleTimeoutSeconds = 2.0;      // Consider idle after 2 seconds of no interaction
    constexpr double kIdleWaitTimeout = 0.1;         // 100ms timeout when idle (~10 FPS equivalent)
    
    // Idle detection state (tracked across frames)
    // Use member variable for last_interaction_time so it can be accessed from ProcessFrame
    last_interaction_time_ = std::chrono::steady_clock::now();
    auto last_mouse_pos = ImVec2(-1, -1);  // Invalid position to force first-frame detection
    bool was_idle = false;
    
    // Main loop
    while (!glfwWindowShouldClose(window_)) {
      // Priority 1: Window minimized - sleep until event arrives
      if (HandleMinimizedWindow()) {
        continue;  // Re-check window state before processing frame
      }
      
      // Priority 2: Window unfocused - reduce frame rate significantly
      if (HandleUnfocusedWindow()) {
        continue;
      }
      
      // Priority 3: Window focused - check if we should use idle mode
      bool is_idle = HandleFocusedWindow(kIdleTimeoutSeconds, kIdleWaitTimeout);
      
      // After frame: detect interaction for NEXT iteration's idle decision
      if (DetectUserInteraction(last_mouse_pos)) {
        last_interaction_time_ = std::chrono::steady_clock::now();
      }
      
      // Log idle state transitions (debug builds only)
      UpdateIdleStateLogging(is_idle, was_idle);
    }
    
    // Save settings before shutdown
    SaveSettingsOnShutdown();
    
    return 0;
  } catch (const std::bad_alloc& e) {
    (void)e;  // Suppress unused variable warning in Release mode
    LOG_ERROR_BUILD("Memory allocation failure in Application::Run(): " << e.what());
    return 1;
  } catch (const std::runtime_error& e) {
    (void)e;  // Suppress unused variable warning in Release mode
    LOG_ERROR_BUILD("Runtime error in Application::Run(): " << e.what());
    return 1;
  } catch (const std::exception& e) {
    (void)e;  // Suppress unused variable warning in Release mode
    LOG_ERROR_BUILD("Standard exception in Application::Run(): " << e.what());
    return 1;
  } catch (...) {
    LOG_ERROR("Unknown exception in Application::Run() - possible memory corruption or non-standard exception");
    return 1;
  }
}

void Application::ProcessFrame() {
  // Note: Event polling (glfwPollEvents/glfwWaitEvents/glfwWaitEventsTimeout)
  // is handled in Run() before calling ProcessFrame() to enable power-saving modes.
  
  // Trigger auto-crawl after first frame is processed (UI is shown)
  if (should_auto_crawl_ && !auto_crawl_triggered_) {
    TriggerAutoCrawl();
    auto_crawl_triggered_ = true;
  }
  
  // Start the Dear ImGui frame
  // Platform-specific frame setup (DirectX or Metal)
  if (renderer_) {
    renderer_->BeginFrame();
  }
  
  // Common ImGui frame setup (GLFW + NewFrame)
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  
  // Handle index dumping if requested
  HandleIndexDump();
  
  // Application logic (handles search, maintenance, keyboard shortcuts)
  const bool is_index_building = IsIndexBuilding();

  // Update index build state and search state
  UpdateIndexBuildState();
  UpdateSearchState(is_index_building);
  
  // Render UI
  RenderMainWindowContent(is_index_building);
  RenderFloatingWindows();
  
  // Rendering
  RenderFrame();
  
  // Render additional platform windows when multi-viewport is enabled
  const ImGuiIO &io = ImGui::GetIO();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
  }
}
```

**Flow:**
1. `Application::Run()` - Main loop on main thread
2. `Application::ProcessFrame()` - Called every frame on main thread
3. `Application::RenderMainWindowContent()` - All ImGui calls on main thread
4. `ResultsTable::Render()` - UI rendering on main thread

### Why This Is Correct

According to `docs/Historical/GEMINI_API_IMGUI_THREADING_ANALYSIS.md`:

> **ImGui is NOT thread-safe**: All ImGui function calls must be made from the main thread

This is a **requirement**, not a limitation. ImGui's immediate mode paradigm requires all rendering to happen on a single thread (the main thread).

## Potential Blocking Operations

While the UI runs on the main thread, the codebase has several mitigations to prevent blocking:

### 1. ✅ Background Search Operations

**Location:** `SearchWorker::WorkerThread()` runs in a background thread

- Search operations run in separate threads
- Results are polled non-blockingly each frame
- UI never waits for search to complete

### 2. ✅ Async File Attribute Loading

**Location:** `ResultsTable::Render()` uses futures for size/modification time loading

```48:94:src/ui/ResultsTable.cpp
void HandleTableSorting(GuiState& state, FileIndex& file_index, ThreadPool& thread_pool) {
  ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs();
  if (!sort_specs) {
    return;
  }

  if (sort_specs->SpecsDirty) {
    ScopedTimer timer("Search - Sorting");
    if (sort_specs->SpecsCount > 0) {
      const ImGuiTableColumnSortSpecs& spec = sort_specs->Specs[0];
      state.lastSortColumn = spec.ColumnIndex;
      state.lastSortDirection = spec.SortDirection;

      // For Size or Last Modified columns, use async loading
      if (spec.ColumnIndex == 2 || spec.ColumnIndex == 3) {
        StartSortAttributeLoading(state, state.searchResults, spec.ColumnIndex, file_index, thread_pool);
      } else {
        // For other columns, sort immediately (no attribute loading needed)
        SortSearchResults(state.searchResults, spec.ColumnIndex, spec.SortDirection, file_index, thread_pool);
        // Invalidate and rebuild filter caches after sorting
        state.timeFilterCacheValid = false;
        state.sizeFilterCacheValid = false;
        UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
        UpdateSizeFilterCacheIfNeeded(state, file_index);
      }
    }
    sort_specs->SpecsDirty = false;
  } else if (state.lastSortColumn != -1 && state.resultsUpdated) {
    ScopedTimer timer("Search - Re-sorting");

    // For Size or Last Modified columns, use async loading
    if (state.lastSortColumn == 2 || state.lastSortColumn == 3) {
      StartSortAttributeLoading(state, state.searchResults, state.lastSortColumn, file_index, thread_pool);
    } else {
      // For other columns, sort immediately (no attribute loading needed)
      SortSearchResults(state.searchResults, state.lastSortColumn, state.lastSortDirection, file_index, thread_pool);
      // Invalidate and rebuild filter caches after sorting
      state.timeFilterCacheValid = false;
      state.sizeFilterCacheValid = false;
      UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
      UpdateSizeFilterCacheIfNeeded(state, file_index);
    }

    // Consume the one-shot "results updated" flag
    state.resultsUpdated = false;
  }
}
```

- File attributes (size, modification time) load asynchronously using `std::future`
- UI shows "..." while loading
- No blocking on file I/O

### 3. ✅ Deferred Filter Cache Rebuilding

**Location:** `ResultsTable::Render()` defers filter cache rebuilds

According to `docs/analysis/FILTERING_SORTING_FLOW_ANALYSIS.md`:

> **Deferred for one frame** when results first arrive (to avoid blocking UI)

- Filter cache rebuilding is deferred for one frame when results first arrive
- Prevents UI freezing during large result set filtering

### 4. ✅ Parallel Sorting

**Location:** `SortSearchResults()` uses `ThreadPool` for parallel sorting

- Sorting operations use thread pools for parallelization
- Large result sets are sorted in parallel, not sequentially

### 5. ✅ Idle-Time Maintenance

**Location:** `ApplicationLogic::Update()` only runs maintenance during idle

```95:115:src/core/ApplicationLogic.cpp
    // Periodic maintenance: Rebuild path buffer during idle time
    // Only perform maintenance if search is not active and index is ready
    if (!search_worker.IsBusy()) {
      static auto last_maintenance_check = std::chrono::steady_clock::now();
      // Check at configured interval during idle time
#ifdef _WIN32
      if (std::chrono::duration_cast<std::chrono::seconds>(now - last_maintenance_check).count() >=
          usn_monitor_constants::kMaintenanceCheckIntervalSeconds) {
        last_maintenance_check = now;
        (void)file_index.Maintain(); // Return value indicates if maintenance was performed
      }
#else
      // macOS: Use same interval (5 seconds) but don't depend on Windows-specific constants
      constexpr int kMaintenanceCheckIntervalSeconds = 5;
      if (std::chrono::duration_cast<std::chrono::seconds>(now - last_maintenance_check).count() >=
          kMaintenanceCheckIntervalSeconds) {
        last_maintenance_check = now;
        (void)file_index.Maintain(); // Return value indicates if maintenance was performed
      }
#endif
    }
```

- `FileIndex::Maintain()` only runs when:
  - Search is not active (`!search_worker.IsBusy()`)
  - At configured intervals (5 seconds on macOS, configurable on Windows)
- Never blocks during active user interaction

### 6. ✅ Non-Blocking Future Checks

**Location:** `ResultsTable::Render()` checks futures with `wait_for(0)`

```376:390:src/ui/ResultsTable.cpp
    // CRITICAL: Check for async sorting completion BEFORE setting
    // display_results This ensures that if sorting completes, display_results
    // will point to sorted data
    // Also check sortDataReady flag for immediate sorting when all data is already loaded
    if ((!state.attributeLoadingFutures.empty() || state.sortDataReady) &&
        CheckSortAttributeLoadingAndSort(
            state, state.searchResults, state.lastSortColumn,
            state.lastSortDirection, state.sort_generation_)) {
      // Sort completed - invalidate and rebuild filter caches
      // Caches must be invalidated because searchResults order changed
      state.timeFilterCacheValid = false;
      state.sizeFilterCacheValid = false;
      UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
      UpdateSizeFilterCacheIfNeeded(state, file_index);
    }
```

- Futures are checked with non-blocking `wait_for(std::chrono::seconds(0))`
- UI never waits for async operations

## Known Issues and Mitigations

### Historical: UI Blocking from Lock Contention

**Documented in:** `docs/archive/UI_BLOCKING_FIX.md`

**Issue:** Lock contention from multiple `GetEntry()` calls during parallel filtering

**Status:** Addressed through:
- Batch lookups where possible
- Lock held for entire search duration (prevents vector modifications)
- Filter cache rebuilding deferred to avoid blocking

### Potential: Brief Blocking in Gemini API

**Documented in:** `docs/Historical/GEMINI_API_IMGUI_THREADING_ANALYSIS.md`

**Issue:** `StartGeminiApiCall()` may briefly block if previous call is still running

**Mitigations:**
1. Button is disabled during API calls (prevents rapid clicks)
2. Non-blocking check first (`wait_for(0)`)
3. Brief blocking is acceptable (just waiting for HTTP response to complete)
4. Prevents memory leaks from abandoned futures

**Status:** Acceptable with current mitigations

## Alternative Approaches (Not Recommended)

### ❌ Separate UI Thread

**Why not:** ImGui is **not thread-safe** and **requires** all calls on the main thread. This would cause crashes and undefined behavior.

### ❌ Worker Thread with Message Queue

**Why not:** ImGui's immediate mode paradigm doesn't support this pattern. The UI must be rebuilt every frame from application state.

### ✅ Current Approach (Recommended)

**Why yes:**
- Follows ImGui requirements
- Heavy operations run in background threads
- Results synchronized back to main thread
- Non-blocking checks for async operations
- Deferred operations to avoid blocking
- Idle-time maintenance only

## Performance Characteristics

### Frame Rate Management

The application uses intelligent frame rate management:

1. **Minimized:** Sleep until event (0% CPU)
2. **Unfocused:** ~10 FPS (user in another app)
3. **Idle:** ~10 FPS after 2 seconds of no interaction
4. **Active:** Full speed (VSync-limited, typically 60 FPS)

This prevents unnecessary CPU usage while maintaining responsiveness.

### Virtual Scrolling

**Location:** `ResultsTable::Render()` uses `ImGuiListClipper`

```438:441:src/ui/ResultsTable.cpp
      ImGuiListClipper clipper;
      clipper.Begin(static_cast<int>(display_results.size()));  // NOSONAR(cpp:S1905) - Required cast: ImGui API expects int
      while (clipper.Step()) {  // NOSONAR(cpp:S134) - Nested loops acceptable: ImGui clipper pattern requires table->clipper->for nesting
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
```

- Only visible rows are rendered
- Large result sets don't impact performance
- ImGui handles virtual scrolling automatically

## Conclusion

### ✅ Current Implementation is Correct

1. **UI on main thread is REQUIRED** by ImGui (not optional)
2. **All blocking operations are mitigated:**
   - Background threads for heavy work
   - Async operations with futures
   - Deferred cache rebuilding
   - Idle-time maintenance only
   - Non-blocking future checks
3. **Performance is optimized:**
   - Virtual scrolling for large lists
   - Parallel sorting
   - Intelligent frame rate management
   - Lazy loading of file attributes

### Recommendations

**No changes needed** - the current implementation follows best practices:

1. ✅ Continue using main thread for all ImGui calls
2. ✅ Continue using background threads for heavy operations
3. ✅ Continue using async operations with futures
4. ✅ Continue deferring expensive operations when possible
5. ✅ Continue using idle-time maintenance only

### If UI Freezing Occurs

If UI freezing is observed in practice:

1. **Profile the frame time** - identify which operation is blocking
2. **Check for blocking operations** - look for:
   - Synchronous file I/O
   - Blocking waits on futures (use `wait_for(0)` instead of `wait()`)
   - Heavy computations in render functions
3. **Consider further deferral** - move expensive operations to idle time or background threads
4. **Use profiling tools** - identify hotspots in `ProcessFrame()` or `RenderMainWindowContent()`

## References

- `docs/Historical/GEMINI_API_IMGUI_THREADING_ANALYSIS.md` - Threading analysis
- `docs/design/IMGUI_IMMEDIATE_MODE_PARADIGM.md` - ImGui paradigm explanation
- `docs/analysis/FILTERING_SORTING_FLOW_ANALYSIS.md` - Filtering and sorting flow
- `docs/archive/UI_BLOCKING_FIX.md` - Historical UI blocking issue
- `AGENTS.md` - ImGui threading rules (lines 1091-1094)
