# Security Review - 2026-01-17

## Executive Summary
- **Security Posture Score**: 4/10
- **Critical Vulnerabilities**: 1
- **Attack Surface Assessment**: The primary attack surface is the user search query, which could be a vector for ReDoS attacks. The USN Journal itself is a trusted source, but a corrupted journal could potentially cause issues.
- **Hardening Recommendations**:
    -   Drop privileges after initialization.
    -   Implement input validation for search queries.
    -   Use a sandboxing mechanism to limit the application's access to the system.

## Findings

### Critical
-   **Privilege Escalation**: The application does not drop elevated privileges after initializing the `UsnMonitor`. This is a critical security vulnerability that could allow an attacker to gain full control of the system if they find another vulnerability in the application.
    -   **Vulnerability**: CWE-250: Execution with Unnecessary Privileges
    -   **Location**: `src/usn/UsnMonitor.cpp`
    -   **Attack Vector**: An attacker could exploit another vulnerability in the application (e.g., a buffer overflow) to execute arbitrary code with elevated privileges.
    -   **Impact**: Privilege Escalation
    -   **Remediation**: Drop privileges after the `UsnMonitor` has been initialized. This can be done by creating a new thread with a lower-privileged token and running the rest of the application in that thread.
    -   **Severity**: Critical

### High
-   **Uncontrolled Resource Consumption**: The application does not limit the amount of memory that can be used for the index. This could be exploited by an attacker to cause a denial-of-service attack by creating a large number of files on the monitored volume.
    -   **Vulnerability**: CWE-400: Uncontrolled Resource Consumption
    -   **Location**: `src/index/FileIndex.cpp`
    -   **Attack Vector**: An attacker could create a large number of files on the monitored volume, causing the application to consume all available memory.
    -   **Impact**: Availability
    -   **Remediation**: Implement a mechanism to limit the amount of memory that can be used for the index.
    -   **Severity**: High

### Medium
-   **Inadequate Input Validation**: The application does not adequately validate user input, which could lead to ReDoS attacks.
    -   **Vulnerability**: CWE-20: Improper Input Validation
    -   **Location**: `src/ui/SearchInputs.cpp`
    -   **Attack Vector**: An attacker could provide a malicious regex pattern that causes the application to hang.
    -   **Impact**: Availability
    -   **Remediation**: Implement input validation for search queries to prevent ReDoS attacks.
    -   **Severity**: Medium
