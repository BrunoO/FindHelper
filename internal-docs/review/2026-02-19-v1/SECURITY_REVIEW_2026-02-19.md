# Security Review - 2026-02-19

## Executive Summary
- **Health Score**: 8/10
- **Critical Issues**: 0
- **High Issues**: 1
- **Total Findings**: 8
- **Estimated Remediation Effort**: 20 hours

## Findings

### Critical
*None identified.*

### High
1. **Regex Denial of Service (ReDoS) Risk**
   - **Vulnerability**: CWE-1333: Inefficient Regular Expression Complexity
   - **Location**: `src/utils/StdRegexUtils.h`, `src/search/SearchWorker.cpp`
   - **Attack Vector**: A user provides a maliciously crafted regex (e.g., `(a+)+$`) as a search query.
   - **Impact**: The search worker thread may hang or consume 100% CPU for an extended period, leading to application unresponsiveness.
   - **Proof of Concept**: Search for `(a+)+$` in a folder containing many files starting with 'a'.
   - **Remediation**: Use a regex engine with guaranteed linear time complexity (like RE2) or implement execution time limits for `std::regex`.
   - **Severity**: High (Availability)

### Medium
1. **Potential Command Injection in Font Loading (Linux)**
   - **Vulnerability**: CWE-78: Improper Neutralization of Special Elements used in an OS Command
   - **Location**: `src/platform/linux/FontUtils_linux.cpp:32`
   - **Attack Vector**: If `font_name` in `AppSettings` is maliciously crafted (e.g., `Arial"; rm -rf /; "`), it could execute arbitrary commands.
   - **Impact**: Arbitrary command execution with the privileges of the application.
   - **Proof of Concept**: Modify `settings.json` to include shell metacharacters in `fontFamily`.
   - **Remediation**: Sanitize `font_name` before passing to `popen` or use Fontconfig's C API instead of the command-line utility.
   - **Severity**: Medium (Integrity/Confidentiality)

2. **Admin Privilege Requirement for USN Journal**
   - **Vulnerability**: Principle of Least Privilege Violation
   - **Location**: `src/usn/UsnMonitor.cpp`
   - **Attack Vector**: The application requires elevation to access the USN Journal. Any vulnerability in the application (like the ReDoS above) could then be exploited in an elevated context.
   - **Impact**: Escalated impact of other vulnerabilities.
   - **Remediation**: Explore dropping privileges after opening the volume handle or using a separate low-privilege process for the UI.
   - **Severity**: Medium

### Low
1. **Information Leakage via Logs**
   - **Vulnerability**: CWE-532: Insertion of Sensitive Information into Log File
   - **Location**: Multiple files using `LOG_INFO_BUILD`
   - **Attack Vector**: Full file paths and search queries are logged.
   - **Impact**: Log files may contain sensitive file names or user information.
   - **Remediation**: Sanitize logs in production builds or provide an option to disable detailed logging.
   - **Severity**: Low

2. **TOCTOU in File Deletion**
   - **Vulnerability**: CWE-367: Time-of-check Time-of-use (TOCTOU) Race Condition
   - **Location**: `src/ui/ResultsTable.cpp` (Delete action)
   - **Attack Vector**: A file could be replaced by a symlink between the confirmation and the actual deletion.
   - **Impact**: Accidental deletion of unintended targets.
   - **Remediation**: Use file IDs or handles for deletion where supported by the OS.
   - **Severity**: Low

## Summary Requirements

- **Security Posture Score**: 8/10. The core logic (USN parsing) is well-validated. The primary risks are from user-controlled search patterns and the high privilege level required for operation.
- **Critical Vulnerabilities**: None found. ReDoS should be addressed before public release.
- **Attack Surface Assessment**:
  1. **Search Queries**: Most immediate vector for ReDoS and logic bypass.
  2. **Settings Files**: Vector for command injection and configuration manipulation.
  3. **USN Records**: Low risk due to kernel source, but robustly handled.
- **Hardening Recommendations**:
  1. Replace `std::regex` with a safer alternative or add timeouts.
  2. Use C APIs for Fontconfig on Linux instead of `popen`.
  3. Implement a "sandbox" or privilege separation for the UI components.
  4. Ensure `SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0)` is used for sensitive handles.
