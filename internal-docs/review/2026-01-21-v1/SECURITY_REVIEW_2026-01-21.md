# Security Review - 2026-01-21

## Executive Summary
- **Security Posture Score**: 4/10
- **Critical Vulnerabilities**: 2
- **Attack Surface Assessment**: The primary attack vectors are through the USN Journal parsing (kernel-level input) and user-provided search queries (potential for ReDoS). The application's requirement to run with elevated privileges significantly increases the impact of any potential vulnerability.

The application has critical security vulnerabilities that must be addressed before any production deployment. The most severe issues are the failure to drop elevated privileges after initialization and the use of excessive permissions when accessing the USN Journal. These create a significant risk of privilege escalation and system-wide compromise.

## Findings

### Critical
1.  **Failure to Drop Elevated Privileges**
    - **Vulnerability:** CWE-250: Execution with Unnecessary Privileges
    - **Location:** `src/core/main_common.h`
    - **Attack Vector:** An attacker who finds a separate vulnerability in the application (e.g., a buffer overflow) can exploit it to gain full administrator-level control of the system, because the application continues to run with elevated privileges long after they are needed.
    - **Impact:** Privilege Escalation
    - **Proof of Concept:**
        1.  The application starts with elevated privileges to access the USN Journal.
        2.  The application successfully initializes the `UsnMonitor`.
        3.  The application continues to run its main loop, including UI rendering and user input processing, with the same elevated privileges.
        4.  An attacker who can trigger a memory corruption bug in the UI can execute arbitrary code with admin rights.
    - **Remediation:** After the `UsnMonitor` is successfully initialized, the application should drop its elevated privileges to that of a normal user. This can be achieved using platform-specific APIs (e.g., `SetThreadToken` on Windows).
    - **Severity:** Critical

2.  **Excessive Permissions for USN Journal Handle**
    - **Vulnerability:** CWE-272: Least Privilege Violation
    - **Location:** `src/usn/UsnMonitor.h`
    - **Attack Vector:** The handle to the volume is opened with `GENERIC_WRITE` permissions, which are not necessary for reading USN records. If an attacker can find a way to write to this handle, they could corrupt the file system.
    - **Impact:** Integrity
    - **Proof of Concept:** The `CreateFile` call for the volume handle requests `GENERIC_READ | GENERIC_WRITE`. A vulnerability that allows an attacker to write to this handle could lead to direct disk modification.
    - **Remediation:** Open the volume handle with only the permissions required, which is `GENERIC_READ`.
      ```cpp
      // In UsnMonitor.cpp
      hVolume_ = CreateFile(volumePath.c_str(),
                            GENERIC_READ, // NOT GENERIC_WRITE
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr,
                            OPEN_EXISTING,
                            0,
                            nullptr);
      ```
    - **Severity:** Critical

### High
1.  **Potential for ReDoS in Search Queries**
    - **Vulnerability:** CWE-1333: Inefficient Regular Expression Complexity
    - **Location:** `src/search/SearchPatternUtils.cpp`
    - **Attack Vector:** A user can input a specially crafted regular expression in the search box that causes catastrophic backtracking, leading to 100% CPU utilization and making the application unresponsive.
    - **Impact:** Availability (Denial of Service)
    - **Proof of Concept:** A regex like `(a+)+$` when matched against a string of many 'a's followed by a non-'a' character will cause the regex engine to enter a state of exponential complexity.
    - **Remediation:**
        - Use a regex engine that has protection against ReDoS, or implement a timeout for all regex matching operations.
        - Sanitize user input to disallow complex regex patterns, such as nested quantifiers.
    - **Severity:** High
    - **Effort:** M

2.  **Unbounded Memory Allocation for Index**
    - **Vulnerability:** CWE-770: Allocation of Resources Without Limits or Throttling
    - **Location:** `src/index/FileIndex.cpp`
    - **Attack Vector:** The file index grows in memory without any configurable limits. On a system with a very large number of files, or if the USN Journal contains a massive number of records, the application could consume all available memory, leading to a system-wide denial of service.
    - **Impact:** Availability
    - **Proof of Concept:** Monitor a volume with tens of millions of files. The application's memory usage will grow until it exhausts system resources.
    - **Remediation:** Implement a configurable limit on the maximum size of the file index. When the limit is reached, either stop indexing and warn the user, or implement a strategy to page the index to disk.
    - **Severity:** High
    - **Effort:** L

## Hardening Recommendations
1.  **Implement Privilege Separation:** The component that accesses the USN Journal should be a separate, small, and isolated process. The main application should run with standard user privileges and communicate with the high-privilege process via a secure IPC mechanism.
2.  **Sandbox the Regex Engine:** Run all regular expression matching in a separate thread or process with a strict timeout to prevent ReDoS attacks from freezing the entire application.
3.  **Code Signing:** All executables and DLLs should be signed to prevent tampering and ensure their authenticity.
4.  **Enable ASLR and DEP:** Ensure that all compiled binaries have Address Space Layout Randomization (ASLR) and Data Execution Prevention (DEP) enabled to make memory corruption vulnerabilities more difficult to exploit.
