# Section 3 (Manual Search) Improvement Proposal

## Current State Analysis

### Current Layout Issues

**Section 3: Manual Search** currently has several layout problems:

1. **Inconsistent Indentation**
   - Path Search: No indentation (primary)
   - Extensions and Filename: Indented 20px (secondary)
   - Search Controls: No indentation
   - **Problem:** The indentation suggests hierarchy, but it's unclear why Extensions/Filename are "secondary"

2. **Crowded Search Controls Row**
   - All controls on one line: `[Search] [Folders Only] [Case-Insensitive] [Auto-refresh] [Clear All] [Help]`
   - **Problem:** Too many items on one line, hard to scan, may wrap on smaller screens

3. **Inconsistent Label Formatting**
   - Path Search: "Path Search:" (with colon)
   - Extensions: "Extensions:" (with colon)
   - Filename: "Item name contains:" (longer label, with colon)
   - **Problem:** Inconsistent naming - "Item name contains" is verbose compared to others

4. **Mixed Input Types on Same Line**
   - Extensions and Filename are on the same line but serve different purposes
   - **Problem:** May feel cramped, especially with Filename's long label and help buttons

5. **Unclear Visual Hierarchy**
   - Path Search is clearly primary (full width, no indent)
   - Extensions/Filename are secondary (indented, smaller)
   - But Search Controls are back to full width
   - **Problem:** The visual flow is: primary → secondary → primary again (confusing)

## Proposed Improvements

### Option 1: Clear Two-Tier Layout (Recommended)

**Principle:** Separate primary search from refinement filters, with clear visual grouping.

```
┌─────────────────────────────────────────────────────────────┐
│ SECTION 3: Manual Search                                    │
│ ─────────────────────────────────────────────────────────── │
│                                                              │
│ PRIMARY SEARCH                                               │
│ Path Search: [________________________________] (?) [🔧]    │
│                                                              │
│ REFINEMENT FILTERS                                           │
│ Extensions: [________]                                       │
│ Filename:   [________] (?) [🔧]                             │
│                                                              │
│ SEARCH CONTROLS                                              │
│ [Search]                                                     │
│ [Folders Only] [Case-Insensitive] [Auto-refresh]            │
│ [Clear All] [Help]                                           │
└─────────────────────────────────────────────────────────────┘
```

**Changes:**
1. **Remove indentation** - Use section labels instead ("PRIMARY SEARCH", "REFINEMENT FILTERS", "SEARCH CONTROLS")
2. **Stack Extensions and Filename vertically** - Each on its own line for clarity
3. **Break Search Controls into logical groups:**
   - Search button on its own line (primary action)
   - Options on second line (Folders Only, Case-Insensitive, Auto-refresh)
   - Actions on third line (Clear All, Help)
4. **Consistent label format** - Use "Filename:" instead of "Item name contains:"

**Pros:**
- Clear visual hierarchy with section labels
- More readable (no cramped horizontal layout)
- Better for smaller screens (less wrapping)
- Consistent formatting

**Cons:**
- Takes more vertical space
- Extensions and Filename no longer on same line (but this may be better)

### Option 2: Compact Horizontal Layout

**Principle:** Keep compact but improve organization.

```
┌─────────────────────────────────────────────────────────────┐
│ PRIMARY SEARCH: [________________________] (?) [🔧]       │
│                                                              │
│ FILTERS: Extensions: [____]  Filename: [____] (?) [🔧]     │
│                                                              │
│ [Search] [Folders Only] [Case-Insensitive] [Auto-refresh]   │
│ [Clear All] [Help]                                           │
└─────────────────────────────────────────────────────────────┘
```

**Changes:**
1. **Add section labels** - "PRIMARY SEARCH", "FILTERS", implicit "CONTROLS"
2. **Keep Extensions and Filename on same line** - But with consistent "Filename:" label
3. **Group Search Controls** - Two lines instead of one

**Pros:**
- More compact (less vertical space)
- Still clear organization
- Extensions and Filename remain on same line

**Cons:**
- Still somewhat crowded
- Less clear hierarchy than Option 1

### Option 3: Tabbed/Grouped Layout

**Principle:** Use visual grouping with subtle backgrounds or borders.

```
┌─────────────────────────────────────────────────────────────┐
│ SECTION 3: Manual Search                                    │
│ ─────────────────────────────────────────────────────────── │
│                                                              │
│ ┌─ Search Pattern ────────────────────────────────────────┐ │
│ │ Path Search: [________________________________] (?) [🔧]│ │
│ └─────────────────────────────────────────────────────────┘ │
│                                                              │
│ ┌─ Filters ───────────────────────────────────────────────┐ │
│ │ Extensions: [________]  Filename: [________] (?) [🔧]  │ │
│ └─────────────────────────────────────────────────────────┘ │
│                                                              │
│ ┌─ Options ──────────────────────────────────────────────┐ │
│ │ [Search] [Folders Only] [Case-Insensitive] [Auto-refresh]│ │
│ │ [Clear All] [Help]                                       │ │
│ └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

**Changes:**
1. **Visual boxes** - Subtle borders/backgrounds to group related items
2. **Clear section names** - "Search Pattern", "Filters", "Options"
3. **Logical grouping** - Related controls grouped together

**Pros:**
- Very clear visual separation
- Professional appearance
- Easy to scan

**Cons:**
- Requires more UI styling (may be heavier)
- Takes more space

## Recommendation: Option 1 (Clear Two-Tier Layout)

**Rationale:**
- **Clarity over compactness** - Manual search is for power users who need clarity
- **Better readability** - Each input on its own line is easier to read
- **Consistent formatting** - All labels use same format ("Path Search:", "Extensions:", "Filename:")
- **Logical grouping** - Section labels make hierarchy clear
- **Scalable** - Easy to add more filters in the future

### Implementation Details

**File: `ui/SearchInputs.cpp`**

```cpp
void SearchInputs::Render(...) {
  // PRIMARY SEARCH
  ImGui::Text("PRIMARY SEARCH");
  ImGui::Spacing();
  
  if (SearchInputs::RenderInputFieldWithEnter("Path Search", "##path", 
      state.pathInput.Data(), SearchInputField::MaxLength(), 
      full_width, state, true, true)) {
    enter_pressed = true;
  }
  
  ImGui::Spacing();
  
  // REFINEMENT FILTERS
  ImGui::Text("REFINEMENT FILTERS");
  ImGui::Spacing();
  
  // Extensions on its own line
  if (SearchInputs::RenderInputFieldWithEnter("Extensions", "##extensions",
      state.extensionInput.Data(), SearchInputField::MaxLength(),
      300.0f, state)) {
    enter_pressed = true;
  }
  
  ImGui::Spacing();
  
  // Filename on its own line (renamed from "Item name contains")
  if (SearchInputs::RenderInputFieldWithEnter("Filename", "##filename",
      state.filenameInput.Data(), SearchInputField::MaxLength(),
      300.0f, state, true, true)) {
    enter_pressed = true;
  }
  
  ImGui::Spacing();
  
  // Handle Enter key press
  if (enter_pressed && !is_index_building && search_controller && search_worker) {
    search_controller->TriggerManualSearch(state, *search_worker);
  }
}
```

**File: `ui/SearchControls.cpp`**

```cpp
void SearchControls::Render(...) {
  ImGui::Spacing();
  ImGui::Text("SEARCH CONTROLS");
  ImGui::Spacing();
  
  // Primary action: Search button on its own line
  if (is_index_building) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Search") ||
      (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
    if (!is_index_building && search_controller && search_worker) {
      search_controller->TriggerManualSearch(state, *search_worker);
    }
  }
  if (is_index_building) {
    ImGui::EndDisabled();
  }
  
  ImGui::Spacing();
  
  // Options: grouped together
  ImGui::Checkbox("Folders Only", &state.foldersOnly);
  ImGui::SameLine();
  
  bool case_insensitive = !state.caseSensitive;
  ImGui::Checkbox("Case-Insensitive", &case_insensitive);
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("When checked (default), search ignores case.\nWhen "
                "unchecked, search is case-sensitive.");
    ImGui::EndTooltip();
  }
  state.caseSensitive = !case_insensitive;
  
  ImGui::SameLine();
  if (is_index_building) {
    ImGui::BeginDisabled();
  }
  ImGui::Checkbox("Auto-refresh", &state.autoRefresh);
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::Text("Re-run search automatically when files are added/deleted.\nKeeps "
                "results up-to-date with file system changes.");
    ImGui::EndTooltip();
  }
  if (is_index_building) {
    ImGui::EndDisabled();
  }
  
  ImGui::Spacing();
  
  // Actions: grouped together
  if (ImGui::Button("Clear All")) {
    state.ClearInputs();
    state.timeFilter = TimeFilter::None;
  }
  
  ImGui::SameLine();
  if (ImGui::Button("Help")) {
    ImGui::OpenPopup("KeyboardShortcutsPopup");
  }
}
```

## Alternative: Minimal Changes (Quick Win)

If Option 1 is too much change, here's a minimal improvement:

1. **Remove indentation** from Extensions/Filename (they're not really "secondary")
2. **Rename "Item name contains" to "Filename"** (consistent with other labels)
3. **Break Search Controls into 2 lines:**
   - Line 1: `[Search] [Folders Only] [Case-Insensitive] [Auto-refresh]`
   - Line 2: `[Clear All] [Help]`

**Pros:**
- Minimal code changes
- Still improves readability
- Quick to implement

**Cons:**
- Less clear hierarchy than Option 1
- Still somewhat crowded

## Summary

**Recommended:** Option 1 (Clear Two-Tier Layout)
- Best clarity and readability
- Consistent formatting
- Clear visual hierarchy
- Better for power users

**Fallback:** Minimal Changes
- Quick to implement
- Still improves current state
- Can be enhanced later





