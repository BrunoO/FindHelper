# C++17 init-statement examples

Extended examples for the **C++17 init-statement pattern** (variables declared in `if` conditions). For the rule summary, when to apply, and when not to apply, see **AGENTS.md** § C++17 Init-Statement Pattern.

## Good: variable only used inside the if

```cpp
// Single use in if statement
if (size_t pool_thread_count = thread_pool.GetThreadCount(); pool_thread_count == 0) {
  LOG_ERROR_BUILD("Thread pool has 0 threads");
  return;
}

// Else-if chain (same variable, related conditions)
if (std::string validated_name = ValidateAndNormalizeStrategyName(strategy_name);
    validated_name == "static") {
  return std::make_unique<StaticChunkingStrategy>();
} else if (validated_name == "hybrid") {
  return std::make_unique<HybridStrategy>();
}

// File operations
if (int stderr_fd = open("/tmp/findhelper_stderr.log", O_WRONLY | O_CREAT | O_TRUNC, 0644); stderr_fd != -1) {
  dup2(stderr_fd, STDERR_FILENO);
  close(stderr_fd);
}

// JSON parsing
if (json config_json = json::parse(text); !ParseSearchConfigFromJson(config_json, result)) {
  return result;
}

// String operations
if (size_t last_slash = path.find_last_of("\\/"); last_slash != std::string::npos) {
  directory_path = path.substr(0, last_slash);
  filename = path.substr(last_slash + 1);
}

// Nested if with init-statement
if (auto it = id_to_path_index_.find(id); it != id_to_path_index_.end()) {
  if (size_t idx = it->second; is_deleted_[idx] == 0) {
    is_deleted_[idx] = 1;
  }
}
```

## Don't apply: variable used after the if

```cpp
// Variable used after if statement
int width = std::atoi(value);
if (width >= 640 && width <= 4096) {
  args.window_width_override = width;
}
LOG_INFO_BUILD("Width processed: " << width);  // width used here — keep declaration outside

// Variable used in multiple unrelated if statements
HRESULT hr = SomeFunction();
if (SUCCEEDED(hr)) { /* ... */ }
if (FAILED(hr)) { /* ... */ }  // hr used here too

// Variable used in else block AND after if
bool has_size_filter = state.sizeFilter != SizeFilter::None;
if ((has_time_filter || has_size_filter) && state.searchActive) {
  // ... use has_size_filter ...
} else {
  // ... use has_size_filter ...
}
size_t displayed_count = has_size_filter ? state.sizeFilteredCount : state.filteredCount;

// Variable used in else and after if (same scope)
uint64_t last_total_time = search_metrics.last_search_time_ms + search_metrics.last_postprocess_time_ms;
if (last_total_time < 1000) {
  ImGui::Text("Search: %llums", static_cast<unsigned long long>(last_total_time));
} else {
  ImGui::Text("Search: %.2fs", last_total_time / 1000.0);
}
```

## Don't apply: ImGui pattern

When a variable is modified by reference in the condition (e.g. `ImGui::SliderFloat(..., &var, ...)`) and then used after the `if` block, keep the declaration outside the `if`. Sonar cpp:S6004 may flag it; that is a false positive for this pattern.

```cpp
float font_size = settings.fontSize;
if (ImGui::SliderFloat("Font Size", &font_size, 10.0F, 24.0F, "%.1f")) {
  settings.fontSize = font_size;
  changed = true;
}
```

## Common patterns

- File I/O: `if (int fd = open(...); fd != -1) { ... }`
- JSON: `if (json obj = json::parse(text); !obj.empty()) { ... }`
- String: `if (size_t pos = str.find(':'); pos != std::string::npos) { ... }`
- Map lookup: `if (auto it = map.find(key); it != map.end()) { ... }`
- Environment: `if (const char* home = std::getenv("HOME"); home != nullptr) { ... }`
- Windows API: `if (DWORD result = GetModuleFileNameA(...); result > 0) { ... }`

## Real-world example (GeminiApiUtils.cpp)

```cpp
// Before: variable declared before if, only used within if
const std::string ext_pattern = std::string("/") + "*." + ext;
if (TryRemoveExtensionPattern(path_content, ext, ext_pattern)) {
  return true;
}

// After: init-statement
if (std::string ext_pattern = std::string("/") + "*." + ext; TryRemoveExtensionPattern(path_content, ext_pattern)) {
  return true;
}
```

## SonarQube

Rule **cpp:S6004** flags variables that could use init-statements. Always verify the variable is not used after the `if` block; if it is, the warning is a false positive.
