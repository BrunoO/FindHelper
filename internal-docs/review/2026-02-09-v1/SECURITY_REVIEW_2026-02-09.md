# Security Review - 2026-02-09

## Executive Summary
- **Health Score**: 7/10
- **Critical Vulnerabilities**: 1 (ReDoS)
- **High Issues**: 1
- **Total Findings**: 8
- **Estimated Remediation Effort**: 24 hours

## Findings

### Critical
- **Vulnerability**: CWE-1333: Inefficient Regular Expression Complexity (ReDoS)
- **Location**: `src/utils/StdRegexUtils.h` and `src/search/SearchWorker.cpp`
- **Attack Vector**: User-provided search queries with `rs:` prefix are passed to `std::regex`. An attacker or accidental user can enter a pattern like `rs:(a+)+$` which causes exponential backtracking when matched against a long string of 'a's.
- **Impact**: Availability (DoS). The search thread will hang, and since searches are often triggered "as you type" or on Enter, this can lock up the application's search capability and consume CPU.
- **Proof of Concept**: Enter `rs:(a+)+$` in the search box and search against a long path like `aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa`.
- **Remediation**:
  - Switch to a linear-time regex engine like **RE2**.
  - Or, add a timeout to `std::regex` operations (though `std::regex` doesn't support this natively, so it would require a watchdog thread).
- **Severity**: Critical (CVSS: 7.5 - High availability impact, low complexity)

### High
- **Vulnerability**: CWE-269: Improper Privilege Management
- **Location**: `src/usn/UsnMonitor.cpp`
- **Attack Vector**: The application requires elevation for USN Journal access but does not explicitly drop or restrict privileges for other operations (like Gemini API calls or UI rendering).
- **Impact**: Increased attack surface if a vulnerability is found in the UI or AI processing logic.
- **Remediation**: Use the "Least Privilege" principle. If possible, separate the USN monitoring into a small, privileged service and run the UI/AI logic as a standard user.
- **Severity**: High

### Medium
- **Vulnerability**: CWE-22: Improper Limitation of a Pathname to a Restricted Directory (Path Traversal)
- **Location**: `src/crawler/FolderCrawler.cpp`
- **Attack Vector**: While the application is a search tool, it allows "Crawling" folders. If the folder path is provided via AI search or command line, it could potentially be used to access sensitive directories if not properly validated.
- **Impact**: Confidentiality.
- **Remediation**: Ensure all input paths are normalized and validated against an allowed set of volumes/roots.
- **Severity**: Medium

### Low
- **Vulnerability**: CWE-404: Improper Resource Shutdown or Release
- **Location**: `src/platform/windows/FileOperations_win.cpp`
- **Attack Vector**: Occasional use of raw `HANDLE` without RAII in some error paths could lead to handle exhaustion over long periods.
- **Impact**: Availability.
- **Remediation**: Use `unique_handle` (RAII) for all Windows API handles.
- **Severity**: Low

## Summary Requirements

- **Security Posture Score**: 7/10. The application is mostly offline and handles local data, but the `rs:` regex and AI integration introduce new attack vectors.
- **Critical Vulnerabilities**: **ReDoS** in `std::regex` usage for `rs:` patterns must be addressed before exposing AI features to untrusted prompts.
- **Attack Surface Assessment**: The primary attack surface is the **Search Input Field**, specifically when using the `rs:` prefix or when the Gemini AI generates search parameters.
- **Hardening Recommendations**:
  1. **Regex Hardening**: Replace `std::regex` with **RE2**.
  2. **Privilege Reduction**: Use the manifest to run as `asInvoker` by default and only request elevation when USN monitoring is explicitly enabled. (Note: memory says manifest already specifies `asInvoker`, which is good).
  3. **Input Sanitization**: Sanitize all inputs from the Gemini API as if they were from an untrusted user.
