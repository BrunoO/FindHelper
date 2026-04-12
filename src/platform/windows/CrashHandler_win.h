#pragma once

namespace crash_handler {

/**
 * @brief Install a process-wide unhandled exception filter on Windows.
 *
 * The filter writes a minidump file when the process crashes due to an
 * unhandled structured exception (e.g. access violation).
 */
void InstallCrashHandler();

}  // namespace crash_handler
