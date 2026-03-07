# Security Review - 2026-02-06

## Executive Summary
- **Health Score**: 7/10
- **Critical Issues**: 1
- **High Issues**: 2
- **Total Findings**: 6
- **Estimated Remediation Effort**: 8 hours

## Findings

### Critical
1. **ReDoS (Regex Denial of Service) in Search Patterns**
   - **Vulnerability**: CWE-1333: Inefficient Regular Expression Complexity
   - **Location**: `src/search/SearchPatternUtils.h` / `std::regex` usage
   - **Attack Vector**: User enters a maliciously crafted regex pattern (e.g., `(a+)+$`) via the `rs:` search prefix.
   - **Impact**: CPU Exhaustion (Availability). The search thread will hang, consuming 100% of a core indefinitely.
   - **Remediation**: Replace `std::regex` with a linear-time engine like Google RE2, or implement strict execution time limits/complexity checks for user-provided regex.
   - **Severity**: Critical (CVSS: 7.5 - High impact on availability)

### High
1. **Potential Buffer Overflow in USN Record Parsing**
   - **Vulnerability**: CWE-120: Buffer Overflow
   - **Location**: `src/usn/UsnMonitor.cpp` - `ProcessUsnRecords`
   - **Attack Vector**: Corrupted USN Journal or malicious driver providing malformed `USN_RECORD_V2` with `RecordLength` exceeding the actual buffer size or being too small for the fixed header.
   - **Impact**: Memory corruption / Potential RCE at SYSTEM level (Integrity/Confidentiality/Availability).
   - **Remediation**: Add strict validation that `RecordLength` is within the remaining buffer bounds AND at least `sizeof(USN_RECORD_V2)` before processing each record.
   - **Severity**: High (Due to SYSTEM-level execution)

2. **Privilege Over-provisioning**
   - **Vulnerability**: CWE-250: Execution with Unnecessary Privileges
   - **Location**: Application Manifest / `UsnMonitor`
   - **Attack Vector**: Any vulnerability in the app is amplified because it runs as Administrator by default for USN access.
   - **Impact**: Privilege Escalation.
   - **Remediation**: Implement a separate, minimal privileged service for USN monitoring and communicate via IPC, or drop privileges for the UI process after initializing the USN monitor.
   - **Severity**: High

### Medium
1. **Unsanitized Path Handling in UI Actions**
   - **Vulnerability**: CWE-22: Improper Limitation of a Pathname to a Restricted Directory ('Path Traversal')
   - **Location**: `src/ui/ResultsTable.cpp` - "Open in Explorer" / "Copy Path"
   - **Attack Vector**: Indexed files with malicious names containing `..` sequences.
   - **Impact**: Information Disclosure.
   - **Remediation**: Sanitize all paths before passing them to shell execution or clipboard.
   - **Severity**: Medium

### Low
1. **Information Leak in Log Files**
   - **Vulnerability**: CWE-532: Insertion of Sensitive Information into Log File
   - **Location**: `src/utils/Logger.h`
   - **Attack Vector**: Log files contain full file paths which may include sensitive usernames or project names.
   - **Impact**: Privacy/Information Disclosure.
   - **Remediation**: Ensure log file permissions are restricted (already implemented for Linux: 0600). Consider an option to redact paths in logs.
   - **Severity**: Low

## Quick Wins
1. **Add bounds checks to USN parsing**: Ensure `record->RecordLength` is always validated against the remaining buffer size.
2. **Implement regex timeout**: If sticking with `std::regex`, run it in a separate thread with a timeout (not ideal but better than nothing).
3. **Verify submodule versions**: Check `external/` submodules against known CVE databases.

## Hardening Recommendations
1. **Switch to RE2**: Essential for safe user-provided regex search.
2. **Process Isolation**: Separate the high-privileged indexing service from the low-privileged UI.
3. **ASLR/DEP**: Ensure all security mitigations are enabled in the CMake build configuration (already standard for most modern compilers).

## Attack Surface Assessment
The most dangerous inputs are:
1. **User search queries** (Regex injection)
2. **USN Journal data** (Low-level parsing vulnerabilities)
3. **File system names** (Special characters, path traversal)
