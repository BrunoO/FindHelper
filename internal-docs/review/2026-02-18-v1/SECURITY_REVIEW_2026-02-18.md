# Security Review - 2026-02-18

## Executive Summary
- **Security Posture Score**: 8/10
- **Critical Vulnerabilities**: 0
- **High Issues**: 1
- **Total Findings**: 8
- **Estimated Remediation Effort**: 16 hours

## Findings

### High
1. **ReDoS (Regex Denial of Service) Potential (DoS)**
   - **Location**: `src/utils/StdRegexUtils.h`
   - **Vulnerability**: The application uses `std::regex` (ECMAScript engine) for complex patterns without any execution time limits or complexity constraints.
   - **Attack Vector**: A user could enter a pathological regex (e.g., `(a+)+$`) that causes exponential backtracking when matched against certain strings, hanging the search worker thread and consuming 100% CPU.
   - **Impact**: Availability (DoS). One malicious search can freeze the application's search capability.
   - **Remediation**:
     - Switch to a non-backtracking regex engine like **RE2**.
     - OR implement a watchdog timer/interrupt mechanism for `std::regex` operations (difficult with `std::regex`).
     - OR add a pre-check to reject patterns with nested quantifiers.
   - **Severity**: High (CVSS: 7.5 - High availability impact, low complexity, no privileges for search input).

### Medium
1. **TOCTOU in Metadata Loading (Integrity/Reliability)**
   - **Location**: `src/search/SearchResultUtils.cpp` (and other places using lazy loading)
   - **Vulnerability**: Time-of-Check to Time-of-Use. Between the time a file is indexed and the time its size/modification time is loaded, the file could have been deleted or replaced.
   - **Attack Vector**: Rapidly creating and deleting files could cause the application to show stale or incorrect metadata, or potentially crash if not handled gracefully.
   - **Impact**: Integrity (incorrect information).
   - **Remediation**: Ensure all file system operations handle "file not found" errors gracefully (currently mostly handled but worth a sweep).
   - **Severity**: Medium

2. **Privilege Management: SeBackupPrivilege (Privilege Escalation)**
   - **Location**: `src/usn/UsnMonitor.cpp`
   - **Vulnerability**: The application requires and enables `SeBackupPrivilege` to read the USN Journal and MFT. While it drops privileges after initialization, there is a window where the process has elevated rights.
   - **Attack Vector**: If an exploit is successful during the initial indexing phase, the attacker would have backup/restore privileges, allowing them to read any file on the system.
   - **Impact**: Confidentiality/Privilege Escalation.
   - **Remediation**: Already partially addressed by `DropUnnecessaryPrivileges()`. Ensure this is called as early as possible.
   - **Severity**: Medium

### Low
1. **Unbounded Regex Cache (DoS)**
   - **Location**: `src/utils/StdRegexUtils.h`
   - **Vulnerability**: `ThreadLocalRegexCache` has no size limit.
   - **Attack Vector**: A script generating thousands of unique regex queries could lead to memory exhaustion.
   - **Impact**: Availability (Memory Exhaustion).
   - **Remediation**: Implement an LRU cache with a fixed maximum size (e.g., 512 entries).
   - **Severity**: Low (Requires many unique queries, limited impact per thread).

## Security Posture Score: 8/10
The application shows good security awareness by implementing robust USN record validation and dropping privileges after initialization. The use of modern C++ (RAII, `std::string_view`) reduces many common memory safety issues.

## Critical Vulnerabilities
None identified.

## Attack Surface Assessment
- **Search Query Input**: Most dangerous user-controlled input (ReDoS risk).
- **USN Journal Records**: System-controlled but potentially malformed (robustly handled).
- **File System Paths**: Handled using standard libraries, but long paths/UNC paths are edge cases.

## Hardening Recommendations
1. **Adopt RE2** for regex matching to guarantee linear time complexity.
2. **Implement an LRU limit** on the regex cache.
3. **Verify DLL Search Order** (Windows) to prevent DLL hijacking.
4. **Enable Control Flow Guard (CFG)** and other compiler-level protections in the build system.
