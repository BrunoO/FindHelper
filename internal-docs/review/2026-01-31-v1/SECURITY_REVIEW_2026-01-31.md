# Security Review - 2026-01-31

## Executive Summary
- **Health Score**: 7/10
- **Critical Issues**: 0
- **High Issues**: 2
- **Total Findings**: 4
- **Estimated Remediation Effort**: 16 hours

## Findings

### High

#### 1. Regex Injection / Denial of Service (ReDoS)
- **Vulnerability**: CWE-400: Uncontrolled Resource Consumption
- **Location**: `src/utils/StdRegexUtils.h`, `src/utils/SimpleRegex.h`
- **Attack Vector**: A user (or an automated script if the app is used as a backend) could provide a malicious regex or complex glob pattern (e.g., `(a+)+$`) that causes catastrophic backtracking in `std::regex` or deep recursion in `SimpleRegex`.
- **Impact**: CPU exhaustion, causing the UI thread or search worker thread to hang (Denial of Service).
- **Remediation**: Implement a timeout for regex matching or use a non-backtracking regex engine like RE2. For `SimpleRegex`, add a recursion depth limit or convert to an iterative DFA.
- **Severity**: High (CVSS: 7.5 - AV:L/AC:L/PR:N/UI:R/S:U/C:N/I:N/A:H)

#### 2. Uncontrolled Memory Allocation for File Index
- **Vulnerability**: CWE-770: Allocation of Resources Without Limits or Throttling
- **Location**: `src/index/FileIndex.cpp`
- **Attack Vector**: The application automatically indexes all files on the volume. An attacker could create a massive number of small files/directories to exhaust the application's memory.
- **Impact**: System-wide memory pressure or application crash (Denial of Service).
- **Remediation**: Implement a configurable maximum number of indexed items or a memory limit for the index.
- **Severity**: High (CVSS: 6.2 - AV:L/AC:L/PR:N/UI:N/S:U/C:N/I:N/A:H)

### Medium

#### 3. Incomplete Privilege Separation
- **Vulnerability**: Privilege Escalation Risk (Defense in Depth)
- **Location**: `src/usn/UsnMonitor.cpp`
- **Attack Vector**: The entire application runs as Administrator. While some privileges are dropped, the process token remains elevated. If a vulnerability is found in the UI or search logic, an attacker gains admin access.
- **Impact**: Privilege Escalation.
- **Remediation**: Implement a two-process model: a small privileged service for USN access and an unprivileged UI process.
- **Severity**: Medium
- **Effort**: Large

### Low

#### 4. Basic Path Sanitization
- **Vulnerability**: CWE-22: Improper Limitation of a Pathname to a Restricted Directory
- **Location**: `src/platform/windows/FileOperations.h` - `IsPathSafe`
- **Attack Vector**: While it checks for `..` and `\\`, it doesn't account for all Windows-specific path tricks like `\\?\`, `\??\`, or device aliases.
- **Impact**: Potential to open or delete files outside intended scope if a crafted path reaches these functions.
- **Remediation**: Use more robust path normalization and validation, possibly using `std::filesystem::canonical` and verifying it stays within allowed roots.
- **Severity**: Low (Input mostly comes from trusted USN journal)
- **Effort**: Small

## Summary Requirements

- **Security Posture Score**: 7/10. The application follows several good security practices like privilege dropping and robust USN record parsing. However, it lacks protection against resource exhaustion (CPU/Memory) from adversarial inputs or large environments.
- **Critical Vulnerabilities**: None identified that allow immediate RCE or data theft, but ReDoS is a significant availability concern.
- **Attack Surface Assessment**: The primary attack surfaces are:
  1. User search inputs (Regex/Glob).
  2. The file system itself (USN records/MFT records).
- **Hardening Recommendations**:
  1. Add a timeout to regex searches.
  2. Cap the maximum number of items in the `FileIndex`.
  3. Explore a two-process model for complete privilege separation.
  4. Ensure all external dependencies (ImGui, etc.) are kept up to date.
