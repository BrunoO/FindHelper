# UI Separation Review and Proposals

**Date:** 2026-01-07  
**Purpose:** Review UI code separation from application logic and propose improvements

---

## Executive Summary

The codebase has **good separation** in many areas, but there are **several coupling issues** where UI components directly depend on application logic classes (`SearchController`, `SearchWorker`, `FileIndex`) and perform business logic operations. This document identifies these issues and proposes solutions to achieve better separation of concerns.

---

## Current Architecture Overview

### ✅ Good Separation

1. **`application_logic` namespace** - Cleanly separated application logic
   - Located in `src/core/ApplicationLogic.h/cpp`
   - Handles search controller updates, keyboard shortcuts, maintenance tasks
   - No UI dependencies

2. **UI components in `namespace ui`** - All UI components properly namespaced
   - Located in `src/ui/`
   - Static rendering methods (stateless, immediate mode pattern)

3. **`GuiState` class** - UI state properly encapsulated
   - Located in `src/gui/GuiState.h`
   - Contains all UI state, no business logic

### ⚠️ Separation Issues

1. **UI components directly depend on application logic classes**
   - `SearchController`, `SearchWorker`, `FileIndex` passed as parameters to UI rendering methods
   - UI components call `TriggerManualSearch()` directly
   - UI components perform filter cache updates and sorting operations

2. **Application class mixes concerns**
   - Contains both rendering methods (`RenderMainWindowContent()`) and application logic helpers
   - Some rendering logic in `Application` class instead of UI components

3. **Business logic in UI components**
   - `ResultsTable` performs sorting and filter cache updates
   - `FilterPanel` triggers searches directly
   - `EmptyState` triggers searches directly

---

## Detailed Analysis

### Issue 1: Direct Dependencies on Application Logic Classes

**Problem:** UI components receive and use `SearchController`, `SearchWorker`, and `FileIndex` directly.

**Examples:**

```cpp
// src/ui/SearchInputs.h
static void Render(GuiState &state,
                   const SearchController *search_controller,
                   SearchWorker *search_worker,
                   bool is_index_building,
                   GLFWwindow* window = nullptr);

// src/ui/EmptyState.h
static void Render(GuiState& state,
                   size_t indexed_file_count,
                   const SearchController &search_controller,
                   SearchWorker &search_worker,
                   const AppSettings &settings);

// src/ui/FilterPanel.h
static void RenderSavedSearches(
    GuiState &state,
    const AppSettings &settings_state,
    const SearchController &search_controller,
    SearchWorker &search_worker,
    bool is_index_building);
```

**Impact:**
- UI components are tightly coupled to application logic
- Hard to test UI components in isolation
- Changes to `SearchController`/`SearchWorker` require UI changes
- Violates Dependency Inversion Principle (DIP)

**Current Usage Pattern:**
```cpp
// UI component directly calls application logic
search_controller->TriggerManualSearch(state, *search_worker);
```

---

### Issue 2: Business Logic in UI Components

**Problem:** UI components perform business logic operations like sorting, filter cache updates, and search triggering.

**Examples:**

#### ResultsTable.cpp - Sorting and Filter Cache Updates
```cpp
void HandleTableSorting(GuiState& state, FileIndex& file_index, ThreadPool& thread_pool) {
  // ... sorting logic ...
  // Invalidate and rebuild filter caches after sorting
  state.timeFilterCacheValid = false;
  state.sizeFilterCacheValid = false;
  UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
  UpdateSizeFilterCacheIfNeeded(state, file_index);
}
```

#### ResultsTable.cpp - Filter Cache Management
```cpp
// Ensure filter caches are up to date and get display results
UpdateTimeFilterCacheIfNeeded(state, file_index, &thread_pool);
UpdateSizeFilterCacheIfNeeded(state, file_index);
```

**Impact:**
- UI components contain business logic
- Hard to test business logic separately from UI
- UI components need deep knowledge of data structures
- Violates Single Responsibility Principle (SRP)

---

### Issue 3: Application Class Mixes Concerns

**Problem:** `Application` class contains both rendering methods and application logic helpers.

**Examples:**

```cpp
// Application.h - Mixing rendering and logic
void RenderMainWindowContent(bool is_index_building);
void RenderFloatingWindows();
void RenderManualSearchHeader(bool& is_expanded);
void RenderManualSearchContent(bool is_expanded, bool is_index_building);
void RenderAISearchSection(bool is_index_building);
void UpdateIndexBuildState();  // Application logic
void UpdateSearchState(bool is_index_building);  // Application logic
```

**Impact:**
- `Application` class has multiple responsibilities
- Hard to test rendering logic separately
- Application logic mixed with UI rendering

---

### Issue 4: SettingsWindow Takes Application Pointer

**Problem:** `SettingsWindow` receives `Application*` pointer, creating tight coupling.

**Example:**

```cpp
// src/ui/SettingsWindow.h
static void Render(bool *p_open,
                   AppSettings &settings,
                   FileIndex &file_index,
                   Application* application,  // ⚠️ Tight coupling
                   GLFWwindow* glfw_window = nullptr);
```

**Usage:**
```cpp
// src/core/Application.cpp
ui::SettingsWindow::Render(&show_settings_, *settings_, *file_index_, this, window_);
```

**Impact:**
- UI component depends on entire `Application` class
- Hard to test `SettingsWindow` in isolation
- Changes to `Application` may require `SettingsWindow` changes

---

## Proposed Solutions

### Solution 1: Introduce UI Action Interface (Recommended)

**Approach:** Create a lightweight interface for UI actions that UI components can call, instead of directly calling application logic.

**Benefits:**
- Decouples UI from application logic
- Easy to mock for testing
- Clear separation of concerns
- Follows Dependency Inversion Principle

**Implementation:**

#### Step 1: Create UI Action Interface

```cpp
// src/gui/UIActions.h
#pragma once

#include <string>

namespace ui {
  /**
   * @brief Interface for UI actions that UI components can trigger
   * 
   * This interface decouples UI components from application logic classes.
   * UI components call methods on this interface instead of directly
   * calling SearchController, SearchWorker, etc.
   */
  class UIActions {
   public:
    virtual ~UIActions() = default;
    
    /**
     * @brief Trigger a manual search with current state
     * @param state Current GUI state (read-only)
     */
    virtual void TriggerManualSearch(const GuiState& state) = 0;
    
    /**
     * @brief Apply a saved search configuration to state
     * @param state GUI state to modify
     * @param saved_search_name Name of saved search to apply
     */
    virtual void ApplySavedSearch(GuiState& state, const std::string& saved_search_name) = 0;
    
    /**
     * @brief Check if index is currently building
     * @return true if index is building, false otherwise
     */
    virtual bool IsIndexBuilding() const = 0;
    
    /**
     * @brief Get indexed file count
     * @return Number of files in index
     */
    virtual size_t GetIndexedFileCount() const = 0;
  };
}
```

#### Step 2: Implement UIActions in Application

```cpp
// src/core/Application.h
#include "gui/UIActions.h"

class Application : public ui::UIActions {
 public:
  // ... existing methods ...
  
  // UIActions interface implementation
  void TriggerManualSearch(const GuiState& state) override {
    search_controller_.TriggerManualSearch(state_, search_worker_);
  }
  
  void ApplySavedSearch(GuiState& state, const std::string& saved_search_name) override {
    // Find and apply saved search
    // ...
  }
  
  bool IsIndexBuilding() const override {
    return IsIndexBuilding();
  }
  
  size_t GetIndexedFileCount() const override {
    return monitor_ ? monitor_->GetIndexedFileCount() : file_index_->Size();
  }
};
```

#### Step 3: Update UI Components to Use UIActions

```cpp
// src/ui/SearchInputs.h
namespace ui {
  class UIActions;  // Forward declaration
  
  class SearchInputs {
   public:
    static void Render(GuiState &state,
                      UIActions* actions,  // ✅ Interface instead of concrete classes
                      bool is_index_building,
                      GLFWwindow* window = nullptr);
    
    static void RenderAISearch(GuiState &state,
                               GLFWwindow* window,
                               UIActions* actions,  // ✅ Interface
                               bool is_index_building,
                               bool *show_settings = nullptr,
                               bool *show_metrics = nullptr);
  };
}

// src/ui/SearchInputs.cpp
void SearchInputs::Render(GuiState &state,
                          UIActions* actions,
                          bool is_index_building,
                          GLFWwindow* window) {
  // ... UI rendering ...
  
  if (ImGui::Button("Search now")) {
    if (actions) {
      actions->TriggerManualSearch(state);  // ✅ Call through interface
    }
  }
}
```

**Migration Path:**
1. Create `UIActions` interface
2. Implement in `Application`
3. Update UI components one by one to use `UIActions*` instead of concrete classes
4. Remove direct dependencies from UI components

---

### Solution 2: Extract Business Logic from UI Components

**Approach:** Move business logic operations (sorting, filter cache updates) out of UI components into separate service classes.

**Benefits:**
- UI components only handle rendering
- Business logic can be tested independently
- Clear separation of concerns

**Implementation:**

#### Step 1: Create Search Results Service

```cpp
// src/search/SearchResultsService.h
#pragma once

#include "gui/GuiState.h"
#include "index/FileIndex.h"
#include "utils/ThreadPool.h"

namespace search {
  /**
   * @brief Service for managing search results (sorting, filtering, caching)
   * 
   * This service handles all business logic related to search results,
   * separating it from UI rendering code.
   */
  class SearchResultsService {
   public:
    /**
     * @brief Sort search results based on column and direction
     * @param state GUI state (modified: searchResults sorted, filter caches invalidated)
     * @param file_index File index for attribute lookups
     * @param thread_pool Thread pool for async operations
     * @param column_index Column to sort by
     * @param sort_direction Sort direction
     */
    static void SortResults(GuiState& state,
                            FileIndex& file_index,
                            ThreadPool& thread_pool,
                            int column_index,
                            ImGuiSortDirection sort_direction);
    
    /**
     * @brief Update filter caches if needed
     * @param state GUI state (modified: filter caches updated)
     * @param file_index File index for attribute lookups
     * @param thread_pool Thread pool for async operations
     */
    static void UpdateFilterCaches(GuiState& state,
                                   FileIndex& file_index,
                                   ThreadPool& thread_pool);
    
    /**
     * @brief Get display results (applying active filters)
     * @param state GUI state (read-only)
     * @return Pointer to results to display (filtered or unfiltered)
     */
    static const std::vector<SearchResult>* GetDisplayResults(const GuiState& state);
  };
}
```

#### Step 2: Update ResultsTable to Use Service

```cpp
// src/ui/ResultsTable.cpp
#include "search/SearchResultsService.h"

void ResultsTable::Render(GuiState& state,
                          NativeWindowHandle native_window,
                          GLFWwindow* glfw_window,
                          ThreadPool& thread_pool,
                          SearchWorker& search_worker,
                          FileIndex& file_index) {
  // ... UI setup ...
  
  // Handle sorting (delegates to service)
  ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs();
  if (sort_specs && sort_specs->SpecsDirty) {
    const ImGuiTableColumnSortSpecs& spec = sort_specs->Specs[0];
    SearchResultsService::SortResults(state, file_index, thread_pool,
                                      spec.ColumnIndex, spec.SortDirection);
    sort_specs->SpecsDirty = false;
  }
  
  // Update filter caches (delegates to service)
  SearchResultsService::UpdateFilterCaches(state, file_index, thread_pool);
  
  // Get display results (delegates to service)
  const std::vector<SearchResult>* display_results =
      SearchResultsService::GetDisplayResults(state);
  
  // ... render table with display_results ...
}
```

**Migration Path:**
1. Create `SearchResultsService`
2. Move sorting logic from `ResultsTable` to service
3. Move filter cache logic from `ResultsTable` to service
4. Update `ResultsTable` to call service methods

---

### Solution 3: Extract Rendering Methods from Application

**Approach:** Move rendering methods from `Application` class to a dedicated UI coordinator or move them to appropriate UI components.

**Benefits:**
- `Application` class focuses on application lifecycle
- Rendering logic in UI layer where it belongs
- Easier to test rendering separately

**Implementation:**

#### Option A: Create UIRenderer Coordinator (Recommended)

```cpp
// src/ui/UIRenderer.h
#pragma once

namespace ui {
  class UIActions;  // Forward declaration
  
  /**
   * @brief Coordinates rendering of all UI components
   * 
   * This class handles the main window setup and coordinates
   * rendering of all UI components. It replaces rendering methods
   * currently in the Application class.
   */
  class UIRenderer {
   public:
    /**
     * @brief Render main window content
     * @param state GUI state
     * @param actions UI actions interface
     * @param settings Application settings
     * @param native_window Native window handle
     * @param glfw_window GLFW window handle
     * @param is_index_building Whether index is building
     */
    static void RenderMainWindow(GuiState& state,
                                 UIActions* actions,
                                 AppSettings& settings,
                                 NativeWindowHandle native_window,
                                 GLFWwindow* glfw_window,
                                 bool is_index_building);
    
    /**
     * @brief Render floating windows (Settings, Metrics, etc.)
     * @param state GUI state
     * @param actions UI actions interface
     * @param settings Application settings
     * @param show_settings Settings window visibility flag
     * @param show_metrics Metrics window visibility flag
     */
    static void RenderFloatingWindows(GuiState& state,
                                     UIActions* actions,
                                     AppSettings& settings,
                                     bool* show_settings,
                                     bool* show_metrics);
  };
}
```

#### Option B: Move Methods to UI Components

Move `RenderManualSearchHeader()`, `RenderManualSearchContent()`, `RenderAISearchSection()` to appropriate UI components:

- `RenderManualSearchHeader()` → `ui::SearchControls`
- `RenderManualSearchContent()` → `ui::SearchInputs`
- `RenderAISearchSection()` → `ui::SearchInputs`

**Migration Path:**
1. Create `UIRenderer` coordinator class
2. Move rendering methods from `Application` to `UIRenderer`
3. Update `Application::ProcessFrame()` to call `UIRenderer::RenderMainWindow()`
4. Remove rendering methods from `Application`

---

### Solution 4: Replace Application Pointer with Specific Interface

**Problem:** `SettingsWindow` takes `Application*` pointer.

**Solution:** Replace with specific interface or callback functions.

**Implementation:**

#### Option A: Use Callback Functions (Simpler)

```cpp
// src/ui/SettingsWindow.h
namespace ui {
  class SettingsWindow {
   public:
    // Callback function type for triggering recrawl
    using RecrawlCallback = std::function<void(const std::string& folder_path)>;
    
    static void Render(bool *p_open,
                       AppSettings &settings,
                       FileIndex &file_index,
                       RecrawlCallback recrawl_callback,  // ✅ Callback instead of Application*
                       GLFWwindow* glfw_window = nullptr);
  };
}

// src/ui/SettingsWindow.cpp
void SettingsWindow::Render(bool *p_open,
                            AppSettings &settings,
                            FileIndex &file_index,
                            RecrawlCallback recrawl_callback,
                            GLFWwindow* glfw_window) {
  // ... UI rendering ...
  
  if (ImGui::Button("Start Crawl")) {
    if (recrawl_callback) {
      recrawl_callback(settings.crawlFolder);  // ✅ Call callback
    }
  }
}

// src/core/Application.cpp
ui::SettingsWindow::Render(&show_settings_,
                           *settings_,
                           *file_index_,
                           [this](const std::string& folder) {  // ✅ Lambda callback
                             this->StartCrawl(folder);
                           },
                           window_);
```

#### Option B: Use UIActions Interface

```cpp
// src/ui/SettingsWindow.h
namespace ui {
  class UIActions;  // Forward declaration
  
  class SettingsWindow {
   public:
    static void Render(bool *p_open,
                       AppSettings &settings,
                       FileIndex &file_index,
                       UIActions* actions,  // ✅ Interface instead of Application*
                       GLFWwindow* glfw_window = nullptr);
  };
}
```

**Migration Path:**
1. Add callback or interface method to `UIActions`
2. Update `SettingsWindow` to use callback/interface
3. Update `Application` to pass callback/interface
4. Remove `Application*` parameter

---

## Recommended Implementation Order

### Phase 1: Create Interfaces (Low Risk)
1. ✅ Create `UIActions` interface
2. ✅ Implement `UIActions` in `Application`
3. ✅ Create `SearchResultsService` for business logic

**Estimated Effort:** 2-3 hours  
**Risk:** Low - Adding new code, not modifying existing

### Phase 2: Migrate UI Components (Medium Risk)
1. ✅ Update `SearchInputs` to use `UIActions*`
2. ✅ Update `EmptyState` to use `UIActions*`
3. ✅ Update `FilterPanel` to use `UIActions*`
4. ✅ Update `ResultsTable` to use `SearchResultsService`

**Estimated Effort:** 4-6 hours  
**Risk:** Medium - Modifying existing UI components

### Phase 3: Extract Rendering from Application (Medium Risk)
1. ✅ Create `UIRenderer` coordinator
2. ✅ Move rendering methods from `Application` to `UIRenderer`
3. ✅ Update `Application::ProcessFrame()` to use `UIRenderer`

**Estimated Effort:** 3-4 hours  
**Risk:** Medium - Refactoring core Application class

### Phase 4: Remove Application Pointer (Low Risk)
1. ✅ Update `SettingsWindow` to use callback or `UIActions`
2. ✅ Remove `Application*` parameter

**Estimated Effort:** 1-2 hours  
**Risk:** Low - Isolated change

---

## Benefits Summary

### After Implementation

1. **Better Separation of Concerns**
   - UI components only handle rendering
   - Business logic in dedicated services
   - Application logic clearly separated

2. **Improved Testability**
   - UI components can be tested with mock `UIActions`
   - Business logic can be tested independently
   - Rendering logic separated from application lifecycle

3. **Reduced Coupling**
   - UI components depend on interfaces, not concrete classes
   - Changes to `SearchController`/`SearchWorker` don't require UI changes
   - Follows Dependency Inversion Principle

4. **Easier Maintenance**
   - Clear boundaries between layers
   - Changes isolated to appropriate layers
   - Easier to understand codebase structure

---

## Alternative: Minimal Changes Approach

If the full refactoring is too large, a **minimal changes approach** can still improve separation:

1. **Add `UIActions` interface** - Decouple UI from application logic
2. **Extract `SearchResultsService`** - Move business logic out of UI
3. **Keep Application rendering methods** - Don't extract to `UIRenderer` (lower priority)

This provides **80% of the benefits** with **50% of the effort**.

---

## Conclusion

The current codebase has **good architectural foundations** with `application_logic` namespace and UI components in `namespace ui`. However, there are **coupling issues** where UI components directly depend on application logic classes and perform business logic operations.

The proposed solutions follow **SOLID principles** (especially **Dependency Inversion** and **Single Responsibility**) and will result in:

- ✅ Better separation of concerns
- ✅ Improved testability
- ✅ Reduced coupling
- ✅ Easier maintenance

**Recommendation:** Implement **Phase 1** and **Phase 2** first (interfaces and UI component migration), as these provide the most value with manageable risk. **Phase 3** and **Phase 4** can be done later if needed.
