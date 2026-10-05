# StringPool Design Documentation

## Current implementation (2026-03)

**As of March 2026, `FileIndexStorage` does not embed a `StringPool`, and `FileEntry` has no `extension` pointer.**

- **Per-file extensions for search and UI** come from **`PathStorage` SoA** data: precomputed offsets into the path buffer (e.g. `extension_start` in the SoA view), not from interned strings on `FileEntry`.
- **Build-time file names** for parent-chain walks use **`NameArena`** (`std::vector<char>` + index), not a `StringPool`.

The sections below document the **string-interning pattern** (stable pointers, `hash_set_stable_t`, trade-offs vs open-addressing sets) for **reference**—for example when choosing containers in `HashMapAliases.h`, or if a future feature reintroduces interned extension strings. They are **not** a description of the live `FileEntry` layout.

---

## Overview

`StringPool` is a **conceptual** string interning mechanism: deduplicate strings and return stable `const std::string*` for fast pointer equality. A possible use case is file extensions when many entries share the same short string.

## Purpose

### Problem Statement

In a file indexing system with millions of files, many files share the same extension (e.g., `.txt`, `.pdf`, `.jpg`). Without interning, each entry might store its own copy:

- **Memory waste**: Repeated identical extension strings.
- **Comparison overhead**: String compares instead of pointer equality (if you chose to store pointers).

### Solution: String Interning

Interning keeps a single canonical `std::string` per distinct extension and hands out pointers to it. **If** entries stored `const std::string* extension`, those pointers would need to remain valid across pool growth—hence **`hash_set_stable_t`** (see below).

**Today’s index:** extensions in the hot path are represented via **SoA offsets** into one path buffer, which avoids per-entry extension pointers and avoids this pool for search.

## Implementation

### Class Definition (illustrative)

```cpp
class StringPool {
public:
  const std::string *Intern(const std::string &str) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pool_.insert(ToLower(str)).first;
    return &(*it);
  }

private:
  std::mutex mutex_;
  hash_set_stable_t<std::string> pool_;
};
```

### Key Characteristics

1. **Thread-Safe**: Uses `std::mutex` to protect concurrent access
2. **Case-Insensitive**: Automatically lowercases all strings before interning
3. **Stable Pointers**: Returns `const std::string*` that remain valid across container rehashes
4. **Deduplication**: Same string (case-insensitive) always returns the same pointer

### Critical Design Decision: `hash_set_stable_t`

**Why `hash_set_stable_t` instead of `hash_set_t`?**

If code stores **pointers** returned by `Intern()` in long-lived objects, those pointers must stay valid when the set grows:

```cpp
const std::string* Intern(const std::string& str);
```

**Container behavior:**

| Container Type | Memory Layout | Pointer Stability |
|----------------|---------------|-------------------|
| `std::unordered_set` | Node-based | Stable (pointers remain valid) |
| `boost::unordered_set` | Node-based | Stable (pointers remain valid) |
| `ankerl::unordered_dense::set` | Contiguous buffer | Unstable (may move on rehash) |

**Consequence of wrong choice (hypothetical pointer-in-entry design):**

If entries held `const std::string*` into a contiguous open-addressing set, a rehash could move strings and **invalidate** stored pointers. **`hash_set_stable_t`** maps to node-based `std::unordered_set` / `boost::unordered_set` (`HashMapAliases.h`) so interned string addresses stay valid.

## Usage pattern (hypothetical / historical)

If insertion code interned extensions and stored pointers on a struct, the flow would look like:

1. Extract extension from filename (e.g., `"document.txt"` → `"txt"`).
2. `stringPool_.Intern("txt")` → stable pointer.
3. Store pointer on the entry (not how `FileEntry` is built today).

**Search path today:** uses **SoA** extension offsets and `string_view`, not `FileEntry` extension pointers—see `ParallelSearchEngine` / `PathStorage` and `SearchWorker` (e.g. `extension_start`).

### Why search does not use `FileEntry` for extensions

- **Cache locality**: SoA arrays are contiguous.
- **Avoid hash lookups per hit**: Resolving `FileEntry` for every candidate is slower than scanning SoA fields.

## Memory / performance notes (if pointer-per-entry were used)

Illustrative math from an older design (pointer per entry + small pool):

- **Without interning**: many duplicate `std::string` bodies per entry.
- **With interning**: 8 bytes per entry (pointer) + O(unique extensions) in the pool.

Actual memory for extensions in the **current** design is dominated by **PathStorage** (shared path buffer + offsets), not a `StringPool` on `FileIndexStorage`.

## Thread safety

`Intern()` would serialize on a mutex; indexing is often single-threaded; search would not need the pool if extensions come from SoA only.

## Integration with `FileIndexStorage` (current)

- **No** `StringPool stringPool_` on `FileIndexStorage` for extensions.
- **No** `InternExtension` API on `FileIndexStorage`.
- **`FileEntry`** holds metadata such as `parentID`, lazy size/time, `path_storage_index`, `isDirectory`—see `src/index/FileIndexStorage.h`.

## Future optimization ideas

- **Pointer-based filtering from SoA**: Could store interned pointers in SoA columns instead of offsets; trade complexity and stability against string/`string_view` compares—only if profiling warrants it.
- **Lock-free pool**: Usually unnecessary if `Intern()` is indexing-only.

## Testing considerations

If a `StringPool` class is introduced or tested in isolation:

1. Deduplication and case-insensitivity
2. Pointer stability after rehash
3. Thread safety of `Intern()` if called concurrently

## Summary

- **`hash_set_stable_t`** is the right alias when **stable addresses** into a string set are required (`HashMapAliases.h`).
- **Live index:** extensions for search are **SoA-backed**; **no** `StringPool` on `FileIndexStorage` for `FileEntry` extensions as of 2026-03.

---

**Last updated:** 2026-03-21 (aligned with `FileIndexStorage` / `PathStorage` implementation)  
**Related:** `docs/design/ARCHITECTURE_COMPONENT_BASED.md`, `src/utils/HashMapAliases.h`, `internal-docs/analysis/BOOST_VS_ANKERL_ANALYSIS.md`
