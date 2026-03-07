# Search Code Paths Documentation

## Overview

The search functionality uses different code paths depending on whether there's a search query (filename/path) or not.

## Search Paths

### PATH 1: With Search Query (Uses ContiguousStringBuffer::Search)

**Condition:** `query` is not empty (user has entered filename or path search)

**Location:** `SearchWorker.cpp` lines 143-202

**Query Selection Logic:**
- If both `filenameSearch` and `pathSearch` are provided: **Only `filenameSearch` is used**
- If only `filenameSearch` is provided: Uses `filenameSearch`
- If only `pathSearch` is provided: Uses `pathSearch`
- **Rationale:** Since `ContiguousStringBuffer` stores full paths (which include filenames), searching for a filename in the full path will find matches. The path query is ignored when filename is present.

**Flow:**
1. Selects query (filename takes precedence over path)
2. Calls `ContiguousStringBuffer::Search()` for parallel path matching
3. Gets candidate IDs that match the query in the full path
4. Sequentially filters candidates by extension (if specified)
5. Sequentially filters candidates by folders-only (if specified)
6. Converts filtered IDs to SearchResult objects

**Limitation:**
- If user wants to search for files with a specific filename **AND** in a specific path directory, this requires both conditions to be true. Currently, only the filename is searched in the full path, which may match files with that filename in any directory.
- To support true "filename AND path" search, we would need to:
  - Do two separate searches (one for filename, one for path)
  - Intersect the results
  - Or modify ContiguousStringBuffer to support combined queries

**Why this path:**
- Parallel search is optimized for path substring matching
- Fast parallel search across all paths
- Sequential filtering avoids lock contention from multiple threads calling `GetEntry()`

**Performance:**
- Parallel path search: Very fast (uses multiple threads)
- Sequential filtering: Slower but avoids lock contention

### PATH 2: No Search Query (Bypasses ContiguousStringBuffer::Search)

**Condition:** `query` is empty (no filename or path search, only extension/folders filters)

**Location:** `SearchWorker.cpp` lines 203-251

**Flow:**
1. Uses `FileIndex::ForEachEntry()` to iterate all entries
2. Applies extension filter (if specified)
3. Applies folders-only filter (if specified)
4. Converts matching entries to SearchResult objects

**Why this path:**
- No path matching needed (no query to search for)
- `ContiguousStringBuffer` is optimized for path substring search, not needed here
- `ForEachEntry()` is more efficient when we need to iterate all entries
- Single shared lock held for entire iteration (efficient)

**Use Cases:**
- User wants to see all `.txt` files (extension filter only)
- User wants to see all folders (folders-only filter)
- User wants to see all files (no filters)

**Performance:**
- Sequential iteration: O(n) where n = total entries
- No parallelization needed (no search work, just filtering)

## Summary

| Path | Query | Uses ContiguousStringBuffer | Method |
|------|-------|----------------------------|--------|
| PATH 1 | Has query | ✅ Yes | `ContiguousStringBuffer::Search()` + sequential filtering |
| PATH 2 | No query | ❌ No | `FileIndex::ForEachEntry()` with filters |

## Future Considerations

If PATH 2 becomes a performance bottleneck (e.g., very large indexes with only extension filters), consider:
- Parallelizing the `ForEachEntry()` iteration
- Creating a separate extension index (removed in previous refactoring)
- Using ContiguousStringBuffer even for "show all" (but this would be slower due to overhead)

For now, PATH 2 is optimal for its use case (no path search needed).
