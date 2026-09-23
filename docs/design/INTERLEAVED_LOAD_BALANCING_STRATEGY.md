# Interleaved load balancing (historical)

**Status:** The interleaved and other non-hybrid parallel-search schedules are **not in the current codebase**. `ParallelSearchEngine` always uses **`HybridStrategy`** (`src/utils/LoadBalancingStrategy.h` / `LoadBalancingStrategy.cpp`). Settings, CLI, and the UI **do not** select `static` / `hybrid` / `dynamic` / `interleaved` / `work_stealing` anymore.

**Where to read about search today:** [PARALLEL_SEARCH_ENGINE_DESIGN.md](PARALLEL_SEARCH_ENGINE_DESIGN.md).

**Archived detail:** The former interleaved design write-up is in `internal-docs/design/2026-04-11_INTERLEAVED_LOAD_BALANCING_STRATEGY_ARCHIVED.md`. Earlier revisions are also available in git history.
