# Security Review - 2026-02-14

## Executive Summary
- **Security Posture Score**: 6/10
- **Critical Vulnerabilities**: 1
- **High Vulnerabilities**: 1
- **Total Findings**: 5
- **Remediation Effort**: High

## Findings

### Critical
- **ReDoS Vulnerability in `std::regex` usage for `rs:` searches**:
  - **Vulnerability**: CWE-1333: Inefficient Regular Expression Complexity
  - **Location**: `src/utils/StdRegexUtils.h`
  - **Attack Vector**: A user (or malicious source if search queries are ever accepted from external inputs) can provide a "pathological" regex (e.g., `(a+)+$`) via the `rs:` prefix.
  - **Impact**: CPU Exhaustion (DoS). The application will hang or consume 100% CPU on the search worker thread, potentially affecting system responsiveness.
  - **Proof of Concept**: Search for `rs:(a+)+$` against a long string of 'a's.
  - **Remediation**: Migrate to a linear-time regex engine like **RE2** for user-provided patterns, or implement a watchdog/timeout for `std::regex` operations.
  - **Severity**: Critical (CVSS: 7.5 - High availability impact)

### High
- **Path Traversal / Unsanitized Input in Search Queries**:
  - **Vulnerability**: CWE-22: Improper Limitation of a Pathname to a Restricted Directory
  - **Location**: `src/search/SearchController.cpp`, `src/path/PathPatternMatcher.cpp`
  - **Attack Vector**: Search patterns using `pp:` or `rs:` might not be strictly bounded to the intended search root if relative paths or `..` are used.
  - **Impact**: Information Disclosure. Users might discover files outside the intended indexed scope if the crawler/searcher doesn't strictly validate bounds.
  - **Remediation**: Normalize all search root paths and ensure matching logic cannot escape the root.
  - **Severity**: High

### Medium
- **Unbounded Search Result Accumulation**:
  - **Vulnerability**: CWE-770: Allocation of Resources Without Limits or Throttling
  - **Location**: `src/search/StreamingResultsCollector.cpp`
  - **Impact**: Memory Exhaustion (DoS). A search matching millions of files (e.g., `*`) will attempt to store all results in memory.
  - **Remediation**: Implement a hard limit on maximum search results (e.g., 100,000) or use a paging mechanism.
  - **Severity**: Medium

## Summary Requirements
- **Security Posture Score**: 6/10 - Core logic is performant but lacks sufficient defensive "guardrails" against malicious or accidental pathological inputs.
- **Critical Vulnerabilities**: ReDoS in `rs:` searches must be addressed by switching to RE2 or adding timeouts.
- **Attack Surface Assessment**: The search input field is the primary attack surface. While currently local-only, any future remote/API features would make these vulnerabilities critical.
