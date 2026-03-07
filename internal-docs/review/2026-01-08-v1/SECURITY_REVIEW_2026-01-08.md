# Security Review - 2026-01-08

## Security Posture Score: 3/10

**Justification**: The application has a critical privilege escalation vulnerability due to the use of `ShellExecute` while running with elevated privileges. This single issue significantly compromises the security of the system, as it allows for arbitrary code execution with administrator rights.

## Critical Vulnerabilities

### 1. CWE-78: Improper Neutralization of Special Elements used in an OS Command ('OS Command Injection')
- **Vulnerability**: Privilege Escalation
- **Location**: `src/platform/windows/FileOperations_win.cpp`
- **Attack Vector**: A user can open a file that has a malicious file type association (e.g., a `.txt` file that executes a script). Because the application is running with elevated privileges, the associated application will also be launched with elevated privileges.
- **Impact**: Privilege Escalation
- **Proof of Concept**:
  1.  Associate a common file type like `.txt` with a malicious executable.
  2.  Run the FindHelper application as an administrator.
  3.  Open a `.txt` file from within the application.
  4.  The malicious executable will be launched with administrator privileges.
- **Remediation**:
  - **Drop Privileges**: The application should drop privileges after it has opened the handle to the USN Journal. This can be done by creating a less-privileged token and using it to launch the UI part of the application.
  - **Use `ShellExecute` with a less-privileged user**: If dropping privileges for the entire application is not feasible, the `ShellExecute` function can be called in a way that it runs as the logged-in user, not the elevated administrator.
- **Severity**: Critical

## Hardening Recommendations

- **Principle of Least Privilege**: The application should only run with elevated privileges for the parts of the code that absolutely require them. The majority of the application, especially the UI, should run with standard user privileges.
- **Input Validation**: All user-supplied input, including search queries and file paths, should be strictly validated to prevent injection attacks.
- **Code Signing**: The application executable should be signed to prevent tampering.
