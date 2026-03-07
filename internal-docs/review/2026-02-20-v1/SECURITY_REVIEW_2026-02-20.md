# Security Review - 2026-02-20

## Executive Summary
- **Security Posture Score**: 8/10
- **Critical Vulnerabilities**: 0
- **High Issues**: 1
- **Total Findings**: 6
- **Estimated Remediation Effort**: 20 hours

## Findings

### 1. ReDoS (Regex Denial of Service)
- **Vulnerability**: CWE-1333: Inefficient Regular Expression Complexity
- **Location**: `src/utils/StdRegexUtils.h`
- **Attack Vector**: An attacker (or user) providing a complex regex pattern (e.g., `(a+)+$`) against a long string. Since `std::regex` uses an ECMAScript engine with backtracking and no execution time limit, this can cause the search thread to hang indefinitely, consuming 100% CPU.
- **Impact**: Availability (DoS).
- **Remediation**:
  - Implement a regex execution time limit if possible (difficult with `std::regex`).
  - Switch to a regex engine with guaranteed linear time complexity (like RE2).
  - Add a warning/confirmation for complex-looking regex patterns.
- **Severity**: High

### 2. Potential TOCTOU in File Operations
- **Vulnerability**: CWE-367: Time-of-check Time-of-use (TOCTOU) Race Condition
- **Location**: `src/platform/windows/FileOperations_win.cpp` (OpenWithShell, etc.)
- **Attack Vector**: The time between checking if a file exists and opening it with the shell could be exploited if an attacker can replace the file with a malicious link in that interval.
- **Impact**: Integrity / Privilege Escalation.
- **Remediation**: Use file handles instead of paths for subsequent operations where the API allows.
- **Severity**: Medium

### 3. Missing Unbounded Search Result Limit
- **Vulnerability**: CWE-770: Allocation of Resources Without Limits or Throttling
- **Location**: `src/gui/GuiState.h` (`searchResults` vector)
- **Attack Vector**: Searching for a very common pattern (e.g., `*`) on a massive filesystem can lead to millions of results being stored in `GuiState::searchResults`, potentially exhausting system memory.
- **Impact**: Availability (DoS via Memory Exhaustion).
- **Remediation**: Implement a hard cap on the number of results displayed (e.g., 100,000) and warn the user to refine the search.
- **Severity**: Medium

### 4. Raw HANDLE Leak on Error Paths
- **Vulnerability**: CWE-404: Improper Resource Shutdown or Release
- **Location**: `src/usn/UsnMonitor.cpp`
- **Attack Vector**: If an exception or early return occurs between opening a volume handle and wrapping it in RAII, the handle remains open until process termination.
- **Impact**: Availability (Resource leak).
- **Remediation**: Immediately wrap raw `HANDLE` in `ScopedHandle` after creation.
- **Severity**: Medium

### 5. Sensitive Data in Logs
- **Vulnerability**: CWE-532: Insertion of Sensitive Information into Log File
- **Location**: `src/utils/Logger.h`
- **Attack Vector**: The logger records full file paths and search queries. While it uses restrictive permissions (0600 on Linux), on Windows the default permissions might allow other users to read the log files in `%TEMP%`.
- **Impact**: Confidentiality.
- **Remediation**: Ensure restrictive ACLs are set on the log directory/file on Windows. Provide an option to redact sensitive parts of paths in logs.
- **Severity**: Low

## Summary

- **Security Posture Score**: 8/10. The application generally follows good security practices, especially in USN record parsing where robust bounds checking is implemented.
- **Critical Vulnerabilities**: None found.
- **Attack Surface Assessment**: The primary attack surface is the user-provided search queries (Regex) and the USN journal records from the kernel. Since the app requires elevation on Windows, it is already a high-privilege target.
- **Hardening Recommendations**:
  1. Replace `std::regex` with a safer alternative or add execution timeouts.
  2. Implement result set limits to prevent memory exhaustion.
  3. Ensure all Windows handles are wrapped in RAII immediately upon acquisition.
