---
description: Prefer std::optional<T> and [[nodiscard]] over boolean out-parameters
globs: "**/*.cpp,**/*.h,**/*.hpp"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
---

# `std::optional<T>` vs. Out-Parameters

For fallible operations that produce a value, return `[[nodiscard]] std::optional<T>` instead of using a boolean return with a mutable reference out-parameter.

## Why

- **Eliminates Unchecked Initialization:** Callers cannot read uninitialized, partially initialized, or stale variables across failure paths.
- **Explicit Function Contracts:** The return type clearly communicates that failure produces no value.
- **Compiler Enforced:** `[[nodiscard]]` prevents silently ignoring the operation outcome, and clang-tidy's `bugprone-unchecked-optional-access` flags unguarded `.value()` / `*opt` dereferences.
- **No Redundant Default Construction:** Eliminates the ceremony of instantiating an empty object before passing it by reference.

## Do

```cpp
// ✅ Pure value return with optional
[[nodiscard]] std::optional<ParsedUrl> ParseUrl(std::string_view url);
[[nodiscard]] std::optional<float> ConsumeScrollRestorePending();
[[nodiscard]] std::optional<AppSettings> LoadSettings();

// Idiomatic caller using C++17 init-statement:
if (const auto url = ParseUrl(raw_url); url.has_value()) {
  Connect(url->host, url->port);
}

// Direct unwrap with fallback:
const AppSettings settings = LoadSettings().value_or(AppSettings{});
```

## Don't

```cpp
// ❌ C-style out-parameter requiring caller pre-declaration
bool ParseUrl(std::string_view url, ParsedUrl& out);
bool ConsumeScrollRestorePending(float& out_y);
bool LoadSettings(AppSettings& out);

// Risky caller:
ParsedUrl url;
ParseUrl(raw_url, url); // Return value unchecked, url may be invalid!
```

## Exception: Hoisted Reusable Scratch Buffers

Out-parameters are permitted **only** in high-throughput loops where the caller hoists an allocation to avoid millions of allocations/deallocations per second (e.g., `vector.clear()` preserving capacity):

```cpp
// ✅ Legitimate out-parameter for buffer reuse in hot path
bool UsnJournalQueue::Pop(std::vector<char>& buffer);
```
