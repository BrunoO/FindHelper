# Status Bar Information Overload - Implementation Plan

**Issue**: Issue #3 from UI_IMPROVEMENT_IDEAS_SUMMARY.md  
**Severity**: High  
**Effort**: M (2-8 hours)  
**Status**: 📋 Planning

---

## Problem Statement

The status bar currently displays **10+ data points** in a single line, making it difficult to parse and creating high cognitive load:

```
v-X.X | (Release) | Monitoring Active | Total Items: XXX | Queue: X | Displayed: XXX | Search: Xms | Status: Idle | Memory: XXX | Search Threads: X | XX.X FPS
```

**Current Issues:**
- Too much information in one line
- Difficult to quickly find specific information
- Technical details (FPS, thread count) rarely needed by most users
- Status bar takes significant vertical space
- No visual grouping of related information

---

## Solution Overview

Reorganize the status bar into **three logical groups** with conditional display and move technical details to the Metrics window.

### Proposed Layout

```
[LEFT GROUP]                    [CENTER GROUP]                    [RIGHT GROUP]
v-X.X (Release)                 Total: XXX | Displayed: XXX       Status: Idle
[●] Monitoring Active           Search: Xms (when completed)      Memory: XXX
```

**Key Changes:**
1. **Left Group**: Version, build type, monitoring status (with colored dot icon)
2. **Center Group**: File counts, search time (when available)
3. **Right Group**: Status, memory usage
4. **Moved to Metrics Window**: FPS, thread count, queue size (when not building)

---

## Implementation Plan

### Phase 1: Group Information Logically (2-3 hours)

**Goal**: Reorganize status bar into three groups (Left, Center, Right)

#### 1.1 Create Helper Functions for Grouping

**File**: `ui/StatusBar.cpp`

Add helper functions to render each group:

```cpp
// Left group: Version, build type, monitoring status
static void RenderLeftGroup(const UsnMonitor* monitor);

// Center group: File counts, search time
static void RenderCenterGroup(const GuiState& state, 
                              const UsnMonitor* monitor,
                              const FileIndex& file_index,
                              const SearchWorker& search_worker);

// Right group: Status, memory
static void RenderRightGroup(const GuiState& state, 
                             const SearchWorker& search_worker);
```

#### 1.2 Refactor Main Render Function

**Changes:**
- Replace single-line layout with three-group layout
- Use `ImGui::SameLine()` with spacing to create visual groups
- Add spacing between groups (e.g., `ImGui::Spacing()` or `ImGui::SetCursorPosX()`)

**Layout Structure:**
```cpp
void StatusBar::Render(...) {
  ImGui::Separator();
  
  // Left group
  RenderLeftGroup(monitor);
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0f); // Spacing
  
  // Center group
  RenderCenterGroup(state, monitor, file_index, search_worker);
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - right_group_width - 10.0f); // Align right
  
  // Right group
  RenderRightGroup(state, search_worker);
}
```

**Estimated Effort**: 2-3 hours

---

### Phase 2: Replace "Monitoring Active" with Colored Dot Icon (1 hour)

**Goal**: Use compact icon instead of text for monitoring status

#### 2.1 Replace Text with Colored Dot

**File**: `ui/StatusBar.cpp`

**Current Code:**
```cpp
ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Monitoring Active");
```

**New Code:**
```cpp
// Use colored dot instead of text - more compact
ImVec4 dot_color;
if (monitor->IsPopulatingIndex()) {
  dot_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); // Yellow (building)
} else if (monitor->IsActive()) {
  auto metrics = monitor->GetMetricsSnapshot();
  if (metrics.buffers_dropped > 0) {
    dot_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red (errors)
  } else {
    dot_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green (active)
  }
} else {
  dot_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red (inactive)
}

// Render colored dot (using text-based "[*]" for font compatibility)
ImGui::PushStyleColor(ImGuiCol_Text, dot_color);
ImGui::Text("[*]");
ImGui::PopStyleColor();

// Optional: Show tooltip on hover with full status
if (ImGui::IsItemHovered()) {
  ImGui::BeginTooltip();
  if (monitor->IsPopulatingIndex()) {
    ImGui::Text("Building Index...");
  } else if (monitor->IsActive()) {
    ImGui::Text("Monitoring Active");
  } else {
    ImGui::Text("Monitoring Inactive");
  }
  ImGui::EndTooltip();
}
```

**Note**: Using `[*]` instead of `●` (bullet) for font compatibility (same approach as `[R]` button).

**Estimated Effort**: 1 hour

---

### Phase 3: Move Technical Details to Metrics Window (1-2 hours)

**Goal**: Remove FPS, thread count, and queue size (when not building) from status bar

#### 3.1 Remove from Status Bar

**File**: `ui/StatusBar.cpp`

**Remove:**
- FPS display (line 177-179)
- Search Threads display (line 167-175)
- Queue size (when not building - keep it only during index building)

**Keep Queue During Building:**
```cpp
// Only show queue during index building (when it's most relevant)
if (monitor && monitor->IsPopulatingIndex()) {
  size_t queue_size = monitor->GetQueueSize();
  if (queue_size > 0) {
    ImGui::Text("Queue: %zu", queue_size);
    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();
  }
}
```

#### 3.2 Add Missing Metrics to Metrics Window

**File**: `ui/MetricsWindow.cpp`

**Current Status:**
- ✅ Queue size: Already displayed in "Queue Statistics" section
- ❌ FPS: Not currently displayed (needs to be added)
- ❌ Thread count: Not currently displayed (needs to be added)

**Add New Section: "System Performance"**

```cpp
// System Performance (cross-platform)
if (ImGui::CollapsingHeader("System Performance")) {
  // FPS
  ImGuiIO& io = ImGui::GetIO();
  RenderMetricText("Frames per second (UI rendering performance)",
                   "FPS: %.1f", io.Framerate);
  
  // Thread count
  size_t thread_count = file_index.GetSearchThreadPoolCount();
  RenderMetricText("Number of threads in search thread pool",
                   "Search Threads: %zu", thread_count);
}
```

**Estimated Effort**: 1-2 hours

---

### Phase 4: Conditional Display Improvements (1 hour)

**Goal**: Only show information when relevant

#### 4.1 Search Time Conditional Display

**Current**: Shows "Search: ..." while searching, or time when completed

**Improvement**: Only show search time when search is completed (remove "..." placeholder)

```cpp
// Only show search time when search is completed
if (!search_worker.IsBusy()) {
  auto search_metrics = search_worker.GetMetricsSnapshot();
  if (search_metrics.total_searches > 0) {
    // Show search time
  }
  // Don't show anything if no search has been performed
}
```

#### 4.2 Queue Size Conditional Display

**Current**: Only shows when > 0

**Improvement**: Only show during index building (most relevant time)

```cpp
// Only show queue during index building
if (monitor && monitor->IsPopulatingIndex()) {
  size_t queue_size = monitor->GetQueueSize();
  if (queue_size > 0) {
    // Show queue
  }
}
```

**Estimated Effort**: 1 hour

---

### Phase 5: Multi-line Support for Small Windows (Optional, 1-2 hours)

**Goal**: Allow status bar to wrap on smaller windows

#### 5.1 Detect Window Width

```cpp
float window_width = ImGui::GetWindowWidth();
float min_width_for_single_line = 1200.0f; // Approximate minimum width

if (window_width < min_width_for_single_line) {
  // Use multi-line layout
  RenderLeftGroup(monitor);
  ImGui::NewLine();
  RenderCenterGroup(...);
  ImGui::SameLine();
  RenderRightGroup(...);
} else {
  // Use single-line layout
  RenderLeftGroup(monitor);
  ImGui::SameLine();
  RenderCenterGroup(...);
  ImGui::SameLine();
  RenderRightGroup(...);
}
```

**Estimated Effort**: 1-2 hours (optional, can be deferred)

---

## Implementation Order

### Recommended Sequence

1. **Phase 1**: Group Information Logically (2-3 hours)
   - Foundation for all other improvements
   - Makes code more maintainable

2. **Phase 2**: Replace "Monitoring Active" with Colored Dot (1 hour)
   - Quick win, immediate visual improvement
   - Reduces horizontal space usage

3. **Phase 3**: Move Technical Details to Metrics Window (1-2 hours)
   - Reduces status bar clutter
   - Makes Metrics window more useful

4. **Phase 4**: Conditional Display Improvements (1 hour)
   - Polishes the implementation
   - Removes unnecessary information

5. **Phase 5**: Multi-line Support (Optional, 1-2 hours)
   - Can be deferred if not critical
   - Nice-to-have for small windows

**Total Estimated Effort**: 5-9 hours (4-7 hours without Phase 5)

---

## Code Structure

### Proposed File Structure

```
ui/StatusBar.cpp
├── Render() [Main entry point]
├── RenderLeftGroup() [Version, build, monitoring]
├── RenderCenterGroup() [Counts, search time]
├── RenderRightGroup() [Status, memory]
└── FormatMemory() [Existing helper]
```

### Example Implementation Skeleton

```cpp
namespace ui {

// Forward declarations for helper functions
static void RenderLeftGroup(const UsnMonitor* monitor, const FileIndex& file_index);
static void RenderCenterGroup(const GuiState& state, 
                              const UsnMonitor* monitor,
                              const FileIndex& file_index,
                              const SearchWorker& search_worker);
static void RenderRightGroup(const GuiState& state, 
                             const SearchWorker& search_worker);

void StatusBar::Render(GuiState &state,
                      SearchWorker &search_worker,
                      const UsnMonitor *monitor,
                      FileIndex &file_index) {
  ImGui::Separator();

  // Left group: Version, build type, monitoring
  RenderLeftGroup(monitor, file_index);
  
  // Spacing between groups
  ImGui::SameLine();
  float left_group_end = ImGui::GetCursorPosX();
  ImGui::SetCursorPosX(left_group_end + 30.0f); // 30px spacing
  
  // Center group: Counts, search time
  RenderCenterGroup(state, monitor, file_index, search_worker);
  
  // Right group: Status, memory (aligned to right)
  ImGui::SameLine();
  float right_group_width = CalculateRightGroupWidth(state, search_worker);
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - right_group_width - 10.0f);
  RenderRightGroup(state, search_worker);
}

static void RenderLeftGroup(const UsnMonitor* monitor, const FileIndex& file_index) {
  // Version and build type
  ImGui::TextDisabled("v-%s", APP_VERSION);
  ImGui::SameLine();
  // ... build type ...
  ImGui::SameLine();
  ImGui::Text("|");
  ImGui::SameLine();
  
  // Monitoring status with colored dot
  if (monitor) {
    // Render colored dot based on status
  } else {
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "macOS (No Monitoring)");
  }
}

static void RenderCenterGroup(...) {
  // Total items, displayed count, search time
}

static void RenderRightGroup(...) {
  // Status, memory
}

} // namespace ui
```

---

## Testing Checklist

### Visual Testing
- [ ] Status bar displays correctly on Windows
- [ ] Status bar displays correctly on macOS
- [ ] Three groups are visually distinct
- [ ] Colored dot displays correctly (not showing as "?")
- [ ] Spacing between groups is appropriate
- [ ] Right group aligns to window edge

### Functional Testing
- [ ] Monitoring status dot changes color correctly
- [ ] Tooltip shows full status on hover
- [ ] Search time only shows when search completed
- [ ] Queue only shows during index building
- [ ] FPS and thread count are in Metrics window
- [ ] All information is still accessible

### Edge Cases
- [ ] Status bar on very narrow windows (if Phase 5 implemented)
- [ ] Status bar when monitor is nullptr (macOS)
- [ ] Status bar during index building
- [ ] Status bar when no search has been performed
- [ ] Status bar when memory is N/A

---

## Success Criteria

### Must Have
1. ✅ Status bar has three distinct groups (Left, Center, Right)
2. ✅ "Monitoring Active" replaced with colored dot icon
3. ✅ FPS and thread count moved to Metrics window
4. ✅ Queue only shows during index building
5. ✅ Search time only shows when search completed

### Nice to Have
6. ✅ Multi-line support for small windows
7. ✅ Tooltips for compact icons (colored dot)

---

## Related Files

- **Primary**: `ui/StatusBar.cpp` - Main implementation
- **Header**: `ui/StatusBar.h` - Function declarations
- **Metrics Window**: `ui/MetricsWindow.cpp` - Should display FPS, threads, queue
- **Usage**: `Application.cpp` - Calls `StatusBar::Render()`

---

## Notes

- **Font Compatibility**: Use `[*]` instead of `●` for colored dot (same approach as `[R]` button)
- **Metrics Window**: Verify that FPS, thread count, and queue size are already displayed there, or add them if missing
- **Backward Compatibility**: All information should still be accessible, just reorganized
- **Performance**: No performance impact expected (same data, different layout)

---

## Future Enhancements (Out of Scope)

- Clickable status bar sections (e.g., click version to show about dialog)
- Configurable status bar layout (user preference)
- Status bar themes/styles
- Collapsible status bar sections

