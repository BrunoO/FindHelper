# Test Coverage Analysis: GuiState Input Fields Refactoring

## Overview

This document analyzes test coverage for refactoring `GuiState` input fields from raw `char[256]` arrays to an encapsulated `SearchInputField` class, addressing the "Primitive Obsession" code smell identified in the architectural review.

## Current State

### GuiState Input Fields
- `char extensionInput[256] = "";`
- `char filenameInput[256] = "";`
- `char pathInput[256] = "";`

### Usage Patterns Identified

1. **ImGui Integration** (Primary usage)
   - `ImGui::InputText(id, buffer, buffer_size, flags)` - requires `char*` and size
   - `IM_ARRAYSIZE(buffer)` - used to get buffer size
   - Location: `ui/SearchInputs.cpp`, `ui/FilterPanel.cpp`

2. **String Copying**
   - `strcpy_safe(buffer, sizeof(buffer), source)` - safe string copy
   - Location: `ui/FilterPanel.cpp`, `ui/SearchInputs.cpp`, `TimeFilterUtils.cpp`

3. **Direct Assignment**
   - `state.filenameInput[0] = '\0'` - clear input
   - Location: `ui/FilterPanel.cpp`, `GuiState.cpp`

4. **String Length Checks**
   - `strlen(state.extensionInput) > 0` - check if empty
   - Location: `ui/FilterPanel.cpp`

5. **Conversion to std::string**
   - `params.filenameInput = state.filenameInput` - implicit conversion
   - Location: `SearchController::BuildSearchParams()`

6. **Saved Searches**
   - `saved.path = state.pathInput` - assignment to std::string
   - `strcpy_safe(state.pathInput, sizeof(state.pathInput), saved.path.c_str())` - restore
   - Location: `ui/Popups.cpp`, `TimeFilterUtils.cpp`

## Test Coverage Gaps

### Current Test Status
- ❌ **No unit tests exist for GuiState**
- ❌ **No tests for input field operations**
- ❌ **No tests for SearchController::BuildSearchParams()**
- ❌ **No tests for input field conversions**

### Required Test Coverage

#### 1. Basic Input Field Operations
- [ ] Default construction (empty string)
- [ ] Clear operation (`Clear()`)
- [ ] IsEmpty() check
- [ ] SetValue() with various string lengths
- [ ] SetValue() with truncation (strings > 256 chars)
- [ ] AsString() conversion
- [ ] AsView() conversion

#### 2. ImGui Compatibility
- [ ] `Data()` returns non-const `char*` for ImGui::InputText
- [ ] `Data()` returns const `char*` for read-only access
- [ ] `MaxLength()` returns correct size (256)
- [ ] Buffer is null-terminated after operations
- [ ] Buffer size matches IM_ARRAYSIZE expectations

#### 3. String Operations
- [ ] strcpy_safe() equivalent (SetValue with truncation)
- [ ] strlen() equivalent (IsEmpty check)
- [ ] Direct assignment to std::string (implicit conversion)
- [ ] Assignment from std::string (SetValue)

#### 4. SearchController Integration
- [ ] BuildSearchParams() correctly converts all three fields
- [ ] Empty fields handled correctly
- [ ] Full path search mode logic (when filename and extension empty)

#### 5. Saved Searches Integration
- [ ] Save search state (assignment to std::string)
- [ ] Restore search state (SetValue from std::string)
- [ ] Long paths truncated correctly

#### 6. FilterPanel Integration
- [ ] Quick filter buttons set values correctly
- [ ] Clear operations work correctly
- [ ] Active filter detection (IsEmpty checks)

#### 7. Edge Cases
- [ ] Maximum length strings (255 chars + null terminator)
- [ ] Strings exactly at max length
- [ ] Empty strings
- [ ] Strings with special characters
- [ ] Unicode characters (if supported)
- [ ] Null pointer safety (if applicable)

## Test Plan

### Phase 1: Unit Tests for SearchInputField Class

Create `tests/SearchInputFieldTests.cpp` with:

1. **Construction and Basic Operations**
   ```cpp
   TEST_CASE("SearchInputField default construction") {
     SearchInputField field;
     REQUIRE(field.IsEmpty());
     REQUIRE(field.AsString().empty());
     REQUIRE(field.MaxLength() == 256);
   }
   
   TEST_CASE("SearchInputField Clear") {
     SearchInputField field;
     field.SetValue("test");
     field.Clear();
     REQUIRE(field.IsEmpty());
   }
   ```

2. **SetValue and Truncation**
   ```cpp
   TEST_CASE("SearchInputField SetValue normal string") {
     SearchInputField field;
     field.SetValue("test");
     REQUIRE(field.AsString() == "test");
     REQUIRE(!field.IsEmpty());
   }
   
   TEST_CASE("SearchInputField SetValue truncation") {
     SearchInputField field;
     std::string long_string(300, 'a');
     field.SetValue(long_string);
     REQUIRE(field.AsString().length() == 255); // Max without null terminator
     REQUIRE(field.AsString()[254] == 'a');
   }
   ```

3. **ImGui Compatibility**
   ```cpp
   TEST_CASE("SearchInputField ImGui Data() access") {
     SearchInputField field;
     field.SetValue("test");
     char* data = field.Data();
     REQUIRE(std::strcmp(data, "test") == 0);
     REQUIRE(std::strlen(data) == 4);
   }
   
   TEST_CASE("SearchInputField IM_ARRAYSIZE compatibility") {
     SearchInputField field;
     REQUIRE(IM_ARRAYSIZE(field.Data()) == 256);
   }
   ```

4. **String Conversions**
   ```cpp
   TEST_CASE("SearchInputField AsString conversion") {
     SearchInputField field;
     field.SetValue("test");
     std::string str = field.AsString();
     REQUIRE(str == "test");
   }
   
   TEST_CASE("SearchInputField AsView conversion") {
     SearchInputField field;
     field.SetValue("test");
     std::string_view view = field.AsView();
     REQUIRE(view == "test");
   }
   ```

### Phase 2: Integration Tests for GuiState

Create `tests/GuiStateTests.cpp` with:

1. **GuiState with SearchInputField**
   ```cpp
   TEST_CASE("GuiState input fields default empty") {
     GuiState state;
     REQUIRE(state.extensionInput.IsEmpty());
     REQUIRE(state.filenameInput.IsEmpty());
     REQUIRE(state.pathInput.IsEmpty());
   }
   
   TEST_CASE("GuiState ClearInputs") {
     GuiState state;
     state.extensionInput.SetValue("txt");
     state.filenameInput.SetValue("test");
     state.pathInput.SetValue("/path");
     state.ClearInputs();
     REQUIRE(state.extensionInput.IsEmpty());
     REQUIRE(state.filenameInput.IsEmpty());
     REQUIRE(state.pathInput.IsEmpty());
   }
   ```

2. **SearchController Integration**
   ```cpp
   TEST_CASE("SearchController BuildSearchParams from GuiState") {
     GuiState state;
     state.filenameInput.SetValue("test");
     state.extensionInput.SetValue("txt");
     state.pathInput.SetValue("/path");
     state.foldersOnly = true;
     state.caseSensitive = true;
     
     SearchController controller;
     SearchParams params = controller.BuildSearchParams(state);
     
     REQUIRE(params.filenameInput == "test");
     REQUIRE(params.extensionInput == "txt");
     REQUIRE(params.pathInput == "/path");
     REQUIRE(params.foldersOnly == true);
     REQUIRE(params.caseSensitive == true);
   }
   ```

3. **Path and filename are independent**
   Path and filename fields are always separate: the main search bar matches filename only; the Path field matches the full path. There is no "move path to filename" mode. Tests should assert that `BuildCurrentSearchParams` / `BuildSearchParams` pass path and filename through as provided (e.g. pathInput → pathQuery, filenameInput → primary query).

### Phase 3: Edge Case Tests

1. **Maximum Length Handling**
   ```cpp
   TEST_CASE("SearchInputField maximum length string") {
     SearchInputField field;
     std::string max_string(255, 'a');
     field.SetValue(max_string);
     REQUIRE(field.AsString().length() == 255);
     REQUIRE(!field.IsEmpty());
   }
   ```

2. **Special Characters**
   ```cpp
   TEST_CASE("SearchInputField special characters") {
     SearchInputField field;
     field.SetValue("test/file\\path");
     REQUIRE(field.AsString() == "test/file\\path");
   }
   ```

## Implementation Strategy

### Step 1: Create SearchInputField Class
- Create `SearchInputField.h` with the class definition
- Implement all methods
- Ensure ImGui compatibility

### Step 2: Add Unit Tests
- Create `tests/SearchInputFieldTests.cpp`
- Cover all basic operations
- Cover edge cases

### Step 3: Update GuiState
- Replace `char[256]` with `SearchInputField`
- Update `ClearInputs()` method
- Ensure backward compatibility

### Step 4: Add Integration Tests
- Create `tests/GuiStateTests.cpp`
- Test SearchController integration
- Test FilterPanel integration (if possible)

### Step 5: Update All Usage Sites
- Update `ui/SearchInputs.cpp`
- Update `ui/FilterPanel.cpp`
- Update `SearchController.cpp`
- Update `TimeFilterUtils.cpp`
- Update `ui/Popups.cpp`

### Step 6: Verify Tests Pass
- Run all tests
- Fix any issues
- Ensure no regressions

## Success Criteria

✅ All unit tests pass  
✅ All integration tests pass  
✅ No compilation errors  
✅ No runtime errors  
✅ ImGui integration works correctly  
✅ Search functionality works correctly  
✅ Saved searches work correctly  
✅ All existing functionality preserved  

## Risk Assessment

### Low Risk
- Basic operations (SetValue, Clear, IsEmpty)
- String conversions
- ImGui compatibility (if implemented correctly)

### Medium Risk
- SearchController integration (conversion logic)
- Saved searches (state persistence)
- FilterPanel quick filters (string copying)

### High Risk
- ImGui::InputText integration (must maintain exact compatibility)
- Buffer size and null termination (critical for safety)
- Truncation behavior (must match current behavior)

## Notes

- ImGui requires `char*` buffers, so `Data()` method is essential
- `IM_ARRAYSIZE` macro must work with the new class
- String truncation must be safe (no buffer overflows)
- All existing code paths must be tested
- Consider adding assertions for debug builds

