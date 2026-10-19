---
description: Container allocation, buffer hoisting, and capacity reservation guidelines
globs: "**/*.cpp,**/*.h,**/*.hpp"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
---

# Container Allocation, Buffer Hoisting, and Capacity Reservation

Follow these rules in performance-sensitive paths (search loops, pattern matching, path manipulation, UI formatting) to eliminate redundant dynamic heap allocations and memory churn.

## 1. Scratch Container Hoisting in Loops
- **Never** instantiate a dynamic container (`std::vector`, `std::string`) inside a loop when it serves as a temporary work/scratch buffer.
- **Always** declare the scratch container before the loop and invoke `.clear()` inside the loop (or after each unit of work).
- Capacity allocated on the first iteration is retained across subsequent iterations, reducing \(N\) heap allocations/deallocations down to at most 1.

```cpp
// ❌ WRONG: heap allocation and deallocation on every single iteration
for (const auto& run : runs) {
  std::vector<MatchSpan> hits;
  FindMatches(run, hits);
  Process(hits);
}

// ✅ CORRECT: scratch vector capacity is preserved and reused across iterations
std::vector<MatchSpan> hits;
for (const auto& run : runs) {
  hits.clear();
  FindMatches(run, hits);
  Process(hits);
}
```

## 2. Upfront Capacity Reservation
- **Always** call `.reserve(...)` on `std::vector` and `std::string` before populating them when the count or a reasonable upper bound is known in advance (e.g., `pattern.length()`, `source.size()`, `prefix.length() + suffix_len`).
- This avoids repeated exponential reallocations and eliminates heap transitions when strings grow beyond Small String Optimization (SSO) capacity.

```cpp
// ❌ WRONG: incremental growth causes multiple reallocations
std::string result;
for (char c : input) {
  result.push_back(ToLower(c));
}

// ✅ CORRECT: single allocation
std::string result;
result.reserve(input.length());
for (char c : input) {
  result.push_back(ToLower(c));
}
```

## 3. String Concatenation and Known Segment Lengths
- When concatenating strings where substring lengths are already known (e.g. from `std::string_view` or prior length calculations), pass explicit lengths via `.append(ptr, len)` or use `std::string_view`.
- **Never** pass a raw `const char*` to `append()` or `+=` if its length is already computed; doing so incurs redundant `strlen` passes.

```cpp
// ❌ WRONG: std::string(new_prefix) does unreserved alloc, and append(path + old_len) calls strlen
std::string new_path(new_prefix);
new_path.append(path + old_len);

// ✅ CORRECT: exact capacity reserved; known suffix_len passed directly
std::string new_path;
new_path.reserve(new_prefix.length() + suffix_len);
new_path.append(new_prefix);
new_path.append(path + old_len, suffix_len);
```

## 4. Sub-threshold Early Guard Exits
- If an algorithm or extraction function requires a minimum input length (e.g., extracting a substring of length \(\ge 3\)), add an early guard return `if (input.length() < kMinLength) return {};` before any container allocation or reservation.
