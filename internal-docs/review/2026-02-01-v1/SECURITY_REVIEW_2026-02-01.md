# Security Review - 2026-02-01

## Executive Summary
- **Health Score**: 8/10
- **Critical Vulnerabilities**: 0
- **High Issues**: 1
- **Total Findings**: 4
- **Estimated Remediation Effort**: 8 hours

## Findings

### High
1. **ReDoS Vulnerability in `std::regex` usage**
   - **Vulnerability**: CWE-1333: Inefficient Regular Expression Complexity
   - **Location**: `src/utils/StdRegexUtils.h`
   - **Attack Vector**: Users can provide arbitrary regex patterns via the `rs:` prefix in the search box. Maliciously crafted regex patterns (e.g., `(a+)+$`) can cause catastrophic backtracking, leading to CPU exhaustion and DoS of the application.
   - **Impact**: Availability
   - **Proof of Concept**: Search for `rs:(a+)+$` against a long string of 'a's.
   - **Remediation**: Use a regex engine with guaranteed linear-time matching (like RE2) or implement a timeout for `std::regex` operations.
   - **Severity**: High

### Medium
2. **Unbounded Thread-Local Regex Cache**
   - **Vulnerability**: CWE-770: Allocation of Resources Without Limits or Throttling
   - **Location**: `src/utils/StdRegexUtils.h: ThreadLocalRegexCache`
   - **Attack Vector**: If an attacker can trigger many unique regex searches (e.g., via a script or automated input), the thread-local cache will grow indefinitely, leading to memory exhaustion.
   - **Impact**: Availability
   - **Remediation**: Implement LRU eviction with a fixed maximum size for the cache.
   - **Severity**: Medium

### Low
3. **Sensitive Data in Logs**
   - **Vulnerability**: CWE-532: Insertion of Sensitive Information into Log File
   - **Location**: `src/search/SearchWorker.cpp: ExecuteSearch`
   - **Attack Vector**: Search queries (which may contain sensitive file names or paths) are logged at `INFO_BUILD` level. These logs are stored on disk and could be accessed by other users.
   - **Impact**: Confidentiality
   - **Remediation**: Avoid logging full search queries or ensure logs are stored in protected locations.
   - **Severity**: Low

4. **Path Traversal during Index Dumping**
   - **Vulnerability**: CWE-22: Improper Limitation of a Pathname to a Restricted Directory ('Path Traversal')
   - **Location**: `src/core/Application.cpp: HandleIndexDump`
   - **Attack Vector**: The `dump-index-to` command-line argument is used directly as a file path. If this input is not sanitized, it could allow writing the index dump to arbitrary locations on the system.
   - **Impact**: Integrity
   - **Remediation**: Sanitize the dump path or restrict it to a specific directory.
   - **Severity**: Low

## Summary Requirements

### Security Posture Score: 8/10
Justification: The application follows good security practices like dropping privileges after initialization. The main risks are related to regex-based DoS and resource management.

### Critical Vulnerabilities: None

### Attack Surface Assessment
The primary attack surface is the user search input, specifically the `rs:` regex prefix. USN records from the kernel are also an input source but are generally considered trusted, though they are correctly validated.

### Hardening Recommendations
1. Replace `std::regex` with a safer alternative or add execution time limits.
2. Implement strict size limits on all caches (regex, path, etc.).
3. Ensure settings file has restricted permissions (600 on Linux/macOS).
