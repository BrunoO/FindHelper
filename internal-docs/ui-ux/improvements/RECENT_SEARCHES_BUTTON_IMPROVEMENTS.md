# Recent Searches Button Improvements

## Problem Statement

The recent searches buttons are useful, but the button text becomes difficult to understand when there are long lists of extensions (e.g., `*.cpp,h,hpp,cc,cxx,mm,swift,kt,java,py,js,ts,tsx,jsx`). The current implementation:

1. Combines all extensions into a single string: `"*." + extensions`
2. Adds other fields (filename, path, folders) with `" + "` separators
3. Truncates the entire label if it exceeds button width

This results in buttons like:
- `*.cpp,h,hpp,cc,cxx,mm,swift,kt,java,py,js,ts,tsx,jsx + main.cpp + ...`
- `*.cpp,h,hpp,cc,cxx,mm,swift,kt,java,py,js,ts,tsx,jsx + folders`

## Current Implementation

**Location:** `src/ui/EmptyState.cpp` (lines 188-228)

**Current behavior:**
- Extensions are added as `"*." + recent.extensions` (line 197)
- All parts are combined with `" + "` separator (line 216)
- Label is truncated if too long (line 220)

## Improvement Options

### Option 1: Smart Extension Truncation with Count ⭐ **RECOMMENDED**

**Approach:** Show first 2-3 extensions, then show count of remaining extensions.

**Example:**
- Before: `*.cpp,h,hpp,cc,cxx,mm,swift,kt,java,py,js,ts,tsx,jsx`
- After: `*.cpp,h,hpp (+11 more)`

**Implementation:**
```cpp
// In RenderRecentSearches, replace extension handling:
if (!recent.extensions.empty()) {
  std::vector<std::string> ext_list;
  // Split extensions by comma
  std::string ext_str = recent.extensions;
  size_t pos = 0;
  while ((pos = ext_str.find(',')) != std::string::npos) {
    ext_list.push_back(ext_str.substr(0, pos));
    ext_str.erase(0, pos + 1);
  }
  if (!ext_str.empty()) {
    ext_list.push_back(ext_str);
  }
  
  if (ext_list.size() <= 3) {
    // Show all if 3 or fewer
    parts.push_back("*." + recent.extensions);
  } else {
    // Show first 2-3, then count
    std::string first_exts;
    for (size_t i = 0; i < 2 && i < ext_list.size(); ++i) {
      if (i > 0) first_exts += ",";
      first_exts += ext_list[i];
    }
    size_t remaining = ext_list.size() - 2;
    parts.push_back("*." + first_exts + " (+" + std::to_string(remaining) + " more)");
  }
}
```

**Pros:**
- ✅ Clear and concise
- ✅ Shows useful information (first extensions + count)
- ✅ Easy to understand at a glance
- ✅ Maintains button readability

**Cons:**
- ⚠️ Requires parsing extensions (minor complexity)
- ⚠️ Slightly more code

**Complexity:** Low-Medium

---

### Option 2: Tooltip with Full Details ⭐ **RECOMMENDED**

**Approach:** Keep button text concise, show full details in tooltip on hover.

**Example:**
- Button: `*.cpp,h,hpp...` (truncated)
- Tooltip: `Extensions: *.cpp, *.h, *.hpp, *.cc, *.cxx, *.mm, *.swift, *.kt, *.java, *.py, *.js, *.ts, *.tsx, *.jsx\nFilename: main.cpp\nPath: C:\Users\...`

**Implementation:**
```cpp
// After button creation, add tooltip:
if (ImGui::IsItemHovered()) {
  ImGui::BeginTooltip();
  
  // Build full description
  std::string full_desc;
  if (!recent.extensions.empty()) {
    // Format extensions nicely
    std::vector<std::string> ext_list;
    // ... split extensions ...
    full_desc += "Extensions: ";
    for (size_t i = 0; i < ext_list.size(); ++i) {
      if (i > 0) full_desc += ", ";
      full_desc += "*." + ext_list[i];
    }
    full_desc += "\n";
  }
  if (!recent.filename.empty()) {
    full_desc += "Filename: " + recent.filename + "\n";
  }
  if (!recent.path.empty()) {
    full_desc += "Path: " + recent.path + "\n";
  }
  if (recent.foldersOnly) {
    full_desc += "Folders only\n";
  }
  
  ImGui::TextUnformatted(full_desc.c_str());
  ImGui::EndTooltip();
}
```

**Pros:**
- ✅ Button text stays short and readable
- ✅ Full details available on demand
- ✅ No information loss
- ✅ Follows existing tooltip patterns in codebase

**Cons:**
- ⚠️ Requires hover to see full details
- ⚠️ Slightly more code

**Complexity:** Low

---

### Option 3: Prioritize Display Order

**Approach:** Show most important/unique information first, extensions last.

**Example:**
- Before: `*.cpp,h,hpp,cc,cxx,mm,swift,kt,java,py,js,ts,tsx,jsx + main.cpp`
- After: `main.cpp + *.cpp,h,hpp...` (filename first, extensions truncated)

**Implementation:**
```cpp
// Reorder parts: filename/path first, extensions last
std::vector<std::string> parts;

// Add filename first (most specific)
if (!recent.filename.empty()) {
  parts.push_back(recent.filename);
}

// Add path second
if (!recent.path.empty()) {
  parts.push_back(TruncatePathDisplay(recent.path));
}

// Add folders-only indicator
if (recent.foldersOnly) {
  parts.push_back("folders");
}

// Add extensions last (most likely to be long)
if (!recent.extensions.empty()) {
  // Truncate extensions more aggressively
  std::string ext_display = "*." + recent.extensions;
  if (ext_display.length() > 30) {
    ext_display = ext_display.substr(0, 27) + "...";
  }
  parts.push_back(ext_display);
}
```

**Pros:**
- ✅ Shows most important info first
- ✅ Simple change
- ✅ Better for searches with filenames/paths

**Cons:**
- ⚠️ Still doesn't solve long extension lists
- ⚠️ Less useful if only extensions are set

**Complexity:** Low

---

### Option 4: Abbreviate Extensions with Smart Formatting

**Approach:** Format extensions more compactly (e.g., `*.{cpp,h,hpp,...}` or `*.cpp|h|hpp|...`).

**Example:**
- Before: `*.cpp,h,hpp,cc,cxx,mm,swift,kt,java,py,js,ts,tsx,jsx`
- After: `*.{cpp,h,hpp,...}` or `*.cpp|h|hpp|...` (more compact)

**Implementation:**
```cpp
if (!recent.extensions.empty()) {
  std::string ext_display = "*." + recent.extensions;
  // Replace commas with more compact separator
  // Or use brace notation for first few
  if (ext_display.length() > 25) {
    // Use compact format: *.{cpp,h,hpp,...}
    size_t first_comma = ext_display.find(',');
    if (first_comma != std::string::npos) {
      std::string first_ext = ext_display.substr(0, first_comma);
      size_t comma_count = std::count(ext_display.begin(), ext_display.end(), ',');
      ext_display = first_ext + " (+" + std::to_string(comma_count) + " more)";
    }
  }
  parts.push_back(ext_display);
}
```

**Pros:**
- ✅ More compact display
- ✅ Still shows extension info

**Cons:**
- ⚠️ Less intuitive than current format
- ⚠️ May confuse users
- ⚠️ Doesn't solve readability issue completely

**Complexity:** Low-Medium

---

### Option 5: Two-Line Button Text (Multi-line)

**Approach:** Allow buttons to display text on multiple lines.

**Example:**
```
┌─────────────────────────┐
│ *.cpp,h,hpp,cc,cxx,mm,  │
│ swift,kt,java,py,js,ts  │
└─────────────────────────┘
```

**Implementation:**
- Use `ImGui::Button` with explicit size
- Use `ImGui::CalcTextSize` with wrapping
- Render text manually with `ImGui::TextWrapped` inside button

**Pros:**
- ✅ Shows more information
- ✅ No truncation needed

**Cons:**
- ⚠️ Buttons take more vertical space
- ⚠️ More complex layout
- ⚠️ May look cluttered
- ⚠️ Requires custom button rendering

**Complexity:** High

---

### Option 6: Icon-Based or Compact Mode

**Approach:** Use icons or compact symbols for common extension types, show count.

**Example:**
- `📄 13 types` (with tooltip showing full list)
- `🔍 Code files (13)` (categorized)

**Implementation:**
- Categorize extensions (code, images, documents, etc.)
- Show category icon + count
- Full list in tooltip

**Pros:**
- ✅ Very compact
- ✅ Visual and intuitive

**Cons:**
- ⚠️ Requires icon font or image assets
- ⚠️ More complex categorization logic
- ⚠️ May be less informative at a glance

**Complexity:** High

---

## Recommended Approach: Combination

**Best Solution:** Combine **Option 1** (Smart Extension Truncation) + **Option 2** (Tooltip)

### Why This Combination Works Best:

1. **Option 1** makes buttons readable at a glance:
   - Shows first 2-3 extensions (most common/important)
   - Shows count of remaining extensions
   - Example: `*.cpp,h,hpp (+11 more)`

2. **Option 2** provides full details on demand:
   - Hover shows complete extension list
   - Shows all search parameters (filename, path, filters)
   - No information loss

3. **Option 3** (prioritize display) can be added:
   - Show filename/path before extensions if present
   - Makes buttons even more informative

### Implementation Priority:

1. **Phase 1:** Add tooltips (Option 2) - Quick win, immediate improvement
2. **Phase 2:** Smart extension truncation (Option 1) - Better button text
3. **Phase 3:** Prioritize display order (Option 3) - Polish

## Implementation Details

### Helper Function for Extension Formatting

```cpp
/**
 * @brief Format extensions for display with smart truncation
 * 
 * If there are 3 or fewer extensions, shows all.
 * If there are more, shows first 2-3 and count of remaining.
 * 
 * @param extensions Comma-separated extension string (e.g., "cpp,h,hpp")
 * @return Formatted string (e.g., "*.cpp,h,hpp" or "*.cpp,h (+11 more)")
 */
static std::string FormatExtensionsForDisplay(const std::string& extensions) {
  if (extensions.empty()) {
    return "";
  }
  
  // Split extensions
  std::vector<std::string> ext_list;
  std::string ext_str = extensions;
  size_t pos = 0;
  while ((pos = ext_str.find(',')) != std::string::npos) {
    std::string ext = ext_str.substr(0, pos);
    // Trim whitespace
    ext.erase(0, ext.find_first_not_of(" \t"));
    ext.erase(ext.find_last_not_of(" \t") + 1);
    if (!ext.empty()) {
      ext_list.push_back(ext);
    }
    ext_str.erase(0, pos + 1);
  }
  // Add last extension
  if (!ext_str.empty()) {
    ext_str.erase(0, ext_str.find_first_not_of(" \t"));
    ext_str.erase(ext_str.find_last_not_of(" \t") + 1);
    if (!ext_str.empty()) {
      ext_list.push_back(ext_str);
    }
  }
  
  if (ext_list.empty()) {
    return "";
  }
  
  // Show all if 3 or fewer
  if (ext_list.size() <= 3) {
    return "*." + extensions;
  }
  
  // Show first 2, then count
  std::string result = "*." + ext_list[0];
  for (size_t i = 1; i < 2 && i < ext_list.size(); ++i) {
    result += "," + ext_list[i];
  }
  size_t remaining = ext_list.size() - 2;
  result += " (+" + std::to_string(remaining) + " more)";
  
  return result;
}
```

### Tooltip Helper Function

```cpp
/**
 * @brief Build full tooltip text for a recent search
 * 
 * @param recent The saved search to describe
 * @return Formatted tooltip text
 */
static std::string BuildRecentSearchTooltip(const SavedSearch& recent) {
  std::string tooltip;
  
  if (!recent.extensions.empty()) {
    // Format extensions nicely: *.cpp, *.h, *.hpp
    std::vector<std::string> ext_list;
    // ... split extensions (same logic as FormatExtensionsForDisplay) ...
    tooltip += "Extensions: ";
    for (size_t i = 0; i < ext_list.size(); ++i) {
      if (i > 0) tooltip += ", ";
      tooltip += "*." + ext_list[i];
    }
    tooltip += "\n";
  }
  
  if (!recent.filename.empty()) {
    tooltip += "Filename: " + recent.filename + "\n";
  }
  
  if (!recent.path.empty()) {
    tooltip += "Path: " + recent.path + "\n";
  }
  
  if (recent.foldersOnly) {
    tooltip += "Folders only\n";
  }
  
  if (recent.caseSensitive) {
    tooltip += "Case sensitive\n";
  }
  
  if (recent.timeFilter != "None") {
    tooltip += "Time filter: " + recent.timeFilter + "\n";
  }
  
  if (recent.sizeFilter != "None") {
    tooltip += "Size filter: " + recent.sizeFilter + "\n";
  }
  
  return tooltip;
}
```

## Testing Considerations

1. **Test with various extension counts:**
   - 1-3 extensions (should show all)
   - 4-10 extensions (should show first 2 + count)
   - 10+ extensions (should show first 2 + count)

2. **Test with different combinations:**
   - Extensions only
   - Extensions + filename
   - Extensions + path
   - Extensions + filename + path
   - All fields populated

3. **Test tooltip:**
   - Verify full information is shown
   - Check formatting is readable
   - Ensure no truncation in tooltip

4. **Test button layout:**
   - Verify buttons still wrap correctly
   - Check button widths are reasonable
   - Ensure buttons are still clickable

## Related Files

- `src/ui/EmptyState.cpp` - Main implementation
- `src/core/Settings.h` - `SavedSearch` struct definition
- `src/filters/TimeFilterUtils.cpp` - `ApplySavedSearchToGuiState()` helper

## References

- Current implementation: `src/ui/EmptyState.cpp:188-228`
- Tooltip examples: `src/ui/SearchInputs.cpp:321-352`
- Extension storage: `src/core/Settings.h:10` (`SavedSearch::extensions`)
