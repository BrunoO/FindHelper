# Security Review - 2026-01-18

## Executive Summary
- **Security Posture Score**: 4/10
- **Critical Vulnerabilities**: 2
- **High Vulnerabilities**: 2
- **Total Findings**: 10
- **Estimated Remediation Effort**: 8-12 hours

## Findings

### Critical
1.  **Privilege Not Dropped After Initialization**
    *   **Vulnerability**: CWE-250: Execution with Unnecessary Privileges
    *   **Location**: `src/usn/UsnMonitor.cpp`, `src/core/Application.cpp`
    *   **Attack Vector**: After the `UsnMonitor` is initialized with elevated privileges to access the USN Journal, the main application continues to run with those same privileges. If a vulnerability (e.g., a buffer overflow in the search parsing) is exploited, the attacker would gain code execution with administrator rights.
    *   **Impact**: Privilege Escalation
    *   **Proof of Concept**: An exploit targeting any part of the application's UI or search functionality would grant the attacker admin rights.
    *   **Remediation**: The application should be split into two processes: a small, dedicated service running with elevated privileges to monitor the USN Journal, and the main UI application running as a standard user. The two processes can communicate via a secure IPC mechanism (e.g., named pipes with proper ACLs).
    *   **Severity**: Critical (CVSS: 9.8)

2.  **Uncontrolled Memory Allocation for Index**
    *   **Vulnerability**: CWE-400: Uncontrolled Resource Consumption
    *   **Location**: `src/index/FileIndex.cpp`
    *   **Attack Vector**: The `FileIndex` data structures grow in memory without any limits. An attacker who can influence the file system (e.g., by creating a very large number of files on a monitored drive) could cause the application to consume all available memory, leading to a denial of service.
    *   **Impact**: Availability (Denial of Service)
    *   **Proof of Concept**: A script that rapidly creates millions of files on the C: drive would cause the application to crash.
    *   **Remediation**: Implement configurable limits on the maximum number of files to be indexed and the maximum memory usage. When the limits are reached, the application should stop indexing and log a warning.
    *   **Severity**: Critical (CVSS: 9.1)

### High
1.  **Excessive Privileges on Volume Handle**
    *   **Vulnerability**: CWE-272: Least Privilege Violation
    *   **Location**: `src/usn/UsnMonitor.h`
    *   **Attack Vector**: The volume handle for the USN Journal is opened with `GENERIC_WRITE` permissions, which are not necessary for reading USN records. If an attacker could find a way to write to this handle, they could corrupt the file system.
    *   **Impact**: Integrity
    *   **Proof of Concept**: An exploit that reuses the volume handle could potentially write raw data to the disk.
    *   **Remediation**: Open the volume handle with only the required read permissions (`GENERIC_READ`).
    *   **Severity**: High (CVSS: 8.2)

2.  **Potential for ReDoS in Search Queries**
    *   **Vulnerability**: CWE-400: Uncontrolled Resource Consumption (ReDoS)
    *   **Location**: `src/search/ParallelSearchEngine.cpp`
    *   **Attack Vector**: The application uses `std::regex` for search queries. A malicious user could provide a specially crafted regex pattern that causes catastrophic backtracking, leading to 100% CPU usage and making the application unresponsive.
    *   **Impact**: Availability (Denial of Service)
    *   **Proof of Concept**: A search for a regex like `(a+)+$` on a large file index would likely hang the application.
    *   **Remediation**:
        -   Use a regex engine that is not vulnerable to ReDoS, such as Google's RE2.
        -   Implement a timeout for all regex searches.
        -   Sanitize user input to disallow complex regex patterns.
    *   **Severity**: High (CVSS: 7.5)

### Medium
1.  **TOCTOU Race Condition on File Access**
    *   **Vulnerability**: CWE-367: Time-of-check Time-of-use (TOCTOU) Race Condition
    *   **Location**: `src/platform/windows/FileOperations_win.cpp`
    *   **Attack Vector**: The code checks for a file's existence and then opens it. An attacker could replace the file with a symlink to a privileged location between the check and the open, potentially leading to information disclosure or privilege escalation.
    *   **Impact**: Confidentiality / Privilege Escalation
    *   **Proof of Concept**: A script that rapidly swaps a regular file with a symlink to `C:\Windows\System32\config\SAM`.
    *   **Remediation**: Open the file and then verify its properties, rather than checking before opening. Use `O_NOFOLLOW` flags where available.
    *   **Severity**: Medium (CVSS: 6.8)

2.  **Sensitive Data Logged in Plain Text**
    *   **Vulnerability**: CWE-532: Information Exposure Through Log Files
    *   **Location**: `src/utils/Logger.h`
    *   **Attack Vector**: The application logs user search queries and file paths in plain text. If the log files are not properly secured, a local attacker could read them to discover sensitive information.
    *   **Impact**: Confidentiality
    *   **Proof of Concept**: A user searches for "my_passwords.txt", and this query is logged.
    *   **Remediation**:
        -   Avoid logging sensitive information.
        -   If logging is necessary, ensure log files have restrictive permissions.
        -   Consider encrypting log files.
    *   **Severity**: Medium (CVSS: 5.5)

3.  **Use of `strcpy` without Size Checks**
    *   **Vulnerability**: CWE-120: Buffer Copy without Checking Size of Input ('Classic Buffer Overflow')
    *   **Location**: `src/ui/Popups.cpp`
    *   **Attack Vector**: There are instances of `strcpy` being used, which is vulnerable to buffer overflows if the source string is larger than the destination buffer.
    *   **Impact**: Integrity / Availability
    *   **Proof of Concept**: A long filename could overflow a buffer on the stack.
    *   **Remediation**: Replace `strcpy` with `strcpy_s` on Windows or `strncpy` with proper null termination on other platforms.
    *   **Severity**: Medium (CVSS: 5.3)

### Low
1.  **Integer Overflow in Allocation**
    *   **Vulnerability**: CWE-190: Integer Overflow or Wraparound
    *   **Risk Explanation**: There are places where allocations are made based on calculations that could potentially overflow, leading to an undersized buffer and a subsequent buffer overflow.
    *   **Remediation**: Use safe integer libraries or add manual checks for integer overflows before allocations.
    *   **Severity**: Low

2.  **Hardcoded API Keys**
    *   **Vulnerability**: CWE-798: Use of Hard-coded Credentials
    *   **Risk Explanation**: If any API keys were to be added in the future (e.g., for an online service), the current pattern of using constants in headers suggests they might be hardcoded.
    *   **Remediation**: Store secrets in a secure location, such as the system's credential manager, and retrieve them at runtime.
    *   **Severity**: Low (informational)

3.  **DLL Search Order Hijacking**
    *   **Vulnerability**: CWE-427: Uncontrolled Search Path Element
    *   **Risk Explanation**: The application does not use absolute paths when loading libraries, which could make it vulnerable to DLL search order hijacking if run from an untrusted directory.
    *   **Remediation**: Use absolute paths or `SetDllDirectory` to control the DLL search path.
    *   **Severity**: Low

## Summary Requirements
- **Security Posture Score**: 4/10. The application has critical vulnerabilities related to privilege management and resource consumption that need to be addressed immediately. The lack of separation between the privileged service and the UI is a major architectural flaw from a security perspective.
- **Critical Vulnerabilities**:
    1.  Privilege not dropped after initialization.
    2.  Uncontrolled memory allocation for the index.
- **Attack Surface Assessment**: The most dangerous inputs are the file system itself (which can be manipulated to cause DoS) and the search query input (which can be used for ReDoS attacks).
- **Hardening Recommendations**:
    -   **Principle of Least Privilege**: Split the application into a privileged service and a non-privileged UI.
    -   **Input Validation**: Sanitize all user input, especially regex patterns.
    -   **Resource Management**: Implement limits on memory and CPU usage.
    -   **Secure Coding Practices**: Replace unsafe functions like `strcpy` and add checks for integer overflows.
    -   **Dependency Management**: Keep third-party libraries up-to-date and scan them for vulnerabilities.
