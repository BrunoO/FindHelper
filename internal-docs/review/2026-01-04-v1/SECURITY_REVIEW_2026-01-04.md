# Security Review - 2026-01-04

## Executive Summary
- **Security Posture Score**: 4/10
- **Critical Vulnerabilities**: 1
- **High Vulnerabilities**: 2
- **Hardening Recommendations**: 3

## Findings

### Critical
1.  **Privilege Not Dropped After Initialization**
    - **Vulnerability**: CWE-250: Execution with Unnecessary Privileges
    - **Location**: `main_win.cpp`
    - **Attack Vector**: The application requires administrator privileges to access the USN Journal, but it does not drop these privileges after initialization. If a vulnerability is found in the application, an attacker could exploit it to gain administrator-level control of the system.
    - **Impact**: Privilege Escalation
    - **Proof of Concept**: Any successful exploit of a vulnerability in the application will run with administrator privileges.
    - **Remediation**: After initializing the USN Journal, the application should drop its privileges to that of a regular user. This can be done using the `CreateProcessAsUser` function or by creating a separate, unprivileged process to handle the UI and search logic.
    - **Severity**: Critical

### High
1.  **Potential for Regex Injection (ReDoS)**
    - **Vulnerability**: CWE-400: Uncontrolled Resource Consumption ('Resource Exhaustion')
    - **Location**: `SearchPatternUtils.h`
    - **Attack Vector**: The `CreateFilenameMatcher` function uses `std::regex` to perform searches. If a user provides a malicious regex pattern, it could cause the application to hang or crash due to catastrophic backtracking.
    - **Impact**: Availability
    - **Proof of Concept**: A regex such as `(a+)+$` could cause the application to hang if the input string contains a long sequence of "a"s followed by a character other than "a".
    - **Remediation**: Use a timeout mechanism to prevent the regex from running for too long. Alternatively, consider using a simpler, less powerful pattern matching engine that is not vulnerable to ReDoS.
    - **Severity**: High

2.  **Path Traversal Vulnerability**
    - **Vulnerability**: CWE-22: Improper Limitation of a Pathname to a Restricted Directory ('Path Traversal')
    - **Location**: `PathUtils.cpp`
    - **Attack Vector**: The application does not properly sanitize user-provided paths. An attacker could provide a path with `..` sequences to access files and directories outside of the intended search scope.
    - **Impact**: Confidentiality, Integrity
    - **Proof of Concept**: A search for `../../../../../etc/passwd` could reveal the contents of the system's password file.
    - **Remediation**: Sanitize all user-provided paths to remove `..` sequences and ensure that they are within the intended search scope.
    - **Severity**: High

## Summary

-   **Security Posture Score**: 4/10. The application has a critical vulnerability related to privilege escalation, as well as high-severity vulnerabilities related to ReDoS and path traversal. These vulnerabilities could be exploited by an attacker to compromise the system.
-   **Critical Vulnerabilities**: The privilege escalation vulnerability must be fixed before any release.
-   **Attack Surface Assessment**: The most dangerous inputs are the search queries and the file system paths provided by the user. These inputs are not properly sanitized and could be used to exploit vulnerabilities in the application.
-   **Hardening Recommendations**:
    1.  **Drop privileges**: The application should drop its administrator privileges after initializing the USN Journal.
    2.  **Sanitize inputs**: All user-provided inputs should be sanitized to prevent injection attacks.
    3.  **Use a sandbox**: Consider running the search process in a sandbox to limit its access to the system.
