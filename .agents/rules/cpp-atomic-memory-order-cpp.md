---
description: Omit the std::memory_order argument by default; an explicit non-seq_cst order requires a documented reason plus NOSONAR(cpp:S8417)
globs: "**/*.cpp,**/*.h"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
---

# Atomic memory orderings

Sonar rule **`cpp:S8417`** ("Use `std::memory_order_seq_cst` or remove this argument to
ensure sequential consistency") fires on **every explicit memory order other than
`seq_cst`** — including `relaxed`. It compares each explicit order against the implicit
default, so a variable whose loads/stores omit the argument looks "inconsistent" the
moment any single access spells an order out.

**Consequence: S8417 cannot be satisfied by making non-`seq_cst` orders *consistent*.**
Uniformly writing `relaxed` everywhere just turns 4 issues into 14. Omitting the argument
is the only way to clear it without a suppression.

## Default: omit the argument

For ordinary atomics — progress counters, caches, stats, "have we started" flags with no
publication protocol — **do not pass a memory order at all**:

```cpp
// ✅ Default seq_cst, and Sonar cpp:S8417 stays clean.
files_processed_.fetch_add(local);
dirs_processed_.fetch_add(1);
flag_.store(true);
if (stop_.load()) { /* ... */ }

// ❌ Each explicit non-seq_cst order is an open Sonar issue.
files_processed_.fetch_add(local, std::memory_order_relaxed);
```

This costs essentially nothing. On x86-64 a sequentially consistent `fetch_add` compiles to
the same `lock xadd` as a `relaxed` one, and the counters above are batched per directory
rather than updated per file. Do not trade clarity and a clean analyzer report for a
micro-gain that does not exist on the target platform.

## When an explicit order is justified

Write `relaxed` / `acquire` / `release` / `acq_rel` only when there is a **specific
correctness requirement**, such as:

- Publishing non-atomic data to another thread (release store / acquire load).
- Double-checked locking on a non-atomic pointer.
- Lock-free handoff between a producer and consumer queues.
- Read-modify-write on a value that must not be reordered (`compare_exchange` chains).

Then, on **every** access to that variable, add a same-line suppression with a real reason:

```cpp
// ✅ src/utils/CpuFeatures.cpp is the reference implementation of this pattern.
g_avx2_supported.store(supported, std::memory_order_relaxed);  // NOSONAR(cpp:S8417) - ordered before the release-store below; readers acquire via the flag
g_avx2_checked.store(true, std::memory_order_release);         // NOSONAR(cpp:S8417) - release: makes the relaxed store above visible to any acquire-load of this flag
```

## Rules

1. **Never mix.** If one access to an atomic needs an explicit order, every access to that
   same atomic gets an explicit order **and** a `NOSONAR(cpp:S8417)` justification.
   A single unsuppressed `relaxed` next to defaulted loads is the exact defect S8417 reports.
2. **A suppression is not a licence.** `NOSONAR(cpp:S8417) - <reason>` must state the
   ordering protocol (what pairs with what, and why the default is insufficient).
   `- perf` or `- faster` is not a reason; a wrong `relaxed` is a data race.
3. **Verify, don't assume.** After changing an ordering, run
   `./scripts/build_tests_macos.sh --tsan`. ThreadSanitizer models C++ atomics precisely and
   is the check that proves the ordering is actually sufficient.
4. **Guardrail.** `scripts/check_cpp_guardrails.py` fails the pre-commit hook on any
   explicit non-`seq_cst` order that lacks `NOSONAR(cpp:S8417)`.

## Suppression scope

`NOSONAR` must sit on the **same line** as the offending call (see
`.agents/rules/suppressions-same-line.md`). A block-level `// NOSONAR` on the function
signature will not suppress the issue.