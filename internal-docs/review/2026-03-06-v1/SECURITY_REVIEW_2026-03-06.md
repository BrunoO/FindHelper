# Security Review - 2026-03-06

## Executive Summary
- **Health Score**: 9/10
- **Critical Issues**: 0
- **High Issues**: 1
- **Total Findings**: 5
- **Estimated Remediation Effort**: 16 hours

## Findings

### Critical
None.

### High
1. **ReDoS Risk in Search Queries** (Category 2)
   - **Vulnerability**: CWE-1333: Inefficient Regular Expression Complexity (ReDoS).
   - **Location**: `src/utils/StdRegexUtils.cpp::RegexMatch`
   - **Attack Vector**: User-supplied malicious regex (e.g., `(a+)+$`) can cause catastrophic backtracking, exhausting CPU and freezing the UI/search threads.
   - **Impact**: Denial of Service (DoS) of the application.
   - **Proof of Concept**: Use a pattern with exponential complexity and a long, non-matching string.
   - **Remediation**: Use a regex engine with time limits or linear-time execution (like RE2), or add a watchdog timer to terminate long-running regex matches.
   - **Severity**: High
   - **Effort**: Large (8+ hours to change engine)

### Medium
1. **Path Traversal in Folder Crawler** (Category 2)
   - **Vulnerability**: CWE-22: Improper Limitation of a Pathname to a Restricted Directory.
   - **Location**: `src/crawler/FolderCrawler.cpp`
   - **Attack Vector**: If crawling starts from a user-supplied relative path, it might be possible to escape the intended directory using `..` sequences.
   - **Impact**: Information Disclosure (indexing files outside intended scope).
   - **Remediation**: Canonicalize all user-supplied paths and verify they are within the expected root before indexing.
   - **Severity**: Medium
   - **Effort**: Medium (2 hours)

2. **UNC Path Injection** (Category 2)
   - **Vulnerability**: CWE-20: Improper Input Validation.
   - **Location**: `src/path/PathUtils.cpp`
   - **Attack Vector**: Injecting UNC paths (`\\server\share`) where only local paths are expected might trigger network traffic or authentication leaks (NTLM hashes).
   - **Impact**: Information Disclosure / Network activity.
   - **Remediation**: Explicitly validate and optionally block UNC paths if not supported.
   - **Severity**: Medium
   - **Effort**: Medium (2 hours)

### Low
1. **Privileged Operations Inheritance** (Category 3)
   - **Vulnerability**: CWE-250: Execution with Unnecessary Privileges.
   - **Location**: `src/platform/windows/AppBootstrap_win.cpp`
   - **Attack Vector**: The entire app runs elevated for USN access. Child processes (if spawned) will also be elevated.
   - **Impact**: Privilege Escalation for any child processes.
   - **Remediation**: Use `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` to limit handle inheritance and consider dropping privileges where possible.
   - **Severity**: Low
   - **Effort**: Medium (4 hours)

2. **Insecure Logging of Sensitive Paths** (Category 5)
   - **Vulnerability**: CWE-532: Insertion of Sensitive Information into Log File.
   - **Location**: `src/utils/Logger.h`
   - **Attack Vector**: Full user file paths are logged, which may contain sensitive names (usernames, project names).
   - **Impact**: Information Disclosure via log files.
   - **Remediation**: Implement a "secure logging" mode that truncates or redacts paths in production logs.
   - **Severity**: Low
   - **Effort**: Medium (2 hours)

## Security Posture Score: 9/10
The application has a small attack surface as it is mostly a local tool. The primary risk is DoS via ReDoS and potential information disclosure through path traversal or log files. The dependency chain is well-controlled via submodules.

## Critical Vulnerabilities
None identified.

## Attack Surface Assessment
User search queries (especially regex) and the target folder path are the most dangerous inputs. These should be strictly validated and constrained.

## Hardening Recommendations
1. **Regex Safety**: Implement execution time limits for regex matches.
2. **Path Canonicalization**: Always resolve and validate paths before crawling.
3. **Log Sanitization**: Redact sensitive path information in production logs.
4. **Least Privilege**: If possible, separate the USN reader into a small service and run the UI with standard privileges.
