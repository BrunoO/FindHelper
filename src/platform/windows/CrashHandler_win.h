#pragma once

namespace crash_handler {

/**
 * @brief Install a process-wide unhandled exception filter on Windows.
 *
 * The filter writes a minidump under `<exe-dir>/crash-dumps/`, a `.txt` sidecar
 * with exception code/thread id, and a best-effort IMPORTANT log line.
 * Absence of `=== Logging session ended ===` in the app log strongly indicates
 * this path (or abort/terminate): static Logger destruction did not run.
 */
void InstallCrashHandler();

}  // namespace crash_handler
