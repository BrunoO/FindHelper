# AGENTS document Additions from Sonar Issue Session (2026-02-01)

**Date:** 2026-02-01  
**Context:** After fixing Sonar open issues in this session, we identified instructions that were missing or under-specified in AGENTS document. This document records what was added so the same classes of issues are prevented in the future.

---

## Issues Fixed in This Session (Summary)

| Sonar Rule | Description | Files Fixed |
|------------|-------------|-------------|
| S954 | Move #include to top of file | SearchResultUtils.h |
| S3230 | In-class initializer instead of ctor initializer list | StreamingResultsCollector |
| S5997 | Replace std::lock_guard with std::scoped_lock | StreamingResultsCollector.cpp |
| S6012 | Use CTAD (no explicit template args) | StreamingResultsCollector.cpp |
| S1709 | Add explicit to constructor | StreamingResultsCollector.h |
| S1172 | Unused parameter: remove / unnamed / [[maybe_unused]] | ParallelSearchEngine.h, TestHelpers.cpp |
| S1066 | Merge nested if statements | ParallelSearchEngine.h |
| S6004 | Init-statement for variable in if | CommandLineArgs.cpp, ResultsTable.cpp |
| S2486 | Handle exception or don't catch | SearchWorker.cpp |
| S2738 | Catch specific exception type | SearchWorker.cpp |
| S108 | Empty compound statement (empty catch) | SearchWorker.cpp |
| S1181 | Catch more specific exception | (first catch block in SearchWorker) |

---

## Instructions That Were Missing or Weak in AGENTS document

1. **Include order: all at top; windows.h case**  
   - **Gap:** Rule said "standard C++ include order" but did not explicitly forbid `#include` in the middle of the file (e.g. before inline functions). No mention of `<windows.h>` vs `<Windows.h>`.  
   - **Added:** In "C++ Standard Include Order" → Special Cases: "All #include directives must appear in the top block of the file"; "Use lowercase <windows.h> (not <Windows.h>)" (S954, S3806).

2. **Explicit single-argument constructors (S1709)**  
   - **Gap:** "Use explicit for Conversion Operators" mentioned "single-argument constructors" in When to Apply but only showed `operator bool()` examples.  
   - **Added:** Section retitled to "Use explicit for Conversion Operators and Single-Argument Constructors (cpp:S1709)" and a constructor example added (StreamingResultsCollector-style).

3. **std::scoped_lock vs std::lock_guard (S5997, S6012)**  
   - **Gap:** No rule at all.  
   - **Added:** New subsection "Prefer std::scoped_lock Over std::lock_guard (cpp:S5997, cpp:S6012)" with examples and When to Apply / When NOT to Apply.

4. **Empty catch when draining resources (S2486, S108)**  
   - **Gap:** "Handle Exceptions Properly" said "never empty catch" but did not spell out the pattern for draining (e.g. futures).  
   - **Added:** Bullet "When draining resources (e.g. std::future::get()): do not use empty catch(...); catch std::exception first and log, then catch(...) and log."

5. **Quick Reference and checklist**  
   - **Gap:** No Quick Reference row for explicit constructors, scoped_lock, or include-at-top / windows.h.  
   - **Added:** Quick Reference rows for "Explicit single-argument constructors", "Prefer std::scoped_lock", "Include order: all at top". Questions to Ask Yourself updated with 14–16 (explicit, scoped_lock, includes/windows.h) and 24 (exceptions + empty catch).

---

## References

- **AGENTS document** – Updated sections: C++ Standard Include Order, Use explicit (Conversion Operators and Single-Argument Constructors), new Prefer std::scoped_lock, Handle Exceptions Properly, Quick Reference, Questions to Ask Yourself.
- **Sonar plan:** `docs/plans/2026-02-01_SONAR_OPEN_ISSUES_FIX_PLAN.md`.
