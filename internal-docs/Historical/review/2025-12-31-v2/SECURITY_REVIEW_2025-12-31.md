# Security Review - 2025-12-31

## Executive Summary
- **Security Posture Score**: 5/10
- **Critical Vulnerabilities**: 1
- **High Vulnerabilities**: 1
- **Total Findings**: 3
- **Attack Surface Assessment**: The most dangerous inputs are file paths, especially on Linux where they are passed to shell commands. Search queries with regex support also present a risk of ReDoS.

## Findings

### Critical
1.  **Vulnerability**: CWE-78: Improper Neutralization of Special Elements used in an OS Command ('OS Command Injection')
    **Location**: `FileOperations_linux.cpp`
    **Attack Vector**: An attacker could craft a malicious file path that, when passed to functions like `OpenFileDefault` or `CopyPathToClipboard`, would execute arbitrary shell commands. The `EscapeShellPath` function is insufficient to prevent all forms of command injection.
    **Impact**: Remote Code Execution (RCE)
    **Proof of Concept**: A file named `'; ls -la'` could be used to execute the `ls -la` command.
    **Remediation**: Avoid using `std::system` and `popen` to execute shell commands with user-provided data. Instead, use `exec`-family functions with a list of arguments, which avoids shell interpretation.
    ```cpp
    // Example of a safer way to execute a command
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        execlp("xdg-open", "xdg-open", full_path.c_str(), (char*)NULL);
        // If execlp returns, it means there was an error
        _exit(127);
    }
    ```
    **Severity**: Critical

### High
1.  **Vulnerability**: CWE-400: Uncontrolled Resource Consumption ('Denial of Service')
    **Location**: `SearchPatternUtils.h` (and its usage in `ParallelSearchEngine.cpp`)
    **Attack Vector**: A user could enter a malicious regular expression in the search query that would cause the application to hang. This is known as a ReDoS (Regular Expression Denial of Service) attack.
    **Impact**: Denial of Service (DoS)
    **Proof of Concept**: A regex like `(a+)+` could cause catastrophic backtracking.
    **Remediation**:
    -   Use a regex engine that is not vulnerable to ReDoS, such as Google's RE2.
    -   Implement a timeout for regex matching operations.
    -   Sanitize user input to disallow complex regex patterns.
    **Severity**: High

### Medium
1.  **Vulnerability**: CWE-22: Improper Limitation of a Pathname to a Restricted Directory ('Path Traversal')
    **Location**: `UsnMonitor.cpp`, `FileIndex.cpp`
    **Attack Vector**: The application does not appear to sanitize file paths for directory traversal sequences (`..`). An attacker could potentially craft a path that would allow them to read or write files outside of the intended directory.
    **Impact**: Information Disclosure, Integrity Violation
    **Proof of Concept**: A file path like `../../../../etc/passwd` could be used to access sensitive system files.
    **Remediation**:
    -   Normalize all file paths and ensure that they are within the expected directory.
    -   Use `std::filesystem::weakly_canonical` to resolve paths and check them against a whitelist of allowed directories.
    **Severity**: Medium

## Hardening Recommendations
-   **Principle of Least Privilege**: The application runs with elevated privileges on Windows. It should drop these privileges as soon as they are no longer needed (i.e., after the USN journal handle has been acquired).
-   **Sandboxing**: On Linux, consider using sandboxing technologies like Flatpak or Snap to restrict the application's access to the filesystem.
-   **Input Validation**: All user-provided input, including search queries and file paths, should be strictly validated and sanitized.
