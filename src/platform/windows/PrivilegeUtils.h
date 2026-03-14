#pragma once

/**
 * @file PrivilegeUtils.h
 * @brief Utilities for managing Windows process privileges
 *
 * This module provides functions to drop unnecessary privileges after
 * privileged operations (like opening volume handles) are complete.
 * This follows the principle of least privilege to reduce attack surface.
 *
 * DESIGN DECISION: Option 1 - Disable Specific Privileges
 * ========================================================
 *
 * We have implemented Option 1 (disable specific privileges) rather than
 * Option 2 (two-process model) for the following reasons:
 *
 * 1. **Security Benefit**: Disabling unnecessary privileges (SE_DEBUG,
 *    SE_TAKE_OWNERSHIP, SE_SECURITY, SE_BACKUP, SE_RESTORE) significantly
 *    reduces the attack surface. If another vulnerability is found, an
 *    attacker cannot use these privileges.
 *
 * 2. **Implementation Complexity**: Option 1 requires minimal architectural
 *    changes (2-3 days) vs Option 2 which requires a complete two-process
 *    architecture with IPC (1-2 weeks).
 *
 * 3. **Handle Validity**: Windows checks privileges at handle creation time,
 *    not at handle use time. The volume handle opened with admin privileges
 *    remains valid and usable after dropping privileges, so continuous USN
 *    Journal monitoring continues to work.
 *
 * 4. **Risk vs Reward**: Option 1 provides meaningful security improvement
 *    with low risk and low effort. Option 2 provides complete privilege
 *    separation but requires major refactoring and introduces IPC overhead.
 *
 * LIMITATIONS:
 * - The process still runs with admin privileges overall (the process token
 *   is still elevated). This is a limitation of Windows privilege model:
 *   privileges are process-wide, not thread-specific.
 * - Complete privilege separation would require Option 2 (two-process model),
 *   which is a future architectural enhancement.
 *
 * FUTURE ENHANCEMENT (Option 2 - Two-Process Model):
 * ===================================================
 * For a future version, consider implementing complete privilege separation
 * using a two-process architecture:
 *
 * 1. Unprivileged Main Process:
 *    - Runs UI, search, file operations without admin privileges
 *    - Handles all user interactions and file operations
 *    - Communicates with privileged service via IPC
 *
 * 2. Privileged Service Process:
 *    - Small, minimal process that only handles USN Journal operations
 *    - Opens volume handle and reads USN Journal entries
 *    - Communicates results back to main process via IPC (named pipes/sockets)
 *    - Minimal attack surface (only USN Journal access code)
 *
 * Benefits:
 * - Complete privilege separation (main process fully unprivileged)
 * - Minimal attack surface for privileged code (only small service process)
 * - Better security model (if main app is compromised, attacker doesn't get admin)
 * - Service can be restarted independently
 *
 * Implementation Requirements:
 * - IPC communication layer (named pipes or sockets)
 * - Service installation and lifecycle management
 * - Protocol for USN Journal data transfer
 * - Error handling and reconnection logic
 * - Estimated effort: 1-2 weeks
 *
 * This is NOT planned for the current version but may be considered for
 * a future major release when architectural refactoring is appropriate.
 *
 * See docs/security/PRIVILEGE_DROPPING_STATUS.md for detailed comparison of options.
 */

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <vector>
#include <string>

namespace privilege_utils {
  /**
   * @brief Drop unnecessary privileges after volume handle acquisition
   *
   * Should be called after opening the volume handle for USN Journal access.
   * Disables privileges that are not needed for USN Journal operations, reducing
   * the attack surface if another vulnerability is found.
   *
   * Privileges disabled:
   * - SE_DEBUG_PRIVILEGE: Debug programs (not needed for USN Journal)
   * - SE_TAKE_OWNERSHIP_PRIVILEGE: Take ownership of files (not needed)
   * - SE_SECURITY_PRIVILEGE: Manage auditing and security log (not needed)
   * - SE_BACKUP_PRIVILEGE: Backup files (not needed for read-only USN access)
   * - SE_RESTORE_PRIVILEGE: Restore files (not needed)
   *
   * Note: The volume handle opened with admin privileges remains valid and
   * usable after dropping privileges. Windows checks privileges at handle
   * creation time, not at handle use time.
   *
   * @return true if privileges were successfully dropped (or weren't enabled),
   *         false on error
   */
  bool DropUnnecessaryPrivileges();

  /**
   * @brief Get list of currently enabled privileges (for debugging)
   *
   * @return Vector of privilege names that are currently enabled
   */
  std::vector<std::string> GetCurrentPrivileges();
} // namespace privilege_utils

#endif  // _WIN32

