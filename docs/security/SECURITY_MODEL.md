# Security Model

**Last Updated:** 2026-02-01
**Purpose:** Document the application's security architecture, privilege model, and attack surface

---

## Executive Summary

This application can run with standard user privileges on all platforms. On Windows, elevated (administrator) privileges are optional and only required to access the USN Journal for real-time file system monitoring. When running without administrator rights, the application uses folder crawling (like on macOS and Linux).

To minimize security risk when running as administrator, the application implements privilege reduction after acquiring necessary resources, and includes input validation to prevent privilege escalation vulnerabilities.

**Security Posture:** The application follows the principle of least privilege. When elevated, it drops unnecessary privileges after initialization. Failure to drop privileges in elevated mode is treated as a fatal error, causing the application to exit immediately.

---

## Privilege Model

### Optional Elevation (Windows Only)

On Windows, the application supports two execution modes:

1.  **Standard User (Default):** The application starts without administrator privileges (`asInvoker`). It uses folder crawling to build the file index. USN Journal real-time monitoring is disabled.
2.  **Administrator:** If the user chooses to "Run as administrator", the application can access the USN Journal for real-time monitoring.

### Why Elevated Privileges Are Required (for USN Monitoring)

The application requires administrator privileges on Windows specifically to:
- Open volume handles with `GENERIC_READ | GENERIC_WRITE` access
- Query and read from the USN Journal using `DeviceIoControl(FSCTL_READ_USN_JOURNAL)`

These operations require `SE_BACKUP_PRIVILEGE` and `SE_RESTORE_PRIVILEGE`, which are typically only available to administrators.

### Privilege Reduction Strategy

After successfully opening the volume handle and querying the USN Journal, the application immediately drops unnecessary privileges:

**Privileges Dropped:**
- `SE_DEBUG_PRIVILEGE` - Debug programs (not needed for USN Journal)
- `SE_TAKE_OWNERSHIP_PRIVILEGE` - Take ownership of files (not needed)
- `SE_SECURITY_PRIVILEGE` - Manage auditing and security log (not needed)
- `SE_RESTORE_PRIVILEGE` - Restore files (not needed for read-only USN access)

**Privileges Retained:**
- `SE_BACKUP_PRIVILEGE` - Required for USN Journal auto-refresh feature (bypasses ACLs to read metadata)

**Why This Works:**
Windows checks privileges at handle creation time, not at handle use time. The volume handle opened with admin privileges remains valid and usable after dropping privileges, so continuous USN Journal monitoring continues to work without interruption.

### Fatal Error on Privilege Drop Failure

**Security Requirement:** If privilege dropping fails, the application **must exit immediately**. Continuing to run with unnecessary elevated privileges creates a privilege escalation vulnerability.

**Implementation:**
1. `UsnMonitor::ReaderThread()` attempts to drop privileges after opening the volume handle
2. If `DropUnnecessaryPrivileges()` returns `false`, the `privilege_drop_failed_` flag is set
3. `UsnMonitor::Start()` returns `false`, and `DidPrivilegeDropFail()` returns `true`
4. `AppBootstrap::StartUsnMonitor()` detects the failure and sets `security_fatal_error = true`
5. `main_common.h` checks `security_fatal_error` and exits with error code 1 before creating the Application

**Error Message:**
```
SECURITY: Failed to drop privileges after acquiring volume handle.
Continuing with elevated privileges is a security risk. Application will exit.
```

---

## Attack Surface

### Primary Attack Vectors

1. **File Paths from USN Journal**
   - **Source:** USN Journal records (kernel-provided, trusted)
   - **Risk:** Low - paths come from the file system itself, not user input
   - **Mitigation:** Path validation before ShellExecute (defense-in-depth)

2. **User Search Queries**
   - **Source:** User input via UI
   - **Risk:** Medium - regex injection (ReDoS) possible
   - **Mitigation:** Regex timeout, input sanitization (see `SearchPatternUtils.h`)

3. **ShellExecute Operations**
   - **Source:** User-selected files from search results
   - **Risk:** Medium - if privileges aren't dropped, could execute arbitrary code
   - **Mitigation:** 
     - Privilege dropping (primary defense)
     - Path validation (defense-in-depth)
     - File paths come from trusted source (USN Journal)

### Path Validation

**Location:** `FileOperations.h` - `internal::IsPathSafe()`

**Checks:**
- Rejects UNC paths (`\\server\share`) - prevents network access
- Rejects path traversal sequences (`..`) - prevents directory escape
- Rejects embedded nulls - prevents string truncation attacks

**Why This Is Needed:**
Even though file paths come from the USN Journal (trusted source), path validation adds defense-in-depth protection against:
- Malicious file names created on the file system
- Corrupted USN Journal records
- Future code changes that might introduce untrusted paths

### ShellExecute Usage

**Functions Using ShellExecute:**
1. `FileOperations::OpenFileDefault()` - Opens files with default application
2. `ShellContextUtils::RestartAsAdmin()` - Restarts application as administrator
3. `ShellContextUtils::ShowContextMenu()` - Shows Windows context menu (uses `IContextMenu::InvokeCommand`)

**Security Analysis:**
- **`RestartAsAdmin()`**: SAFE - Uses `GetModuleFileNameW()` to get current executable path (controlled)
- **`OpenFileDefault()`**: SAFE with mitigations - Paths from file index (trusted), validated before use
- **`ShowContextMenu()`**: SAFE with mitigations - Paths from file index (trusted), Windows Shell validates commands

---

## Security Architecture

### Current Implementation (Option 1: Privilege Reduction)

**Design:** Single-process model with privilege reduction after initialization

**Pros:**
- Minimal architectural changes (2-3 days implementation)
- Meaningful security improvement (drops 4 dangerous privileges)
- Low risk, low effort
- Handle remains valid after privilege drop

**Cons:**
- Process still runs with admin privileges overall (process token is elevated)
- Not complete privilege separation
- If another vulnerability is found, attacker could still use remaining privileges

**Security Benefit:** Reduces attack surface by ~80% (drops 4 of 5 dangerous privileges)

### Future Enhancement (Option 2: Two-Process Model)

**Design:** Separate privileged service process and unprivileged UI process

**Pros:**
- Complete privilege separation
- UI process runs with standard user privileges
- Minimal attack surface for privileged process (only USN Journal access)

**Cons:**
- Major architectural changes (1-2 weeks implementation)
- IPC overhead and complexity
- Service management complexity

**When to Consider:** Future major release when architectural refactoring is planned

**See:** `docs/PRIVILEGE_DROPPING_STATUS.md` for detailed comparison

---

## Error Handling

### Exception Handling Strategy

**Current State:** Several `catch(...)` blocks that swallow unknown exceptions

**Security Concern:** Generic exception catches can hide security-relevant errors

**Recommendation:** Replace with specific exception handlers where possible

### Security-Critical Errors

**Privilege Drop Failure:**
- **Handling:** Fatal error, application exits immediately
- **Location:** `UsnMonitor::ReaderThread()`, `AppBootstrap::StartUsnMonitor()`, `main_common.h`
- **Rationale:** Continuing with elevated privileges is a security risk

**Path Validation Failure:**
- **Handling:** Operation rejected, error logged
- **Location:** `FileOperations::OpenFileDefault()`
- **Rationale:** Defense-in-depth, prevents potential privilege escalation

---

## Hardening Recommendations

### Short-Term (Implemented)

✅ **Privilege Reduction** - Drop unnecessary privileges after initialization  
✅ **Fatal Error on Privilege Drop Failure** - Exit immediately if privileges cannot be dropped  
✅ **Path Validation** - Validate paths before ShellExecute operations  

### Medium-Term (Recommended)

- **Specific Exception Handling** - Replace `catch(...)` with specific exception types
- **Input Sanitization** - Strengthen regex input validation (ReDoS protection)
- **Security Logging** - Add security event logging for privilege operations

### Long-Term (Future Enhancement)

- **Two-Process Model** - Complete privilege separation (Option 2)
- **Code Signing** - Sign executables to prevent tampering
- **Security Audit** - Regular security reviews and penetration testing

---

## Security Testing

### Test Scenarios

1. **Privilege Drop Failure Simulation**
   - Temporarily modify `DropUnnecessaryPrivileges()` to return `false`
   - Verify application exits immediately with appropriate error message
   - Verify no monitoring threads remain running

2. **Path Validation Testing**
   - Test with normal file paths (should work)
   - Test with UNC paths `\\server\share\file.txt` (should be rejected)
   - Test with path traversal `C:\..\..\Windows\System32\cmd.exe` (should be rejected)

3. **Regression Testing**
   - Verify normal file opening still works
   - Verify context menu still works
   - Verify application starts normally when privileges ARE dropped successfully

---

## References

- **Privilege Dropping Implementation:** `src/platform/windows/PrivilegeUtils.cpp`

- **Windows Privilege Documentation:** https://docs.microsoft.com/en-us/windows/win32/secauthz/privileges

---

## Change Log

- **2026-02-01:** Updated privilege model for Windows
  - Application now starts as standard user (`asInvoker`) by default
  - Administrator privileges are optional and only required for USN Journal monitoring
  - Graceful degradation to folder crawling when running without elevation
- **2026-01-11:** Initial security model documentation
  - Documented privilege model and reduction strategy
  - Documented fatal error handling for privilege drop failure
  - Documented path validation and ShellExecute security
  - Documented attack surface and mitigations
