---
name: Tech Debt
description: Tackle technical debt in existing C++ software via inventory, risk triage, regression safety net, and incremental SOLID fixes
---

# Tech Debt - Tackle Technical Debt in Existing Software

Use when refactoring legacy code, fixing fragility/leaks/races/duplication, or improving maintainability without behavior change.

## Principles (source of truth)

SOLID as baseline:
- Single Responsibility, Open/Closed, Liskov Substitution, Interface Segregation, Dependency Inversion.
Complementary: DRY (one source of truth), KISS, YAGNI, composition over inheritance, Law of Demeter, fail fast, explicit over implicit.

C++17 specifics:
- RAII, `std::vector` / `std::unique_ptr` / `std::shared_ptr`; never `new/delete/malloc/free`, no C-casts.
- `const` correctness, `std::string_view` for read-only params, `(std::min)/(std::max)`, `std::scoped_lock`, `explicit` single-arg ctors.
- No raw `T*` across threads; use `shared_ptr` snapshot. No dangling `string_view` into mutable/pooled storage.
- Max 3-level nesting, early returns, no empty `catch`, no I/O under lock.

## Workflow

1. **Inventory (graph first):**
   - Prefer codebase-memory-mcp: `search_graph`, `trace_path`, `get_code_snippet`, `query_graph`, `get_architecture`. Fall back to `grep` only for literals/configs.
   - List debt items with file:line: fragility, ownership/lifetime, concurrency, duplication, naming, missing tests.
2. **Triage by risk:**
   - Order: crash/data-loss > leak/use-after-free/dangling view > race/deadlock > untestability/duplication > style.
   - Record cost-of-delay vs cost-to-fix. Pick one item per iteration.
   - For every proposed action, always list Pros, Cons, and Risks in plain words before acting.
3. **Safety net first:**
   - Add characterization/regression test reproducing the defect before changing code.
   - macOS entrypoint only: `./scripts/build_tests_macos.sh`. Single case: `./build_tests/<target> -tc="<CaseName>"`.
4. **Fix incrementally (one hypothesis, one commit):**
   - Minimal targeted fix before broad refactor. Boy Scout Rule: leave touched code cleaner.
   - Refactor patterns: extract helpers, replace `#ifdef` hacks with platform-agnostic abstraction, replace raw owning pointers, add `const`, enforce ownership map.
   - Preserve `GuiState`-style write confinement if applicable: workers to staging buffers, commit on owner thread.
5. **Verify:**
   - Re-run target tests + full suite. Run `scripts/run_clang_tidy.sh` or `scripts/pre-commit-clang-tidy.sh` for changed files.
   - Check `python3 scripts/find_class_struct_mismatches.py`. No new Sonar issues.
   - Record outcome: repro passes/fails, dump change, new evidence.
6. **Prevent recurrence:**
   - Add assertion, lint suppression only with justification, or standard in `docs/standards/`.
   - Update `docs/` if contributor-facing, else `internal-docs/`.

## Do not

- Broad rewrites without tests.
- Behavior change + refactor in same commit.
- Changing platform `#ifdef` blocks to "look cross-platform" instead of abstracting.
- Running `cmake/make/clang++` directly on macOS.

## Answer style

- Use simple everyday words. Avoid technical jargon; when a technical term is unavoidable, explain it in one short plain phrase.
- Keep answers short and clear.
