# 2026-02-18 Set and unordered_set usage vs FAST_LIBS

## Summary

- **unordered_set**: Already covered by FAST_LIBS via `hash_set_t` and `hash_set_stable_t` in `HashMapAliases.h`. All hot-path unordered-set usage should go through these aliases when FAST_LIBS_BOOST=ON.
- **std::set (ordered set)**: Not in FAST_LIBS. There are three use cases; two can benefit from switching to `hash_set_t` where order is not required; one (transparent lookup) needs either a dedicated alias or staying with `std::set` with `std::less<void>`.

## 1. Current FAST_LIBS coverage (HashMapAliases.h)

| Alias              | When FAST_LIBS_BOOST=ON   | When OFF        |
|--------------------|---------------------------|-----------------|
| `hash_map_t<K,V>`  | `boost::unordered_map`    | `std::unordered_map` |
| `hash_set_t<V>`    | `boost::unordered_set`    | `std::unordered_set`  |
| `hash_set_stable_t<V>` | `boost::unordered_set` | `std::unordered_set`  |

So **unordered_set is already in the FAST_LIBS approach**. The only remaining check is that no code uses `std::unordered_set` or `#include <unordered_set>` directly when it could use `hash_set_t` / `hash_set_stable_t`.

**Verified**: The only direct `std::unordered_set` / `<unordered_set>` in `src/` is inside `HashMapAliases.h`. `FileIndexStorage.h` uses `hash_set_stable_t<std::string>` for the string pool. No other source file uses raw `std::unordered_set`; no benefit to "adding" unordered_set to FAST_LIBS (it is already there).

## 2. std::set usage in the codebase

All current ordered-set usage is `std::set`:

| Location            | Type / use                                                                 | Order required? |
|---------------------|----------------------------------------------------------------------------|------------------|
| SearchContext.h     | `ExtensionSet = std::set<std::string, std::less<void>>` (extension filter) | No (lookup only) |
| GuiState.h          | `std::set<uint64_t> deferredCloudFiles`                                   | No (membership)  |
| GuiState.h          | `std::set<std::string, std::less<>> pendingDeletions`                     | No (transparent lookup) |
| GuiState.h          | `std::set<uint64_t> markedFileIds`                                         | No (membership)  |

- **SearchController.cpp** includes `<set>` but only uses `state.deferredCloudFiles` and `state.markedFileIds` (types come from GuiState.h). The extra `#include <set>` in SearchController.cpp is redundant and can be removed.

## 3. Benefit of including set/unordered_set in FAST_LIBS

### 3.1 unordered_set

- **Already in FAST_LIBS** via `hash_set_t` and `hash_set_stable_t`.
- **Action**: Ensure any new "set of items, no order" use uses `hash_set_t` (or `hash_set_stable_t` where pointer stability is required) so FAST_LIBS_BOOST=ON gets Boost automatically. No new aliases needed.

### 3.2 Replacing std::set where order is not required

- **deferredCloudFiles**, **markedFileIds**: Used only for membership (insert/erase/clear/count). Can be switched to `hash_set_t<uint64_t>` so FAST_LIBS_BOOST=ON uses `boost::unordered_set`; same API, typically faster lookups.
- **pendingDeletions**: Same membership use but with transparent comparator for `string_view` lookups. Options:
  - Keep `std::set<std::string, std::less<>>` for transparent find(string_view), or
  - Introduce an unordered set with transparent hash/equal (e.g. Boost with `boost::hash<std::string_view>` and `std::equal_to<void>`) behind an alias; then use that when FAST_LIBS_BOOST=ON.
- **ExtensionSet**: Hot path (extension filter in search). Currently `std::set<std::string, std::less<void>>` for transparent find(string_view) without allocation. Options:
  - Keep as-is (ordered set, transparent compare), or
  - Add an alias for "unordered set of strings with transparent lookup" (Boost supports this) and use it when FAST_LIBS_BOOST=ON for better lookup performance.

### 3.3 Adding an ordered-set alias to FAST_LIBS

- **Optional**: Define e.g. `set_t<V>` or `ordered_set_t<V>` mapping to `boost::container::set` when FAST_LIBS_BOOST=ON and `std::set` otherwise. Benefit is small unless we have many ordered sets or need Boost’s set behavior. Current code has only a few `std::set` usages; the larger gain is replacing order-unnecessary sets with `hash_set_t`.

## 4. Recommendations

1. **unordered_set**: Treat as already in FAST_LIBS. Use `hash_set_t` / `hash_set_stable_t` for all unordered-set use so FAST_LIBS_BOOST=ON is applied consistently.
2. **Redundant include**: Remove `#include <set>` from `SearchController.cpp` (types come from GuiState.h).
3. **Optional refactors** (if we want more FAST_LIBS benefit from sets):
   - Replace `std::set<uint64_t> deferredCloudFiles` and `std::set<uint64_t> markedFileIds` in GuiState.h with `hash_set_t<uint64_t>` (include `HashMapAliases.h` if not already).
   - For ExtensionSet / pendingDeletions: either keep current `std::set` with `std::less<void>` or introduce a transparent-heterogeneous unordered-set alias (Boost) and use it when FAST_LIBS_BOOST=ON; document in HashMapAliases.h.
4. **Ordered set alias**: Add `set_t` / `ordered_set_t` to FAST_LIBS only if we later add more ordered-set use or want a single knob for ordered set implementation.

## 5. References

- `src/utils/HashMapAliases.h` – current hash_map / hash_set aliases
- `docs/analysis/2026-02-02_HOT_SEARCH_PATH_OPTIMIZATION_REVIEW.md` – extension_set and heterogeneous lookup
- Boost optimizations branch review – flat_set_t / flat_map_t (branch, not merged); see project maintainer for details
