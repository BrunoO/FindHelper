# Security Review - 2026-01-05

## Executive Summary
- **Security Posture Score**: 4/10
- **Critical Vulnerabilities**: 1
- **High Vulnerabilities**: 2
- **Attack Surface Assessment**: The primary attack vectors are through malformed USN journal records and crafted user search queries. The application's requirement to run with elevated privileges significantly increases the impact of any potential vulnerability.

## Findings

### Critical
1.  **CWE-250: Execution with Unnecessary Privileges**
    -   **Location:** `src/platform/windows/main_win.cpp`
    -   **Attack Vector:** The application requires administrator privileges to access the USN journal. However, it does not drop these privileges after the initial setup. If a vulnerability is exploited in a part of the application that does not require elevated privileges (e.g., the UI, search query parsing), the attacker will gain full administrator control over the system.
    -   **Impact:** Privilege Escalation
    -   **Proof of Concept:** A buffer overflow in the search query parsing logic could be exploited to execute arbitrary code with administrator privileges.
    -   **Remediation:** After initializing the USN journal, the application should drop its privileges to that of a standard user. This can be achieved using the `CreateProcessAsUser` API to relaunch itself in a non-elevated context.
    -   **Severity:** Critical

### High
1.  **CWE-400: Uncontrolled Resource Consumption (ReDoS)**
    -   **Location:** `src/search/SearchPatternUtils.h`
    -   **Attack Vector:** The application uses `std::regex` for search queries prefixed with `rs:`. If a user provides a specially crafted regular expression with catastrophic backtracking, it can cause the application to hang, consuming 100% of a CPU core.
    -   **Impact:** Availability (Denial of Service)
    -   **Proof of Concept:** A search query like `rs:(a+)+b` would likely trigger this vulnerability.
    -   **Remediation:**
        -   Implement a timeout mechanism for all regex searches.
        -   Consider using a regex engine that is not vulnerable to ReDoS, such as Google's RE2.
        -   Sanitize user input to disallow complex regex patterns.
    -   **Severity:** High

2.  **CWE-22: Improper Limitation of a Pathname to a Restricted Directory ('Path Traversal')**
    -   **Location:** `src/index/IndexFromFilePopulator.cpp`
    -   **Attack Vector:** When indexing from a file, the application does not properly sanitize the file paths. An attacker could craft a file list containing `..` sequences to cause the application to index files outside of the intended directory.
    -   **Impact:** Confidentiality
    -   **Proof of Concept:** A file list containing `C:/Users/someuser/../../../Windows/System32` could be used to index the System32 directory.
    -   **Remediation:** Before accessing any file, normalize the path and ensure that it is within the intended base directory.
    -   **Severity:** High

### Medium
1.  **CWE-404: Improper Resource Shutdown or Release**
    -   **Location:** `src/platform/windows/FileOperations_win.cpp`
    -   **Attack Vector:** The file uses raw `HANDLE` variables that are manually closed. If an error occurs and an early return is taken, the `CloseHandle()` call might be skipped, leading to a handle leak.
    -   **Impact:** Availability (Resource Exhaustion)
    -   **Proof of Concept:** A long-running application could slowly leak handles, eventually leading to system instability.
    -   **Remediation:** Use an RAII wrapper (like the `ScopedHandle` class that already exists in the codebase but is not used here) to ensure that handles are always closed.
    -   **Severity:** Medium

### Low
1.  **CWE-777: Deprecated or Obsolete Function**
    -   **Location:** `src/filters/TimeFilterUtils.cpp`
    -   **Attack Vector:** The code uses the `localtime` function, which is not thread-safe. This could potentially lead to data corruption in a multi-threaded environment.
    -   **Impact:** Integrity
    -   **Proof of Concept:** If two threads call `localtime` at the same time, they could receive incorrect results.
    -   **Remediation:** Replace `localtime` with its thread-safe equivalent, `localtime_s` on Windows and `localtime_r` on Linux/macOS.
    -   **Severity:** Low

## Hardening Recommendations
-   **Principle of Least Privilege:** As mentioned in the critical vulnerability, dropping privileges after initialization is the most important hardening step.
-   **Input Sanitization:** All user input, especially search queries and file paths, should be strictly sanitized.
-   **Use Safe APIs:** Replace unsafe C-style functions (like `strcpy`, `localtime`) with their safer, modern C++ equivalents.
-   **Dependency Audit:** Regularly audit third-party libraries (like `nlohmann::json`) for known vulnerabilities.
