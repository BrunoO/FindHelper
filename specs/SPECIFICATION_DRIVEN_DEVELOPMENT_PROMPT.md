# Specification-Driven Development Prompt (USN_WINDOWS / FindHelper)

**Purpose:** A step-by-step prompt to develop features using structured, executable specifications. Customized for this project's rules, tooling, and context. Use with Cursor Agent/Composer for implementation from the spec.

**Date:** 2026-02-20

---

## Copy-paste prompt (fill the bracketed parts, then start with Step 1)

```
You are a senior software architect specializing in structured specification development for this C++ project. Guide me through creating a comprehensive, executable specification for [BRIEF FEATURE DESCRIPTION].

**Project context (USN_WINDOWS / FindHelper):**
- Cross-platform file search app: **macOS** (main dev), **Windows** (primary target), **Linux** (secondary). ImGui GUI; C++17 only (no older, no newer).
- Quality: clang-tidy + SonarQube; invariants and assertions in code; DRY constants (see docs/analysis/2026-02-20_DRY_CONSTANTS_ANALYSIS.md). No backward compatibility required.
- Build/verify: On macOS use `scripts/build_tests_macos.sh` only; do not run cmake/make/clang++ directly unless the task explicitly requires it. Windows: CMake + optional PGO; see AGENTS.md "Modifying CMakeLists.txt Safely".
- Standards: `AGENTS.md` (always applied), `docs/standards/CXX17_NAMING_CONVENTIONS.md`, `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md`. Production checklist: `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md`.

Follow this exact step-by-step process without skipping or assuming. After each step, **pause for my input** before proceeding.

---

### Step 1 — REQUIREMENTS GATHERING

Ask clarifying questions about:

- **Functional / non-functional:** Behavior, inputs/outputs, error cases, performance expectations (this project favors speed over memory; no extra allocations in hot paths).
- **Constraints:** C++17 only; macOS/Linux/Windows; no changes inside `#ifdef _WIN32` / `__APPLE__` / `__linux__` to "unify" code (use platform-agnostic abstractions with separate implementations if needed). Windows rules: `(std::min)`/`(std::max)`, explicit lambda captures in templates (no `[&]`/`[=]`), forward declaration match (struct vs class).
- **Integrations:** CMake, clang-tidy (`.clang-tidy`), SonarQube, existing scripts (e.g. `scripts/build_tests_macos.sh`), MCP/skills if relevant (e.g. browser, SonarQube usage). Constants: prefer `settings_defaults` (Settings.h), `file_time_constants` (FileTimeTypes.h), `file_system_utils_constants` (FileSystemUtils.h) — one constant per purpose (DRY).
- **ImGui (if UI):** Immediate mode; all ImGui on main thread; popup rules: same window context for OpenPopup/BeginPopupModal, SetNextWindowPos every frame before BeginPopupModal, close button outside CollapsingHeader with SetItemDefaultFocus. See AGENTS.md "ImGui Popup Management Rules".
- **Security / robustness:** Input validation, exception handling (specific catches, no empty catch blocks), logging (`LOG_ERROR_BUILD` / `LOG_WARNING_BUILD`).

Output: Short summary of assumptions and a list of open questions. **Pause for my answers.**

---

### Step 2 — USER STORIES

Generate **5–10 prioritized user stories** in Gherkin (Given/When/Then). Include:

- Happy path and main edge cases (empty input, bounds, errors).
- Invariants where relevant (e.g. preconditions/postconditions, state not corrupted).
- References to project rules where they affect behavior (e.g. "Given the app follows AGENTS.md platform rules …").

Present in a **Markdown table** (e.g. ID, priority, one-line summary, Gherkin snippet). **Pause for my feedback.**

---

### Step 3 — ARCHITECTURE OVERVIEW

Outline:

- **High-level design:** Components, interfaces, data flow. One responsibility per class/function (SOLID); prefer minimal, clear abstractions (KISS, YAGNI, DRY).
- **C4 architecture views (for anything beyond a trivial change):**
  - **Level 1 — System Context:** Where this feature or subsystem fits in the overall product; what people and external systems interact with it.
  - **Level 2 — Container:** Which runtime/deployable units (desktop app, background worker, indexing component, databases, queues, external tools) are involved and how they communicate.
  - **Level 3 — Component:** For any container you touch in a non-trivial way, show its main internal components (services, controllers, workers, repositories, caches) and their responsibilities/relationships.
  - Keep C4 views **text-based and versionable** (e.g. short Markdown lists or simple ASCII/PlantUML/Structurizr-style sketches) rather than image-only diagrams. Make sure your narrative explicitly says which parts correspond to L1/L2/L3.
- **Patterns:** RAII for resources; `std::string_view` for read-only string parameters; `const T&` for read-only refs; in-class initializers for default member values; no C-style arrays (use `std::array`/`std::vector`); safe string helpers (`strcpy_safe`/`strcat_safe` from StringUtils.h where applicable).
- **Platform:** Where behavior differs by OS, describe the abstraction and per-platform implementation, not changes inside existing `#ifdef` blocks.
- **Threading and robustness:** If threads are involved, state what runs on the main thread (e.g. ImGui) vs workers, how results are synchronized, and which **invariants** the design relies on (ownership, lifetimes, state transitions).

Output: Short narrative + optional diagram (e.g. Mermaid). **Pause for my input.**

---

### Step 4 — ACCEPTANCE CRITERIA

For each story (or group), list **measurable** criteria:

- **Behavior:** Observable outcomes (e.g. "Search returns results within X ms under Y conditions").
- **Code invariants:** Pre/postconditions or assertions you will add (e.g. loop invariants, state checks). Explicitly **enumerate additional invariants** discovered during the spec (e.g. data structure relationships, state-machine rules, threading constraints) and specify **where** they will be enforced (types vs runtime checks, API boundaries, promotion/swap points).
- **Quality gates:** No new SonarQube or clang-tidy violations; preferred style applied so one fix satisfies both (in-class init, const ref, no `} if (` on one line — see AGENTS.md "Clang-Tidy and SonarQube Alignment"). If suppression is unavoidable, `// NOSONAR` on the **same line** as the issue only.
- **Tests:** Unit tests (doctest) where appropriate; coverage expectations if set (e.g. >90% for new code).
- **Duplication:** No new duplication; reuse existing constants/helpers (DRY). See `docs/analysis/2026-02-20_DRY_CONSTANTS_ANALYSIS.md` for where to add shared constants.

Present in a **table** (story ID, criterion, measurable check). **Pause for my confirmation.**

---

### Step 5 — TASK BREAKDOWN

Create a **phased task list** (e.g. spec validation → prototype → tests → review → integration). For each task, ensure it is a **small, logically coherent step** that:

- Leaves the codebase in a **buildable, shippable** state.
- Is small enough for a human to review in isolation.

For each task:

- Clear, actionable description.
- Dependencies (e.g. "after Step 4 approved").
- **Effort estimate in hours** (rough).
- Reference to scripts/tools (e.g. "Run `scripts/build_tests_macos.sh` on macOS after code changes").
- Brief **acceptance criteria** and tests to run for that task (unit, integration, ImGui regression, manual smoke tests), so the implementing agent can decide when it is safe to move to the next step.

Use a **Markdown table** (phase, task, deps, estimate, notes). **Pause for my approval.**

---

### Step 6 — RISKS & MITIGATIONS

Identify:

- **Technical risks:** e.g. thread safety, performance regressions, Windows/MSVC quirks (lambda captures in templates, forward declarations), ImGui popup lifecycle.
- **Process risks:** Scope creep, unclear acceptance.
- **Mitigations:** Concrete actions (e.g. "Add assertions at boundaries"; "Run clang-tidy on changed files"; "Verify no new Sonar issues for changed files").

Present as a short table (risk, impact, mitigation). **Pause for my input.**

---

### Step 7 — VALIDATION & HANDOFF

Propose:

- **Review checklist:** Aligned with `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` (quick and/or comprehensive as appropriate). Include: no new Sonar/clang-tidy issues, preferred style, no changes inside platform `#ifdef` blocks, no extra allocations in hot paths, no new duplication.
- **How to use this spec with Cursor Agent/Composer:** Spell out how the implementation prompt should look, for example:
  - Treat this spec as the **single source of truth** for the feature.
  - In the implementation prompt, explicitly instruct the agent to:
    - **Respect project constraints:** Read and follow `AGENTS.md`, `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md`, this spec, and relevant standards; use C++17 only; obey platform and Windows-specific rules; run `scripts/build_tests_macos.sh` on macOS after C++ changes.
    - **Work in small, reviewable steps:** Implement according to the Step 5 task breakdown, keeping each step small, self-contained, and leaving the code in a buildable, reviewable state. Require that the agent describe the goal, impacted files/functions, and acceptance criteria/tests for each step and not move on until those are satisfied.
    - **Identify and encode invariants:** Use the invariants from Step 4 plus any additional invariants discovered during implementation; enforce them via types where possible and via debug assertions at key boundaries (API entry/exit, state transitions, promotion/swap points, loop invariants).
    - **Be explicit about failure modes and threading:** Enumerate key failure modes for this feature (I/O errors, invalid input, timeouts, concurrency issues) and state how they are detected and handled (log, degrade gracefully, fail fast). Reiterate what runs on the main thread vs workers, and how state is synchronized.
    - **Preserve observability and testability:** Add or extend logging and lightweight metrics where they help debug this feature, and ensure the test plan from Step 4 (unit, integration, UI/regression) is implemented.
    - **Stick to KISS / YAGNI / DRY and SOLID:** Prefer the simplest design that fulfills the spec; avoid speculative abstractions; eliminate duplication (especially constants and helper logic); keep each new helper/class with a single, clear responsibility.
- **Strict constraints to paste into the implementation prompt:** Include the "Strict Constraints / Rules to Follow" block from `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md` (or the one-line reminder from that file) so the implementing agent respects Sonar/clang-tidy, platform rules, and verification steps.
- **Maintain "What's new" in the Help window:** When the feature is shipped, add a single high-level bullet to the "What's new" section in `src/ui/SearchHelpWindow.cpp` with the **date (YYYY-MM-DD)** — use the **git commit date** for that feature (e.g. `git log -1 --format="%ad" --date=short -- path/to/file`). Keep only the **last calendar month** of entries; remove older bullets so the list stays scannable.

Output in **Markdown** with clear sections. Use **tables** for stories, tasks, and risks. After each step, **pause for my input** before proceeding.

Start with **Step 1** now. [BRIEF FEATURE DESCRIPTION]
```

---

## Optional: TLA+-guided specification extension

Use this **in addition** to the 7-step process when the feature involves non-trivial state machines, concurrency, caching, or subtle invariants (e.g. SearchWorker protocols, auto-refresh, streaming collectors). Paste the block below *after* agreeing on the high-level spec (Steps 1–3) to derive a small TLA+ model and a focused list of invariants that should feed back into Step 4 (acceptance criteria) and implementation assertions.

```
You are a senior software architect and formal methods engineer.

Your task is to read the system description I provide and produce:
1. A concise natural-language specification of the system behavior and goals.
2. A list of key safety and liveness properties.
3. A high-level TLA+ specification capturing the system behavior (state variables, Init, Next, invariants).
4. Optional: refinement notes linking the TLA+ spec to the implementation.

Follow these instructions:

1. Understand the input
   - Carefully extract:
     - Main components (services, processes, threads, clients/servers, etc.).
     - Shared state (data structures, messages, persistent storage).
     - Events/actions (API calls, messages, timeouts, retries, state transitions).
     - Fault model (crashes, network partitions, message loss/reorder, etc., if any).
   - If the description is incomplete or ambiguous, clearly state your assumptions in a bullet list before writing the TLA+ spec.

2. Natural-language specification
   Produce a short, structured spec:
   - One-paragraph overview: what the system is supposed to do.
   - Then bullet lists for:
     - Interface: externally visible operations (inputs/outputs).
     - State: key abstract state the system maintains.
     - Behavior: informal description of how state evolves on each operation or event.
   Keep this at the level of observable behavior, not implementation details.

3. List properties to verify
   Explicitly enumerate:
   - Safety properties (what must never happen), e.g.:
     - "No two leaders at the same term."
     - "Balance never becomes negative."
     - "A read never returns a value that was not previously written."
   - Liveness properties (what must eventually happen), e.g.:
     - "Every client request is eventually either acknowledged or rejected."
     - "If a node continuously tries to acquire a lock, it eventually succeeds."
   Write each as a short, precise sentence. Where possible, phrase them as invariants or temporal properties ("always", "eventually", "infinitely often").

4. TLA+ specification structure
   Produce a TLA+ module with this canonical structure:
   - A `MODULE` header and necessary `EXTENDS` (e.g. `Naturals`, `Sequences`, etc.).
   - A declaration of `VARIABLES` for the abstract state.
   - A definition `vars == << ... >>` grouping the variables.
   - `Init` predicate:
     - Describe all allowed initial states.
     - Use type invariants (e.g. `x \in Nat`, `msgs \subseteq Messages`) and initial conditions.
   - `Next` action:
     - A disjunction of sub-actions, one per kind of step (e.g. `Request`, `Respond`, `Timeout`, `Crash`, `Recover`).
     - Each sub-action:
       - Has a clear name, e.g. `Request(req)`, `Commit`, `Timeout(node)`.
       - Uses primed variables (`x'`) to describe the next state.
       - Preserves unchanged variables with `UNCHANGED << ... >>`.
   - Overall behavior:
     - `Spec == Init /\ []Next_vars`
     - Add liveness constraints as needed, e.g. fairness:
       - `Spec == Init /\ []Next_vars /\ WF_vars(Next)`
   - Invariants:
     - For each safety property, define a TLA+ formula, e.g. `TypeOK`, `NoDoubleSpend`, `AtMostOneLeader`.
     - List them in a set or as separate definitions with comments.
   Use idiomatic TLA+ style: abstract state, simple expressions, and avoid encoding implementation details unless necessary.

5. Link between code and TLA+ (optional but encouraged)
   - Add a short section "Implementation mapping" that explains:
     - How each TLA+ variable corresponds to data structures or modules in the implementation.
     - How each `Next` sub-action corresponds to functions, RPC handlers, or transitions in the code.
   - Do not try to generate code; just explain the mapping so future invariants can be wired into assertions and tests.

6. Output format
   Produce your answer in this order:
   1. "Assumptions and scope" (if any).
   2. "Informal specification" (section 2).
   3. "Safety and liveness properties" (section 3).
   4. "TLA+ specification" – one complete module, ready to paste into the TLA+ Toolbox.
   5. "Implementation mapping" (section 5), with explicit notes on which invariants should become debug assertions, type-level guarantees, or test expectations in the USN_WINDOWS codebase.
   Use clear headings and code blocks for TLA+.
```

---

## How to use

1. Copy the block above into a new chat (or Composer).
2. Replace `[BRIEF FEATURE DESCRIPTION]` with your feature (e.g. "Add assertion helpers for loop invariants", "New size-range filter in search UI").
3. Run Step 1; answer the architect's questions when it pauses.
4. Proceed step by step, only moving on after you confirm each step.
5. When implementation starts, give the agent the final spec plus the Strict Constraints from `AGENT_STRICT_CONSTRAINTS.md` and point to `AGENTS.md`.

---

## References (for the architect and the implementing agent)

| Topic | Reference |
|-------|-----------|
| Project rules (always applied) | `AGENTS.md` |
| Strict constraints for task prompts | `internal-docs/prompts/AGENT_STRICT_CONSTRAINTS.md` |
| Naming | `docs/standards/CXX17_NAMING_CONVENTIONS.md` |
| DRY constants | `docs/analysis/2026-02-20_DRY_CONSTANTS_ANALYSIS.md` |
| Clang-tidy vs Sonar (avoid double work) | `internal-docs/analysis/2026-02-01_CLANG_TIDY_VS_SONAR_AVOID_DOUBLE_WORK.md` |
| Production checklist | `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` |
| Task prompt structure | `internal-docs/prompts/TaskmasterPrompt.md` |
| ImGui paradigm | `docs/design/IMGUI_IMMEDIATE_MODE_PARADIGM.md` (if UI) |
| Help window "What's new" | `src/ui/SearchHelpWindow.cpp` — add bullet with git commit date (YYYY-MM-DD) when shipping a user-facing feature; keep last calendar month only |

---

## Optional: one-line starter

For a minimal kickoff:

```
Guide me through a spec for [FEATURE]. Project: USN_WINDOWS/FindHelper, C++17, macOS dev / Windows target. Follow the 7-step spec process in specs/SPECIFICATION_DRIVEN_DEVELOPMENT_PROMPT.md; pause after each step. Start with Step 1 (requirements gathering).
```
