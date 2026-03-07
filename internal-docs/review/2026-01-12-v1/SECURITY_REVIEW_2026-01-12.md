# Security Review - 2026-01-12

## Executive Summary
- **Security Posture Score**: 4/10
- **Critical Vulnerabilities**: 1
- **High Vulnerabilities**: 2
- **Attack Surface Assessment**: The primary attack surfaces are user-provided search queries (risk of ReDoS) and the USN Journal itself (risk of parsing malformed data). The most critical vulnerability, however, is the failure to drop elevated privileges.

## Findings

### Critical
1.  **Vulnerability**: CWE-250: Execution with Unnecessary Privileges
    -   **Location**: `src/platform/windows/main_win.cpp` (implicitly, the entire application after initialization)
    -   **Attack Vector**: After initializing the `UsnMonitor`, which requires administrator privileges, the application continues to run with those same elevated privileges. If a vulnerability is found elsewhere in the application (e.g., a buffer overflow in search query parsing or a library dependency), an attacker could exploit it to gain full control of the system.
    -   **Impact**: Privilege Escalation.
    -   **Proof of Concept**: An attacker crafts a malicious search query that exploits a hypothetical buffer overflow in the regex engine. Because the application is running as an administrator, the resulting shellcode also runs as an administrator, compromising the entire system.
    -   **Remediation**: After the `UsnMonitor` has been successfully initialized, the application should drop its elevated privileges and continue running as a standard user. This can be achieved using Windows APIs to create a new process with lower privileges or by managing process tokens.
    -   **Severity**: Critical. This is a fundamental security design flaw.

### High
1.  **Vulnerability**: CWE-400: Uncontrolled Resource Consumption (ReDoS)
    -   **Location**: `src/path/PathPatternMatcher.cpp`
    -   **Attack Vector**: The application uses `std::regex` for search queries prefixed with `rs:`. `std::regex` is known to be susceptible to ReDoS (Regular Expression Denial of Service) if the user provides a "catastrophic backtracking" pattern. A malicious query could cause the application to hang, consuming 100% of a CPU core.
    -   **Impact**: Availability (Denial of Service).
    -   **Proof of Concept**: A user enters a search query like `rs:^(\w+\s+)*\w+$` against a long string, causing the regex engine to enter a state of exponential complexity.
    -   **Remediation**:
        1.  **Timeout**: Implement a timeout for all regex matching operations. This can be done by running the match in a separate thread and terminating it if it doesn't complete within a reasonable time.
        2.  **Engine Replacement**: Consider using a ReDoS-safe regex engine like Google's RE2.
    -   **Severity**: High. This can be triggered by any user and can render the application unresponsive.

2.  **Vulnerability**: CWE-787: Out-of-bounds Write
    -   **Location**: `src/usn/UsnRecordUtils.cpp`
    -   **Attack Vector**: The code parsing USN V2 records relies on the `RecordLength` field to determine the offset to the next record. If the USN Journal is corrupted (or being manipulated by a malicious actor with kernel-level access), a malformed `RecordLength` could cause the application to write or read outside of its allocated buffer when processing records.
    -   **Impact**: Integrity, Availability, potentially Privilege Escalation.
    -   **Proof of Concept**: A crafted USN record with a `RecordLength` of 0 would cause an infinite loop. A very large `RecordLength` could cause a read past the end of the buffer.
    -   **Remediation**: Add bounds checking. Before advancing the record pointer, ensure that the `RecordLength` is non-zero and that the next record's address is within the bounds of the buffer provided by the operating system.
      ```cpp
      // In UsnRecordUtils::GetNextRecord
      if (record->RecordLength == 0) {
          // Handle error: zero-length record
          return nullptr;
      }
      uintptr_t current_address = reinterpret_cast<uintptr_t>(record);
      uintptr_t next_address = current_address + record->RecordLength;
      if (next_address >= buffer_end_address) {
          // Handle error: record length extends beyond buffer
          return nullptr;
      }
      return reinterpret_cast<PUSN_RECORD_V2>(next_address);
      ```
    -   **Severity**: High. Parsing data from a privileged source like the USN Journal requires strict validation.

### Medium
1.  **Vulnerability**: CWE-20: Improper Input Validation
    -   **Location**: `src/core/Settings.cpp`
    -   **Attack Vector**: The application loads its settings from `settings.json`. The parsing logic does not sufficiently validate the types and ranges of the values being read. A malicious or corrupted settings file could cause unexpected behavior, such as a negative font size or an invalid enum value for a theme.
    -   **Impact**: Availability.
    -   **Proof of Concept**: A `settings.json` file with `"fontSize": -10` could lead to rendering issues.
    -   **Remediation**: Add validation and clamping for all values read from the settings file. Use default values if a setting is missing or invalid.
    -   **Severity**: Medium. The impact is limited to the application's stability, and it requires the user to have a malicious settings file.

### Hardening Recommendations
-   **Principle of Least Privilege**: The failure to drop elevated privileges is the most significant issue. This should be the top priority.
-   **Input Validation**: All input, whether from the user (search queries) or the system (USN Journal), must be treated as untrusted.
-   **Use Safe Libraries**: For tasks like regex matching, prefer libraries that are designed to be safe from ReDoS attacks.
-   **Sandboxing**: For a future version, consider moving the file indexing and search functionality into a separate, sandboxed process that runs with the lowest possible privileges.
-   **Static Analysis**: Integrate a static analysis tool (like clang-tidy with security checks enabled) into the CI/CD pipeline to catch potential vulnerabilities automatically.