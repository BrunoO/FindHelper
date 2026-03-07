# C-Style Array Replacement Analysis

**Date:** 2026-01-15  
**Purpose:** Identify C-style arrays marked with `// NOSONAR` that can be safely replaced with `std::array`

## Summary

**Total arrays analyzed:** 9  
**Safely replaceable:** 8  
**Requires function signature changes:** 2  
**Cannot replace (C API requirement):** 1

---

## Analysis Results

### ✅ **SAFELY REPLACEABLE** (8 arrays)

#### 1. Pattern Matching Arrays (PathPatternMatcher)

**Location:** `src/path/PathPatternMatcher.cpp`

##### 1.1 `bool bitmap[256]` in `CharClass` struct
```cpp
// Current (line 69)
bool bitmap[256] = {};  // NOSONAR - Fixed-size bitmap for performance-critical pattern matching (hot path)
```

**Replacement:**
```cpp
std::array<bool, 256> bitmap = {};  // Fixed-size bitmap for performance-critical pattern matching (hot path)
```

**Usage:** Accessed via `bitmap[uc]` - direct replacement works  
**Performance Impact:** Zero (identical memory layout and access)  
**Effort:** Low (5 minutes)

---

##### 1.2 `Atom atoms[64]` in `Pattern` struct
```cpp
// Current (line 108)
Atom atoms[64];  // NOSONAR - Fixed-size array for performance-critical pattern matching (hot path)
```

**Replacement:**
```cpp
std::array<Atom, 64> atoms;  // Fixed-size array for performance-critical pattern matching (hot path)
```

**Usage:** Accessed via `pattern.atoms[atom_index]` - direct replacement works  
**Performance Impact:** Zero (identical memory layout and access)  
**Effort:** Low (5 minutes)

---

##### 1.3 `simple_tokens_storage[kMaxPatternTokens * kSimpleTokenSize]` in `PathPatternMatcher` class
```cpp
// Current (PathPatternMatcher.h, line 51-52)
alignas(8) std::uint8_t
    simple_tokens_storage[kMaxPatternTokens * kSimpleTokenSize]{};  // NOSONAR - Fixed-size array for performance-critical pattern matching (hot path)
```

**Replacement:**
```cpp
alignas(8) std::array<std::uint8_t, kMaxPatternTokens * kSimpleTokenSize> simple_tokens_storage{};  // Fixed-size array for performance-critical pattern matching (hot path)
```

**Usage:** Byte array for token storage - direct replacement works  
**Performance Impact:** Zero (identical memory layout, alignment preserved)  
**Effort:** Low (5 minutes)

---

#### 2. Thread-Local UI Buffers (ResultsTable)

**Location:** `src/ui/ResultsTable.cpp`

##### 2.1 `thread_local char filename_buffer[512]`
```cpp
// Current (line 465)
static thread_local char filename_buffer[512];  // NOSONAR - Thread-local buffer for performance-critical UI rendering (hot path)
```

**Replacement:**
```cpp
static thread_local std::array<char, 512> filename_buffer;  // Thread-local buffer for performance-critical UI rendering (hot path)
```

**Usage Changes Required:**
- Replace `IM_ARRAYSIZE(filename_buffer)` with `filename_buffer.size()`
- Replace `filename_buffer` with `filename_buffer.data()` when passing to `std::memcpy` or ImGui
- Access remains the same: `filename_buffer[i]`

**Example:**
```cpp
// Before
if (result.filename.size() < IM_ARRAYSIZE(filename_buffer)) {
  std::memcpy(filename_buffer, result.filename.data(), result.filename.size());
  filename_buffer[result.filename.size()] = '\0';
  filename_cstr = filename_buffer;
}

// After
if (result.filename.size() < filename_buffer.size()) {
  std::memcpy(filename_buffer.data(), result.filename.data(), result.filename.size());
  filename_buffer[result.filename.size()] = '\0';
  filename_cstr = filename_buffer.data();
}
```

**Performance Impact:** Zero (identical memory layout and access)  
**Effort:** Medium (15 minutes - need to update all usages)

---

##### 2.2 `thread_local char ext_buffer[64]`
```cpp
// Current (line 521)
static thread_local char ext_buffer[64];  // NOSONAR - Thread-local buffer for performance-critical UI rendering (hot path)
```

**Replacement:**
```cpp
static thread_local std::array<char, 64> ext_buffer;  // Thread-local buffer for performance-critical UI rendering (hot path)
```

**Usage Changes Required:** Same as filename_buffer (replace `IM_ARRAYSIZE` with `.size()`, use `.data()` for pointers)  
**Performance Impact:** Zero  
**Effort:** Medium (15 minutes)

---

#### 3. ImGui Input Buffers (Popups)

**Location:** `src/ui/Popups.cpp`

##### 3.1 `char param1[256]`, `param2[256]`, `test_text[256]` in `RegexGeneratorState` struct
```cpp
// Current (lines 226-228)
char param1[256];  // NOSONAR - ImGui::InputText requires char* buffer (performance-critical, called every frame)
char param2[256];  // NOSONAR - ImGui::InputText requires char* buffer (performance-critical, called every frame)
char test_text[256];  // NOSONAR - ImGui::InputText requires char* buffer (performance-critical, called every frame)
```

**Replacement:**
```cpp
std::array<char, 256> param1 = {};  // ImGui::InputText requires char* buffer (performance-critical, called every frame)
std::array<char, 256> param2 = {};  // ImGui::InputText requires char* buffer (performance-critical, called every frame)
std::array<char, 256> test_text = {};  // ImGui::InputText requires char* buffer (performance-critical, called every frame)
```

**Usage Changes Required:**
- Replace `IM_ARRAYSIZE(state.param1)` with `state.param1.size()`
- Replace `state.param1` with `state.param1.data()` in `ImGui::InputText` calls
- Constructor initialization: `param1[0] = '\0'` becomes `param1[0] = '\0'` (same)

**Example:**
```cpp
// Before
ImGui::InputText(param_id, state.param1, IM_ARRAYSIZE(state.param1));

// After
ImGui::InputText(param_id, state.param1.data(), state.param1.size());
```

**Performance Impact:** Zero (ImGui accepts `char*` and size, `.data()` provides pointer)  
**Effort:** Medium (20 minutes - need to update all ImGui calls)

---

##### 3.2 `static char save_search_name[128]`
```cpp
// Current (line 637)
static char save_search_name[128] = "";  // NOSONAR - ImGui::InputText requires char* buffer (performance-critical, called every frame)
```

**Replacement:**
```cpp
static std::array<char, 128> save_search_name = {};  // ImGui::InputText requires char* buffer (performance-critical, called every frame)
```

**Usage Changes Required:**
- Replace `IM_ARRAYSIZE(save_search_name)` with `save_search_name.size()`
- Replace `save_search_name` with `save_search_name.data()` in `ImGui::InputText` calls

**Performance Impact:** Zero  
**Effort:** Low (10 minutes)

---

#### 4. ImGui Input Buffer (GuiState)

**Location:** `src/gui/GuiState.h`

##### 4.1 `char gemini_description_input_[512]`
```cpp
// Current (line 115)
char gemini_description_input_[512] = "";  // NOSONAR - ImGui::InputTextMultiline requires char* buffer (performance-critical, called every frame)
```

**Replacement:**
```cpp
std::array<char, 512> gemini_description_input_ = {};  // ImGui::InputTextMultiline requires char* buffer (performance-critical, called every frame)
```

**Usage Changes Required:**
- Find `ImGui::InputTextMultiline` call and update to use `.data()` and `.size()`
- Check: `src/ui/SearchInputs.cpp:453` - `ImGui::InputTextMultiline("##gemini_description", state.gemini_description_input_, ...)`

**Performance Impact:** Zero  
**Effort:** Low (10 minutes)

---

### ⚠️ **REQUIRES FUNCTION SIGNATURE CHANGES** (2 arrays)

#### 5. Path Component Arrays (PathBuilder)

**Location:** `src/path/PathBuilder.cpp`

##### 5.1 `const std::string* components[kMaxPathDepth]` (2 instances)
```cpp
// Current (lines 69, 78)
const std::string* components[kMaxPathDepth];  // NOSONAR - Fixed-size array for performance-critical path building
```

**Replacement:**
```cpp
std::array<const std::string*, kMaxPathDepth> components;  // Fixed-size array for performance-critical path building
```

**Function Signature Changes Required:**

**PathBuilder.h (lines 66, 69):**
```cpp
// Before
static int CollectPathComponents(uint64_t parent_id, std::string_view name,
                                  const FileIndexStorage& storage,
                                  const std::string* components[kMaxPathDepth]);

static std::string BuildPathFromComponents(const std::string* components[kMaxPathDepth],
                                            int componentCount);

// After
static int CollectPathComponents(uint64_t parent_id, std::string_view name,
                                  const FileIndexStorage& storage,
                                  std::array<const std::string*, kMaxPathDepth>& components);

static std::string BuildPathFromComponents(const std::array<const std::string*, kMaxPathDepth>& components,
                                            int componentCount);
```

**PathBuilder.cpp Changes:**
- Function parameter: `const std::string* components[kMaxPathDepth]` → `std::array<const std::string*, kMaxPathDepth>& components`
- Array access: `components[i]` remains the same (std::array supports indexing)
- Array assignment: `components[count++] = &name_storage;` remains the same

**Performance Impact:** Zero (identical memory layout, pass by reference avoids copy)  
**Effort:** Medium (30 minutes - need to update function signatures and verify all call sites)

---

### ❌ **CANNOT REPLACE** (1 array - C API requirement)

#### 6. CPUID Array (CpuFeatures)

**Location:** `src/utils/CpuFeatures.cpp`

##### 6.1 `unsigned int info[4]` in Linux/macOS `cpuid` functions
```cpp
// Current (lines 63, 76)
static void cpuid(unsigned int info[4], unsigned int function_id);
static void cpuid_count(unsigned int info[4], unsigned int function_id, unsigned int subleaf);
```

**Why Cannot Replace:**
- These functions use inline assembly that directly modifies array elements via output constraints
- The assembly syntax `"=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3])` requires direct array indexing
- While `std::array` could work, the inline assembly pattern is specifically designed for C-style arrays
- **Note:** Windows version already uses `std::array<int, 4>` (line 37), but it uses `__cpuid` intrinsic, not inline assembly

**Recommendation:** Keep as-is for Linux/macOS, or refactor to use `std::array` with `.data()` if assembly constraints support it (needs testing)

**Status:** Keep C-style array for now (C API/inline assembly requirement)

---

## Replacement Priority

### High Priority (Easy Wins)
1. ✅ Pattern matching arrays (3 arrays) - Direct replacements, no API changes
2. ✅ ImGui buffers in structs (4 arrays) - Simple `.data()` and `.size()` changes

### Medium Priority (Requires Updates)
3. ⚠️ Thread-local UI buffers (2 arrays) - Need to update `IM_ARRAYSIZE` usage
4. ⚠️ Path component arrays (2 arrays) - Need function signature changes

### Low Priority (Keep As-Is)
5. ❌ CPUID array (1 array) - C API/inline assembly requirement

---

## Implementation Plan

### Phase 1: Pattern Matching Arrays (15 minutes)
- [ ] Replace `bool bitmap[256]` with `std::array<bool, 256>`
- [ ] Replace `Atom atoms[64]` with `std::array<Atom, 64>`
- [ ] Replace `simple_tokens_storage[...]` with `std::array<std::uint8_t, ...>`
- [ ] Verify compilation and tests

### Phase 2: ImGui Struct Buffers (30 minutes)
- [ ] Replace `char param1[256]`, `param2[256]`, `test_text[256]` in `RegexGeneratorState`
- [ ] Update all `ImGui::InputText` calls to use `.data()` and `.size()`
- [ ] Replace `static char save_search_name[128]`
- [ ] Replace `char gemini_description_input_[512]` in `GuiState`
- [ ] Update `ImGui::InputTextMultiline` call
- [ ] Verify compilation and UI functionality

### Phase 3: Thread-Local Buffers (30 minutes)
- [ ] Replace `thread_local char filename_buffer[512]`
- [ ] Replace `thread_local char ext_buffer[64]`
- [ ] Update all `IM_ARRAYSIZE` usages to `.size()`
- [ ] Update all pointer usages to `.data()`
- [ ] Verify compilation and UI rendering

### Phase 4: Path Builder Arrays (30 minutes)
- [ ] Update function signatures in `PathBuilder.h`
- [ ] Update function implementations in `PathBuilder.cpp`
- [ ] Replace local array declarations
- [ ] Verify all call sites still work
- [ ] Run path building tests

---

## Testing Requirements

After each phase:
1. **Compilation:** Verify code compiles on all platforms (Windows, macOS, Linux)
2. **Unit Tests:** Run relevant test suites
3. **Functional Tests:**
   - Pattern matching tests (Phase 1)
   - UI input tests (Phase 2, 3)
   - Path building tests (Phase 4)
4. **Performance:** Verify no performance regression (should be identical)

---

## Benefits Summary

- **Type Safety:** Arrays know their size at compile time
- **STL Compatibility:** Can use STL algorithms (`std::fill`, `std::copy`, etc.)
- **Bounds Checking:** Optional with `.at()` method (not used in hot paths)
- **Code Clarity:** Explicit size in type signature
- **Maintainability:** Easier to refactor and understand
- **Performance:** Zero overhead (identical to C-style arrays)

---

## Notes

- All replacements maintain **identical performance characteristics**
- `std::array` has the same memory layout as C-style arrays (guaranteed by C++ standard)
- `.data()` provides pointer for C API compatibility
- `.size()` provides size information (replaces `IM_ARRAYSIZE` macro)
- Thread-local storage works identically with `std::array`
