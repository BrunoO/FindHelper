# UI Blocking Issue - Analysis and Fix

## Problem

After implementing parallel filtering, the UI becomes blocked after clicking search.

## Root Cause

The issue is **lock contention** from multiple `GetEntry()` calls:

1. `ContiguousStringBuffer::Search()` holds a `shared_lock` for the entire search duration
2. Each parallel thread calls `fileIndex->GetEntry(id)` for every path match
3. Each `GetEntry()` call acquires/releases a `shared_lock` on `FileIndex::mutex_`
4. With many threads and many matches, this creates heavy lock contention
5. If the UI thread (or USN processor thread) tries to acquire a `unique_lock` on `FileIndex::mutex_` (for Insert/Remove), it blocks waiting for all `shared_lock`s to be released

## Current Status

The lock is correctly held for the entire search to prevent vector modifications. However, the many `GetEntry()` calls create contention.

## Potential Solutions

### Option 1: Batch GetEntry() calls (Recommended)
Instead of calling `GetEntry()` for each match individually, collect all IDs first, then do batch lookups:
- Collect all matching IDs from all threads
- Do a single batch lookup with `ForEachEntryByIds()` which holds one lock for all lookups
- Apply filters after batch lookup

### Option 2: Store extension/directory info in ContiguousStringBuffer
Add extension and isDirectory fields to ContiguousStringBuffer so filtering doesn't require FileIndex lookups:
- More memory usage
- Requires updating Insert() to store this data
- Eliminates FileIndex lookups entirely

### Option 3: Use GetEntryRef() with proper lock management
Use `GetEntryRef()` but ensure the reference is used immediately before the lock is released. However, this still has the same lock contention issue.

### Option 4: Two-phase filtering
1. Phase 1: Parallel path search (no FileIndex lookups)
2. Phase 2: Batch FileIndex lookups for all candidates, then filter

## Immediate Workaround

For now, the code is correct but may be slow with heavy filtering. The UI blocking should be minimal since:
- Search happens in background thread
- Lock contention is on FileIndex, not ContiguousStringBuffer
- USN processor thread yields periodically

If UI blocking persists, consider implementing Option 1 (batch lookups).
