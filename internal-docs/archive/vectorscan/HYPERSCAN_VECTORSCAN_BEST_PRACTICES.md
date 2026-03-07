# Hyperscan/VectorScan Best Practices & Optimization Guide

This guide consolidates research from official Hyperscan repositories, benchmarking tools, and real-world examples to provide actionable best practices for efficient pattern matching with Hyperscan and VectorScan.

---

## Table of Contents

1. [Scratch Space Management](#scratch-space-management)
2. [Database Pooling & Reuse](#database-pooling--reuse)
3. [Multi-Pattern Matching Optimization](#multi-pattern-matching-optimization)
4. [Performance Tuning Tips](#performance-tuning-tips)
5. [Streaming vs Batch Processing](#streaming-vs-batch-processing)
6. [Memory Efficiency Patterns](#memory-efficiency-patterns)
7. [Database Serialization & Caching](#database-serialization--caching)
8. [Multi-threaded Scanning](#multi-threaded-scanning)

---

## 1. Scratch Space Management

### What is Scratch Space?

Scratch space (`hs_scratch_t` / `ch_scratch_t`) is temporary per-thread working memory allocated once and reused across multiple scans. It's **not thread-safe** if shared—each thread or concurrent caller needs its own scratch space.

### Key Principles

**✅ DO:**
- Allocate scratch **once** immediately after database compilation, **NOT** in the scanning loop
- Keep one scratch space per thread or concurrent caller
- Allocate scratch during initialization phase, not during performance-critical scanning
- Clone scratch for multi-threaded scenarios using `hs_clone_scratch()` or `ch_clone_scratch()`
- Query scratch size with `hs_scratch_size()` to understand memory requirements

**❌ DON'T:**
- Attempt to reuse scratch in callbacks or recursive scanning (creates `HS_SCRATCH_IN_USE` error)
- Allocate scratch inside the scanning loop (performance killer)
- Share a single scratch space across threads (undefined behavior)
- Attempt to use scratch while it's marked "in use"

### Basic Scratch Allocation Pattern

```c
// 1. Compile database
hs_database_t *db = NULL;
hs_compile_error_t *compile_err = NULL;
hs_error_t err = hs_compile("pattern", 0, HS_MODE_BLOCK, NULL, &db, &compile_err);
if (err != HS_SUCCESS) {
    // Handle compilation error
    hs_free_compile_error(compile_err);
    return;
}

// 2. Allocate scratch immediately after compilation (one-time cost)
hs_scratch_t *scratch = NULL;
err = hs_alloc_scratch(db, &scratch);
if (err != HS_SUCCESS) {
    fprintf(stderr, "ERROR: could not allocate scratch space\n");
    hs_free_database(db);
    return;
}

// 3. Reuse scratch across multiple scans (cheap operation)
err = hs_scan(db, "data", 4, 0, scratch, onMatch, NULL);

// 4. Free scratch when done
hs_free_scratch(scratch);
hs_free_database(db);
```

### Scratch Growth for Multiple Databases

When using multiple databases, grow a single scratch space to accommodate all:

```c
// Allocate scratch for first database
err = hs_alloc_scratch(db_streaming, &scratch);
if (err != HS_SUCCESS) {
    fprintf(stderr, "ERROR: could not allocate scratch space\n");
    return;
}

// This second call will increase the scratch size if more is required
err = hs_alloc_scratch(db_block, &scratch);
if (err != HS_SUCCESS) {
    fprintf(stderr, "ERROR: could not allocate scratch space\n");
    return;
}

// Now scratch is large enough for both databases
hs_scan(db_streaming, data1, len1, 0, scratch, onMatch, NULL);
hs_scan(db_block, data2, len2, 0, scratch, onMatch, NULL);
```

**Source:** [pcapscan.cc](https://github.com/intel/hyperscan/blob/main/examples/pcapscan.cc#L185-L208)

### Scratch Size Queries

```c
size_t scratch_size;
hs_error_t err = hs_scratch_size(scratch, &scratch_size);
if (err == HS_SUCCESS) {
    printf("Scratch space size: %zu bytes\n", scratch_size);
}
```

### Handling Scratch-In-Use Errors

If you encounter `HS_SCRATCH_IN_USE` errors:
- The scratch space is being used in a nested scan (e.g., from a callback)
- Create a separate scratch space for the nested context
- Return from the callback rather than recursively scanning

```c
// If you need recursive/nested scanning:
hs_scratch_t *nested_scratch = NULL;
hs_error_t err = hs_alloc_scratch(db, &nested_scratch);
if (err != HS_SUCCESS) {
    return 1;  // Return from callback
}

// Use nested_scratch for inner scanning
// Later: hs_free_scratch(nested_scratch);
```

---

## 2. Database Pooling & Reuse

### Database Compilation vs Runtime

**Key Insight:** Database **compilation is expensive** (one-time cost), but **runtime scanning is cheap** (reuse the database). Always compile once and scan many times.

### Multi-Database Pattern

For applications with multiple pattern sets, pre-compile all databases:

```c
typedef struct {
    hs_database_t *db_streaming;
    hs_database_t *db_block;
    hs_scratch_t *scratch;
} DatabasePool;

// Initialization (happens once at startup)
DatabasePool* InitializeDatabasePool() {
    DatabasePool *pool = malloc(sizeof(DatabasePool));
    
    // Compile databases
    hs_compile_error_t *err = NULL;
    hs_compile("streaming_pattern", 0, HS_MODE_STREAM, NULL, 
               &pool->db_streaming, &err);
    hs_compile("block_pattern", 0, HS_MODE_BLOCK, NULL, 
               &pool->db_block, &err);
    
    // Allocate single scratch for both
    hs_alloc_scratch(pool->db_streaming, &pool->scratch);
    hs_alloc_scratch(pool->db_block, &pool->scratch);
    
    return pool;
}

// Usage (happens repeatedly, fast path)
void ScanWithPool(DatabasePool *pool, const char *data, size_t len) {
    hs_scan(pool->db_streaming, data, len, 0, pool->scratch, onMatch, NULL);
    hs_scan(pool->db_block, data, len, 0, pool->scratch, onMatch, NULL);
}

// Cleanup (happens once at shutdown)
void FreeDatabasePool(DatabasePool *pool) {
    hs_free_scratch(pool->scratch);
    hs_free_database(pool->db_streaming);
    hs_free_database(pool->db_block);
    free(pool);
}
```

**Source:** [pcapscan.cc Benchmark class](https://github.com/intel/hyperscan/blob/main/examples/pcapscan.cc#L185-L208)

### Pattern for Single Global Database

```c
// Global pool (thread-safe with proper synchronization)
static hs_database_t *g_database = NULL;
static pthread_mutex_t g_db_mutex = PTHREAD_MUTEX_INITIALIZER;

void InitializeGlobalDatabase() {
    pthread_mutex_lock(&g_db_mutex);
    if (g_database == NULL) {
        hs_compile_error_t *err = NULL;
        hs_compile("pattern", 0, HS_MODE_BLOCK, NULL, &g_database, &err);
    }
    pthread_mutex_unlock(&g_db_mutex);
}

// Per-thread scratch
__thread hs_scratch_t *tls_scratch = NULL;

void EnsureThreadScratch() {
    if (tls_scratch == NULL) {
        hs_alloc_scratch(g_database, &tls_scratch);
    }
}
```

---

## 3. Multi-Pattern Matching Optimization

### Chimera: Hybrid Pattern Matching

For applications needing both Hyperscan performance AND PCRE compatibility:

```c
// Compile with Chimera (PCRE + Hyperscan hybrid)
const char *patterns[] = {
    "simple_pattern",
    "regex_with_lookahead(?=suffix)"
};
unsigned flags[] = {0, 0};
unsigned ids[] = {1, 2};

ch_database_t *db = NULL;
ch_compile_error_t *compile_err = NULL;
ch_error_t err = ch_compile_multi(patterns, flags, ids, 2, 0, NULL, 
                                   &db, &compile_err);
if (err != CH_SUCCESS) {
    fprintf(stderr, "Compilation failed\n");
    return;
}

// Allocate scratch for hybrid database
ch_scratch_t *scratch = NULL;
err = ch_alloc_scratch(db, &scratch);

// Scan with hybrid matcher
err = ch_scan(db, data, len, 0, scratch, onMatch, NULL, NULL);

// Cleanup
ch_free_scratch(scratch);
ch_free_database(db);
```

### Pattern Compilation Optimization Tips

```c
// Tip 1: Use HS_FLAG_SOM_LEFTMOST only when needed
// Increases stream state size significantly
hs_error_t err = hs_compile("pattern", HS_FLAG_SOM_LEFTMOST, 
                            HS_MODE_STREAM, NULL, &db, &err);

// Tip 2: Use HS_FLAG_SINGLEMATCH when only first match matters
hs_compile("pattern", HS_FLAG_SINGLEMATCH, HS_MODE_BLOCK, NULL, &db, &err);

// Tip 3: Prefer anchored patterns (compile faster, execute faster)
// Good: ^start.*end$
// Bad:  .*start.*end.*
```

**Source:** [Official Hyperscan performance.rst](https://intel.github.io/hyperscan/dev-reference/performance.rst)

---

## 4. Performance Tuning Tips

### Benchmark Your Patterns

Use the pattern benchmarking tool to identify expensive patterns:

```c
// From patbench.cc pattern removal strategy
// Iteratively remove patterns and measure performance
class Benchmark {
    size_t getScratchSize() const {
        size_t scratch_size;
        hs_scratch_size(scratch, &scratch_size);
        return scratch_size;
    }
    
    // Measure: throughput (t), scratch size (r), stream state (s), 
    //          compile time (c), bytecode size (b)
};
```

**Key Metrics to Track:**
- **Throughput:** MB/s scanned (higher is better)
- **Scratch Size:** Memory footprint (lower is better, affects cache)
- **Stream State Size:** State per active stream (lower is better for streaming mode)
- **Compile Time:** Database compilation duration (one-time cost)
- **Bytecode Size:** Compiled database size (affects memory/cache)

### Performance Best Practices

| Practice | Impact | Details |
|----------|--------|---------|
| **Allocate scratch outside loop** | Critical ⚠️ | Scratch allocation is expensive; never do it per-scan |
| **Block mode preferred** | High | 30-40% faster than streaming if data is contiguous |
| **Shorter patterns** | Medium | Simpler patterns = faster matching |
| **Longer literals** | Medium | More specific patterns execute faster |
| **Avoid bounded repeats in streaming** | High | `{0,1000}` in streaming = large state overhead |
| **Use single-match mode** | Low-Medium | Only if you don't need all matches |
| **Prefer anchored patterns** | Medium | `^pattern$` faster than `.*pattern.*` |

**Source:** [pcapscan.cc](https://github.com/intel/hyperscan/blob/main/examples/pcapscan.cc) - Demonstrates dual-mode scanning

### Scratch Space Sizing Strategy

```c
// Wrong: Allocate once per database
for (int i = 0; i < num_databases; i++) {
    hs_scratch_t *scratch = NULL;
    hs_alloc_scratch(databases[i], &scratch);  // ❌ Waste of time
    // ...
}

// Right: Grow single scratch for all databases
hs_scratch_t *scratch = NULL;
for (int i = 0; i < num_databases; i++) {
    hs_alloc_scratch(databases[i], &scratch);  // ✅ Grows as needed
}
```

---

## 5. Streaming vs Batch Processing

### Block Mode (HS_MODE_BLOCK)

**Use when:** Data arrives as complete contiguous blocks

```c
// Block mode: Process one buffer completely
hs_error_t err = hs_scan(db, buffer, buffer_len, 0, scratch, 
                         onMatch, NULL);
```

**Advantages:**
- Simpler API (single scan call)
- Faster execution (no state management)
- Lower memory (no stream state)
- Best for file scanning, static data

**Disadvantages:**
- Patterns split across blocks are missed
- Cannot match patterns spanning multiple buffers

### Streaming Mode (HS_MODE_STREAM)

**Use when:** Data arrives incrementally (networks, real-time feeds)

```c
// Streaming workflow
hs_stream_t *stream = NULL;
hs_error_t err = hs_open_stream(db, 0, &stream);

// Process chunks
while ((chunk = GetNextChunk()) != NULL) {
    err = hs_scan_stream(db, chunk, chunk_len, 0, stream, 
                         onMatch, NULL);
    if (err != HS_SUCCESS) break;
}

// Finalize (triggers end-of-data matches for anchored patterns)
err = hs_close_stream(stream, scratch, onMatch, NULL);
hs_stream_t stream = NULL;  // Reset
```

**Advantages:**
- Matches patterns across chunk boundaries
- Efficient for streaming data
- Progressive scanning (no need to buffer entire input)

**Disadvantages:**
- Maintains state per stream (memory overhead)
- Slightly slower than block mode for same data
- More complex API

### Choosing the Right Mode

```c
// Pseudo-code: Decision matrix

if (data_is_contiguous) {
    mode = HS_MODE_BLOCK;  // Faster, simpler
} else if (data_arrives_in_chunks) {
    mode = HS_MODE_STREAM;  // Necessary for chunked data
} else if (need_multiple_discontinuous_blocks) {
    mode = HS_MODE_VECTORED;  // Scan multiple blocks in one call
}
```

### Stream State Management

```c
// Stream compression (reduces memory footprint)
hs_stream_t *compressed = NULL;
char *bytes = NULL;
size_t len = 0;

err = hs_compress_stream(db, stream, &bytes, &len);
// ... save compressed stream ...
err = hs_expand_stream(db, bytes, len, &stream);
hs_free(bytes);

// Stream cloning (for checkpointing)
hs_stream_t *checkpoint = NULL;
err = hs_copy_stream(&checkpoint, stream);
// ... stream continues normally ...
// ... if error occurs, can revert to checkpoint ...
hs_close_stream(checkpoint, scratch, NULL, NULL);
```

**Source:** [hscollider stream operations](https://github.com/intel/hyperscan/blob/main/tools/hscollider/main.cpp)

---

## 6. Memory Efficiency Patterns

### Custom Allocators

For applications with special memory requirements:

```c
#include <stdlib.h>

// Custom allocator callbacks
void* MyMalloc(size_t size) {
    // Allocate from custom pool/arena
    return malloc(size);
}

void MyFree(void *ptr) {
    free(ptr);
}

// Register custom allocator before compilation
hs_set_allocator(MyMalloc, MyFree);
hs_set_scratch_allocator(MyMalloc, MyFree);

// Compile database (uses custom allocator)
hs_database_t *db = NULL;
hs_compile_error_t *err = NULL;
hs_compile("pattern", 0, HS_MODE_BLOCK, NULL, &db, &err);

// Reset to system allocator
hs_set_allocator(NULL, NULL);
```

### Scratch Size Prediction

```c
// Query scratch size before allocating from fixed pool
size_t scratch_size;
hs_scratch_size(scratch, &scratch_size);

if (scratch_size > MAX_SCRATCH_SIZE) {
    fprintf(stderr, "Scratch too large: %zu > %zu\n", 
            scratch_size, MAX_SCRATCH_SIZE);
    return -1;
}

// Allocate from your pool
char *memory = AllocateFromPool(scratch_size);
```

### Memory-Efficient Pattern Design

```c
// Expensive: unbounded repeats
"prefix.*{1000,}suffix"  // ❌ Large bytecode, large state

// Efficient: bounded repeats
"prefix.{0,100}suffix"   // ✅ Smaller bytecode

// Expensive: complex alternations
"(a|b|c|d|e|f|g|h|i|j)+" // ❌ Bloated

// Efficient: character class
"[abcdefghij]+"          // ✅ Compact
```

---

## 7. Database Serialization & Caching

### Why Serialize?

- **Pre-compile databases:** Compile patterns once, deploy many times
- **Save to disk:** Avoid recompilation at startup
- **Cross-platform:** Compile on Linux, deploy on Windows
- **Shared memory:** Place database on huge pages for performance

### Serialization Workflow

```c
// 1. Compile database
hs_database_t *db = NULL;
hs_compile_error_t *err = NULL;
hs_compile("pattern", 0, HS_MODE_BLOCK, NULL, &db, &err);

// 2. Serialize to bytes
char *bytes = NULL;
size_t length = 0;
hs_error_t ret = hs_serialize_database(db, &bytes, &length);
if (ret != HS_SUCCESS) {
    fprintf(stderr, "Serialization failed\n");
    return;
}

// 3. Save to disk or transfer
FILE *f = fopen("database.bin", "wb");
fwrite(bytes, 1, length, f);
fclose(f);

// 4. Free serialized bytes (allocated by Hyperscan)
free(bytes);
hs_free_database(db);
```

### Deserialization Workflow

```c
// 1. Load from disk
FILE *f = fopen("database.bin", "rb");
fseek(f, 0, SEEK_END);
size_t length = ftell(f);
fseek(f, 0, SEEK_SET);

char *bytes = malloc(length);
fread(bytes, 1, length, f);
fclose(f);

// 2. Deserialize (allocates new database)
hs_database_t *db = NULL;
hs_error_t ret = hs_deserialize_database(bytes, length, &db);
if (ret != HS_SUCCESS) {
    fprintf(stderr, "Deserialization failed: %d\n", ret);
    free(bytes);
    return;
}

// 3. Use database normally
hs_scratch_t *scratch = NULL;
hs_alloc_scratch(db, &scratch);
hs_scan(db, data, data_len, 0, scratch, onMatch, NULL);

// 4. Cleanup
hs_free_scratch(scratch);
hs_free_database(db);
free(bytes);  // Free input bytes
```

### Pre-allocated Deserialization

For applications with memory constraints:

```c
// 1. Determine size needed
hs_serialized_database_size(bytes, length, &db_size);

// 2. Allocate from fixed pool (8-byte aligned)
hs_database_t *db = AllocateAligned8(db_size);

// 3. Deserialize into pre-allocated location
hs_error_t ret = hs_deserialize_database_at(bytes, length, db);
if (ret != HS_SUCCESS) {
    fprintf(stderr, "Deserialization failed\n");
    return;
}

// 4. Use database (don't use hs_free_database!)
// Instead, free with your custom allocator
FreeAligned(db);
```

### Database Versioning

```c
// Query database info before using
char *info = NULL;
hs_error_t ret = hs_database_info(db, &info);
if (ret == HS_SUCCESS) {
    printf("Database info: %s\n", info);
    // Check version compatibility
    if (strstr(info, "Version: 5.4") == NULL) {
        fprintf(stderr, "Incompatible version\n");
        return -1;
    }
    free(info);  // Free allocated string
}
```

**Source:** [database_util.cpp](https://github.com/intel/hyperscan/blob/main/util/database_util.cpp) - Complete example

---

## 8. Multi-threaded Scanning

### Threading Model

**Critical Rule:** One scratch space per thread (not per database!)

### Pattern 1: Per-Thread Scratch (Clone Method)

```c
#include <pthread.h>

typedef struct {
    hs_database_t *db;
    hs_scratch_t *scratch;
} ThreadContext;

void InitializeThreadContext(ThreadContext *ctx, hs_database_t *db) {
    ctx->db = db;
    // Allocate scratch for this thread
    hs_alloc_scratch(db, &ctx->scratch);
}

void CleanupThreadContext(ThreadContext *ctx) {
    hs_free_scratch(ctx->scratch);
}

void* ThreadFunction(void *arg) {
    ThreadContext *ctx = (ThreadContext *)arg;
    
    // Each thread has its own scratch
    hs_scan(ctx->db, data, len, 0, ctx->scratch, onMatch, NULL);
    
    return NULL;
}

int main() {
    // Compile database once (shared across threads)
    hs_database_t *db = NULL;
    hs_compile_error_t *err = NULL;
    hs_compile("pattern", 0, HS_MODE_BLOCK, NULL, &db, &err);
    
    // Create thread-local scratch spaces
    pthread_t threads[NUM_THREADS];
    ThreadContext contexts[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        InitializeThreadContext(&contexts[i], db);
        pthread_create(&threads[i], NULL, ThreadFunction, &contexts[i]);
    }
    
    // Wait for threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
        CleanupThreadContext(&contexts[i]);
    }
    
    hs_free_database(db);
    return 0;
}
```

### Pattern 2: Clone Method (Master-Worker)

```c
// Main thread compiles and creates prototype scratch
hs_database_t *db = NULL;
hs_compile_error_t *err = NULL;
hs_compile("pattern", 0, HS_MODE_BLOCK, NULL, &db, &err);

hs_scratch_t *prototype_scratch = NULL;
hs_alloc_scratch(db, &prototype_scratch);

// Worker threads clone the prototype
void* WorkerThread(void *arg) {
    hs_scratch_t *thread_scratch = NULL;
    hs_clone_scratch(prototype_scratch, &thread_scratch);
    
    // Use cloned scratch
    hs_scan(db, data, len, 0, thread_scratch, onMatch, NULL);
    
    hs_free_scratch(thread_scratch);
    return NULL;
}

// After all threads complete
hs_free_scratch(prototype_scratch);
hs_free_database(db);
```

**Source:** [runtime.rst](https://intel.github.io/hyperscan/dev-reference/runtime.rst#L165-L183)

### Thread Synchronization for Accurate Timing

From hsbench (production benchmarking tool):

```c
#include <boost/thread/barrier.hpp>

class ThreadContext {
public:
    void barrier() {
        tb.wait();  // Synchronize all threads
    }
    
private:
    thread_barrier &tb;
};

// Usage:
for (int iteration = 0; iteration < NUM_ITERATIONS; ++iteration) {
    auto start = Timer::now();
    
    // Synchronize threads at start
    thread_context->barrier();
    
    // All threads start scanning simultaneously
    hs_scan(db, data, len, 0, scratch, onMatch, NULL);
    
    // Synchronize threads at end
    thread_context->barrier();
    
    auto elapsed = Timer::now() - start;
    results.push_back(elapsed);
}
```

### Thread-Safe Pattern Compilation

```c
#include <pthread.h>

static pthread_once_t g_init_once = PTHREAD_ONCE_INIT;
static hs_database_t *g_database = NULL;

void InitializeDatabase() {
    hs_compile_error_t *err = NULL;
    hs_compile("pattern", 0, HS_MODE_BLOCK, NULL, &g_database, &err);
}

int main() {
    // Ensure database initialized exactly once across all threads
    pthread_once(&g_init_once, InitializeDatabase);
    
    // All threads can now safely use g_database
    // But each thread needs its own scratch
    __thread hs_scratch_t *tls_scratch = NULL;
    if (tls_scratch == NULL) {
        hs_alloc_scratch(g_database, &tls_scratch);
    }
    
    hs_scan(g_database, data, len, 0, tls_scratch, onMatch, NULL);
}
```

---

## Quick Reference: Code Templates

### Template 1: Basic Single-Database Scanning

```c
#include "hs.h"
#include <stdio.h>
#include <string.h>

int onMatch(unsigned id, unsigned long long from, unsigned long long to,
            unsigned flags, void *ctx) {
    printf("Match at offset %llu-%llu\n", from, to);
    return 0;
}

int main() {
    hs_database_t *db = NULL;
    hs_scratch_t *scratch = NULL;
    hs_compile_error_t *compile_err = NULL;
    
    // Compile
    hs_error_t err = hs_compile("pattern", 0, HS_MODE_BLOCK, NULL, 
                                &db, &compile_err);
    if (err != HS_SUCCESS) {
        fprintf(stderr, "Compile failed\n");
        return 1;
    }
    
    // Allocate scratch
    err = hs_alloc_scratch(db, &scratch);
    if (err != HS_SUCCESS) {
        fprintf(stderr, "Scratch allocation failed\n");
        hs_free_database(db);
        return 1;
    }
    
    // Scan
    const char *data = "sample data to search";
    err = hs_scan(db, data, strlen(data), 0, scratch, onMatch, NULL);
    
    // Cleanup
    hs_free_scratch(scratch);
    hs_free_database(db);
    
    return 0;
}
```

### Template 2: Multi-Database with Single Scratch

```c
// Allocate scratch for multiple databases
hs_scratch_t *scratch = NULL;

hs_alloc_scratch(db1, &scratch);
hs_alloc_scratch(db2, &scratch);  // Grows if needed
hs_alloc_scratch(db3, &scratch);  // Grows if needed

// Scan with any database
hs_scan(db1, data1, len1, 0, scratch, onMatch, NULL);
hs_scan(db2, data2, len2, 0, scratch, onMatch, NULL);
hs_scan(db3, data3, len3, 0, scratch, onMatch, NULL);

hs_free_scratch(scratch);
```

### Template 3: Streaming Mode

```c
hs_stream_t *stream = NULL;
hs_open_stream(db, 0, &stream);

// Process chunks
for (int i = 0; i < num_chunks; i++) {
    hs_scan_stream(db, chunks[i], chunk_sizes[i], 0, stream, 
                   onMatch, NULL);
}

// Finalize (triggers end-of-data patterns)
hs_close_stream(stream, scratch, onMatch, NULL);
```

### Template 4: Serialization

```c
// Save
char *bytes = NULL;
size_t length = 0;
hs_serialize_database(db, &bytes, &length);
FILE *f = fopen("db.bin", "wb");
fwrite(bytes, 1, length, f);
fclose(f);
free(bytes);

// Load
FILE *f = fopen("db.bin", "rb");
fseek(f, 0, SEEK_END);
size_t length = ftell(f);
fseek(f, 0, SEEK_SET);
char *bytes = malloc(length);
fread(bytes, 1, length, f);
fclose(f);

hs_database_t *db = NULL;
hs_deserialize_database(bytes, length, &db);
free(bytes);
```

---

## Performance Summary

| Optimization | Speed Gain | Effort | Priority |
|--------------|-----------|--------|----------|
| Allocate scratch outside loop | **30-50%** | Easy | ⚠️ Critical |
| Use block mode vs streaming | **30-40%** | Medium | High |
| Pre-compile/cache databases | **50-90%** | Easy | High |
| Use appropriate pattern syntax | **10-30%** | Medium | Medium |
| Clone scratch for threads | **10-20%** | Easy | High |
| Custom allocators | **Variable** | Hard | Low |
| Stream compression | **N/A** | Easy | Medium |

---

## References

- **Official Hyperscan Documentation:** https://intel.github.io/hyperscan/
- **Runtime Guide:** https://intel.github.io/hyperscan/dev-reference/
- **Examples:** https://github.com/intel/hyperscan/tree/main/examples/
- **Tools/Benchmarks:** https://github.com/intel/hyperscan/tree/main/tools/
- **Unit Tests:** https://github.com/intel/hyperscan/tree/main/unit/

---

**Last Updated:** 2025-01-22
**Hyperscan Version:** 5.4+
**VectorScan Compatible:** Yes
