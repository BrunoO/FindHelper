# Task: Competitive Research and High-Value Feature Opportunities

**Date:** 2026-02-02  
**Source:** Taskmaster prompt; goal = based on this project's current features and limitations (comprehensive list), search the web for similar tools, research their main strengths and weaknesses, and determine the best opportunities for adding highly valued new features to our project.  
**Note:** The executing agent will **not** have access to the project codebase or repository. All context required is contained in this prompt.

---

## Summary

The user wants a **research and strategy task** for an agent that **does not have access to the project**. The prompt is self-contained: (1) it provides a **full project baseline** (features and limitations) inline; (2) the agent must **search the web** for similar tools and research their strengths and weaknesses; (3) the agent must **identify 5–10 high-value feature opportunities** with rationale and priority. The deliverable is a **single markdown document** produced as the task output. The agent uses web search and any available tools to gather information, then outputs the complete research document.

***

```markdown
# Prompt: Competitive Research and High-Value Feature Opportunities

## Important: No Access to Project

**You do not have access to the project repository, codebase, or any project documents.** Everything you need to complete this task is in this prompt. Use the "Project Baseline" section below as the only source of truth for what the product (FindHelper) is and is not today. Do not assume you can read README, docs, or source code.

## Mission Context (The "Why")

FindHelper is a cross-platform file search application (Windows primary, macOS and Linux secondary) with a modern ImGui GUI, AI-assisted search (e.g. Gemini API), and real-time indexing on Windows via the USN Journal. The product team wants to decide which new features to build next. They need: (1) a clear, shared picture of current capabilities and gaps; (2) how similar tools compare; and (3) a prioritized list of high-value opportunities. Your job is to research the landscape and produce that analysis. You will not implement anything—only deliver a structured research document.

## Core Objective (The "What")

1. **Use the project baseline** provided below as the authoritative description of FindHelper's current features and limitations (you may conservatively extend it; do not remove or contradict it).
2. **Search the web** for similar tools (desktop file search, index-based finders, power-user file managers, Everything-like tools, Spotlight alternatives, etc.).
3. **Research each competitor's main strengths and weaknesses** (functionality, performance, UX, platform support, limitations).
4. **Identify 5–10 high-value feature opportunities** for FindHelper, with a short rationale and suggested priority (high/medium/low) for each.

**Deliverable:** One structured markdown document containing the baseline (as you use it), the list of similar tools, competitor analysis, and the prioritized opportunities. Output the complete document as your task result (e.g. in your response or as a file in your workspace). No implementation or code changes.

## Desired Outcome (The "How it Should Be")

Your deliverable must be a **single markdown document** with these four parts:

1. **Project baseline** – The list of current features and current limitations you used (copy or summarize from the section below; you may add brief clarifications if needed). This gives readers a precise picture of what FindHelper does and does not do today.
2. **Similar tools** – A list of comparable products: name, platform(s), one-line description, and URL or source where you found the information.
3. **Competitor analysis** – For each shortlisted tool: main strengths, main weaknesses, and how they compare to FindHelper (what they do better, what FindHelper does better, and clear gaps or differentiators). Prefer facts and citations; label any inferences.
4. **High-value feature opportunities** – 5–10 opportunities, each with: a short name, one paragraph explaining why it is high value and how it relates to competitor gaps or user needs, and a suggested priority (high/medium/low). No implementation details.
Additional requirements:
- **Factual and traceable:** Cite or link sources where possible; if something is your inference, say so.
- **Scope:** Opportunities should fit the product's scope: cross-platform (Windows primary, macOS/Linux secondary), desktop app with ImGui, performance-sensitive search, target users = power users / developers / system administrators. The stack is C++17; there is no requirement for backward compatibility.

## Project Baseline: Current Features and Limitations (Your Source of Truth)

Use this section as the **only** description of FindHelper. Do not assume any feature or limitation not listed here. You may extend or refine the list conservatively (e.g. add one or two items that clearly follow from the description); do not remove or contradict any item.

### Current Features

**Product & platform**
- Cross-platform file search application: **FindHelper** (primary target Windows, secondary macOS and Linux).
- GUI: **ImGui (Dear ImGui)** immediate-mode UI; Windows: DirectX 11 + GLFW; macOS: Metal + GLFW; Linux: OpenGL 3 + GLFW.
- Build: CMake, C++17; tests: doctest; optional Boost, optional PGO (Windows).

**Indexing & monitoring**
- **Windows:** Real-time file system monitoring via **USN Journal** (when running with administrator rights); volume-level monitoring; event types: create, delete, rename, modify, close; privilege reduction after acquiring volume handle (drops unnecessary privileges, retains SE_BACKUP for USN).
- **Windows without elevation:** Runs as standard user (`asInvoker`); uses **folder crawling** instead of USN; no real-time monitoring.
- **macOS / Linux:** **Folder crawling** only (no USN equivalent enabled yet); no real-time monitoring on Linux (inotify/FAM not integrated).
- Initial index population: crawl of selected folder(s)/roots; optional MFT metadata reading on Windows (size/mod time during population) when `ENABLE_MFT_METADATA_READING` is on.

**Search**
- **Manual search:** Full-text substring search over **filenames** (path/filename matching); case-sensitive and case-insensitive; **regex** support; extension filters; path/pattern filters (e.g. path patterns, filename patterns).
- **Parallel search engine:** Multi-threaded search with load balancing (Static, Hybrid, Dynamic, Interleaved); SoA (Structure of Arrays) path storage; zero-copy read-only views; AVX2 substring search when available; pattern matchers created once per thread; cancellation support.
- **Streaming results:** Incremental display of results while search runs (batching by size/time); final handoff with sort and filters applied to full set.
- **Lazy attributes:** File size and modification time loaded on demand (lazy I/O) with caching; optional MFT-based reads on Windows.
- **"Show All" path:** Can show all indexed files (e.g. empty query); currently serial (ForEachEntry) for large indices.

**AI-assisted search**
- **Gemini API integration:** AI-assisted search (e.g. natural language → search config); external API calls; configurable prompts; AI description can be associated with search (saved searches + AI description is a planned idea, not fully implemented).
- **Planned/explored:** Copilot plugin, clipboard prompt generation, local LLM experiments (e.g. Gemma), direct Gemini API from app.

**UI & UX**
- Search input, filters (extension, path, size, date), results table; open file/folder; drag-and-drop (Windows implemented; macOS parity in progress); keyboard shortcuts; metrics window (gated by `--show-metrics`); popups (help, keyboard shortcuts, delete confirmation, etc.).
- Saved searches (feature exists; “save AI description with search” and dedicated saved-search window are ideas).
- Target users: power users, developers, system administrators.

**Architecture & quality**
- Component-based **FileIndex** (PathStorage, FileIndexStorage, PathBuilder, LazyAttributeLoader, ParallelSearchEngine); **ISearchableIndex** interface; thread-safe shared_mutex model; no friend classes for search.
- Code quality: AGENTS document, SonarQube, clang-tidy; production checklist; multiplatform coherence (e.g. `#endif` comments, shared `FileOperations.h`).

**Security**
- Runs as standard user by default; optional elevation on Windows only for USN; privilege dropping after init; fatal exit if privilege drop fails; path validation before ShellExecute; input validation and error handling.

### Current Limitations

**Platform & indexing**
- **Linux:** No real-time monitoring (inotify/FAM not integrated); folder crawl only; Linux support is “bring to parity” (build exists, behavior/UX not fully at parity).
- **macOS:** No USN equivalent; folder crawl only; no real-time monitoring; drag-and-drop parity with Windows in progress; BasicFinder-style fallback for non-admin users is an idea.
- **Windows:** Without elevation, no USN (folder crawl only); no two-process privilege separation (unprivileged UI + privileged USN service) yet.

**Search & performance**
- **LoadSettings in hot path:** Search can trigger 2–3 synchronous file I/O calls per search when optional_settings is nullptr (to be removed/cached).
- **Extension filter allocations:** Extension set lookups may allocate in inner loop (heterogeneous lookup / transparent hasher not yet used).
- **Post-processing:** O(N) GetEntry(id) per result for metadata (size/time); no bulk retrieval or SoA metadata in hot path.
- **Regex cache:** ThreadLocalRegexCache has no LRU eviction (unbounded growth with many unique regexes).
- **"Show All":** Serial ForEachEntry for large indices (parallel “match everything” is a backlog item).
- **Full-path search:** Removed from production (filename-focused search only in current design).

**UX & product**
- No dedicated “saved searches” window with AI description visible; “classic vs AI” panel is an idea.
- Metrics visible only with `--show-metrics` (intentional).
- UIRenderer is a large module; decomposition is backlog.
- No built-in Copilot plugin or clipboard prompt generation yet.

**Code & process**
- Sonar: some open issues (Phases 1, 2, 4 in zero-open-issues plan); exception handling in search lambdas to be wrapped; naming convention audit pending; clang-tidy warnings in many files.
- PGO and test targets: test targets sharing sources with main executable must use matching PGO compiler flags (documented in AGENTS document).

**Optional / future**
- Privilege dropping Option 2 (two-process model) is long-term; SIMD is_deleted/is_directory scan; PathPatternMatcher continuation; Linux full parity; Logger streaming interface; docs reorganization (internal vs public).

## Visual Workflow (Mermaid)

    ```mermaid
    graph TD
        A[Start] --> B[Use project baseline from this prompt]
        B --> C[Search web for similar tools]
        C --> D[Shortlist comparable products]
        D --> E[Research each: strengths & weaknesses]
        E --> F[Compare to FindHelper gaps/differentiators]
        F --> G[Identify high-value feature opportunities]
        G --> H[Prioritize and write rationale]
        H --> I[Write structured research document]
        I --> J[Output the document as task result]
    ```

## The Process / Workflow

1. **Baseline**
   - Use the "Project Baseline: Current Features and Limitations" section above as your only source of truth. You do not have access to any project files. You may conservatively extend the list (e.g. add one or two items that clearly follow from the description). Do not remove or contradict the listed features/limitations.

2. **Search for similar tools**
   - Use web search (and MCP/browser tools if available) to find tools that are comparable to FindHelper: desktop file search, index-based file finders (e.g. Everything, Listary, Spotlight, WizTree, fsearch, locate, etc.), power-user file managers with search, and AI-assisted file search if any are known. Prefer recent, verifiable sources (official sites, reviews, comparisons). Record: name, platform(s), one-line description, and URL/source.

3. **Shortlist**
   - Choose a manageable set (e.g. 5–12) that best overlap with FindHelper’s space (fast index-based search, optional real-time indexing, filters, cross-platform or Windows-first). Include at least one strong Windows incumbent (e.g. Everything) and at least one cross-platform or macOS/Linux alternative.

4. **Competitor analysis**
   - For each shortlisted tool, research and document: main strengths, main weaknesses, and how they compare to FindHelper (what they do better, what FindHelper does better, and clear gaps or differentiators). Prefer facts and citations; label inferences.

5. **High-value opportunities**
   - From competitor gaps, user needs (power users, devs, admins), and the project’s limitations list, identify 5–10 **high-value feature opportunities**. For each: short name, one-paragraph rationale (why high value, link to competitor gap or limitation), and suggested priority (high/medium/low). No implementation; focus on product value and fit with C++17, ImGui, cross-platform, performance-sensitive scope.

6. **Deliverable**
   - Produce a single structured markdown document with the four sections. Output the complete document as your task result (e.g. in your response or as a file in your workspace). If you save a file, use a filename with date prefix YYYY-MM-DD_. You do not have access to the project repository. If you need a filename example: 2026-02-02_COMPETITIVE_RESEARCH_AND_FEATURE_OPPORTUNITIES.md. Do not change application code or build config.

## Anticipated Pitfalls

- **Scope creep:** This task is research and a written deliverable only. Do not implement features, change CMake, or modify application code.
- **Bias:** Rely on verifiable sources and competitor documentation/reviews; clearly mark inferences vs facts.
- **Ignoring baseline:** The features/limitations list is the project’s current state; do not assume features that are only planned (e.g. Linux inotify, two-process privilege model) as already present.
- **Vague opportunities:** Each opportunity must have a clear rationale tied to competitor gap or limitation and a suggested priority.
- **Wrong audience:** Opportunities should fit power users, developers, and system administrators on Windows (primary) and macOS/Linux (secondary), and the existing tech stack (C++17, ImGui, no backward compatibility requirement).

## Acceptance Criteria / Verification Steps

- [ ] Project baseline section is present and matches (or conservatively extends) the comprehensive features and limitations list provided in this prompt.
- [ ] Similar tools are listed with name, platform(s), one-line description, and source/URL where possible.
- [ ] At least one strong Windows incumbent and at least one cross-platform or non-Windows alternative are included.
- [ ] For each shortlisted competitor: main strengths, main weaknesses, and comparison to FindHelper are documented.
- [ ] 5–10 high-value feature opportunities are listed with: short name, rationale (one paragraph), and suggested priority (high/medium/low).
- [ ] Deliverable is a single markdown document provided in full as the task output (or as a saved file with date prefix YYYY-MM-DD_ in the filename).
- [ ] No application code, CMake, or build configuration changes were made (you do not have repo access).
- [ ] Document is factual and traceable (citations/sources where possible; inferences labeled).

## Strict Constraints / Rules to Follow

**Scope**
- **Research and document only.** Do not implement features, add dependencies, or modify source code, CMakeLists.txt, or build scripts. Do not run build or compile commands unless the task explicitly required it (this task does not).

**Baseline**
- Use the "Project Baseline: Current Features and Limitations" list in this prompt as the authoritative baseline. You have no access to project files. You may extend or refine the list conservatively; do not remove or contradict the listed items.

**Quality**
- For the research document: use clear headings, short paragraphs, and citations/sources where possible. Use technical terms as defined in the baseline (e.g. C++17, ImGui, USN Journal). Prefer no code examples in the deliverable.

**No code or repo access**
- This task does not touch code. You do not have access to the project repository, CMake, or source files. Do not assume you can read or write any project paths.

**Verification**
- Before considering the task complete: confirm the deliverable has all four sections (baseline, similar tools, competitor analysis, opportunities) and that the complete document is provided as your output.

## Reminder: No Project Access

You do not have access to the project repository, codebase, or any project documents. All context you need is in this prompt. The "Project Baseline" section is the only source of truth for FindHelper's current features and limitations. Use web search (and any other tools available to you) to research similar tools and produce your analysis. Output your final markdown document as your task result.

Proceed with the task.
```
