# Open Sonar Issues (Current)

**Date:** 2026-02-03  
**Source:** SonarCloud project `BrunoO_USN_WINDOWS`  
**Last scanner run:** After commit 696ba0b (fix/suppress 10 issues)  
**Total OPEN:** 1 issue (server may still be processing; re-check in a few minutes)

---

## By file

### src/crawler/FolderCrawler.cpp (1)
| Rule    | Line | Severity | Message |
|---------|------|----------|---------|
| cpp:S6004 | 547 | MINOR | Use the init-statement to declare "error" inside the if statement. |

This line already has `// NOSONAR(cpp:S6004)` (variable is used after the if in `LOG_WARNING_BUILD`). If the issue stays OPEN after processing, try placing only `// NOSONAR` at end of line; the CFamily analyzer may not recognize the rule-specific form.

---

## Resolved by commit 696ba0b (now CLOSED)

- **FileIndexMaintenance.cpp:** S1481/S1854 (threshold_abs, threshold_pct) – inlined into LOG
- **LazyAttributeLoader.cpp:** S1481/S1854 (load_count, total_time) – NOSONAR (used in macro)
- **MetricsWindow.cpp:** S1186 – nested comment added in empty stub

---

## By rule (summary)

| Rule     | OPEN | Status |
|----------|------|--------|
| cpp:S6004 | 1  | FolderCrawler:547 – NOSONAR present; may clear after server processing |

---

## Previous resolutions (earlier analysis)

- FolderCrawler: S3923, S1871
- IndexOperations.h: S1068
- PathPatternMatcher, SearchWorker, StdRegexUtils, StringSearch, UsnMonitor, CpuFeatures, FileSystemUtils, SystemIdleDetector: various S1172/S1481/S1854/S5276/S6004
