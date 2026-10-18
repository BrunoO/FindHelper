#pragma once

/**
 * @file ExceptionHandling.h
 * @brief Centralized exception handling helpers for consistent error reporting
 *
 * This module provides cross-platform helpers that encapsulate try/catch chains
 * with proper logging.
 *
 * DESIGN:
 * - RunFatal: For bootstrap/main - log, cleanup, and exit
 * - LogException / LogUnknownException: Low-level logging helpers
 * - InstallTerminateHandler: Custom std::terminate handler for fatal error reporting
 */

#include <exception>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace exception_handling {

namespace detail {

// Standardized format: [operation] ([context]): [category]: [message]. Next: [action]
void LogExceptionStructured(std::string_view operation, std::string_view context,
                            std::string_view category, std::string_view message,
                            std::string_view next_action);

}  // namespace detail

/**
 * @brief Installs a custom std::terminate handler for unified fatal error reporting.
 *
 * Call as early as possible in main() (e.g. at start of RunApplication).
 * The handler best-effort logs IMPORTANT via Logger::LogBestEffortFatal, then
 * writes to std::cerr and aborts. Logger may be unavailable or locked — that is OK.
 * Covers: unhandled exceptions in threads, destructor throws, noexcept violations.
 * Note: abort() typically skips static destructors, so `Logging session ended` is absent.
 */
void InstallTerminateHandler();

/**
 * @brief Logs std::exception with context. Cross-platform.
 *
 * @param operation Name of the operation that failed
 * @param context Additional context (e.g., thread index, range)
 * @param e Exception that was caught
 */
void LogException(std::string_view operation, std::string_view context,
                  const std::exception& e);

/**
 * @brief Logs unknown exception with context. Cross-platform.
 *
 * @param operation Name of the operation that failed
 * @param context Additional context (e.g., thread index, range)
 */
void LogUnknownException(std::string_view operation, std::string_view context);

/**
 * @brief Executes op(); on exception: logs, runs cleanup, returns exit_code.
 *
 * Use for fatal paths (bootstrap, main). Catches all exception types.
 * Single NOSONAR in implementation for catch-all.
 *
 * @param context Context string for error messages (e.g., "in BootstrapTraits::Initialize")
 * @param op Callable to execute that may throw, returns exit code (0 on success)
 * @param exit_code Value to return on exception (default 1)
 * @param cleanup Optional callable to run before returning on error
 * @return 0 on success, exit_code on exception
 */
template <typename Op, typename Cleanup>
int RunFatal(std::string_view context, const Op& op, int exit_code,
             const Cleanup& cleanup) {
  const std::string next_action =
      "exiting with code " + std::to_string(exit_code);
  try {
    return op();
  } catch (const std::bad_alloc& e) {
    (void)e;  // Suppress unused variable warning in Release mode
    detail::LogExceptionStructured("RunFatal", context, "bad_alloc", e.what(),
                                   next_action);
    cleanup();
    return exit_code;
  } catch (const std::system_error& e) {
    (void)e;  // Suppress unused variable warning in Release mode
    detail::LogExceptionStructured("RunFatal", context, "system_error", e.what(),
                                   next_action);
    cleanup();
    return exit_code;
  } catch (const std::runtime_error& e) {  // NOSONAR(cpp:S1181) - Part of exception hierarchy
    (void)e;  // Suppress unused variable warning in Release mode
    detail::LogExceptionStructured("RunFatal", context, "runtime_error", e.what(),
                                   next_action);
    cleanup();
    return exit_code;
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all for std exceptions
    (void)e;  // Suppress unused variable warning in Release mode
    detail::LogExceptionStructured("RunFatal", context, "exception", e.what(),
                                   next_action);
    cleanup();
    return exit_code;
  } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for non-standard exceptions
    detail::LogExceptionStructured("RunFatal", context, "unknown", "", next_action);
    cleanup();
    return exit_code;
  }
}

template <typename Op>
int RunFatal(std::string_view context, const Op& op, int exit_code = 1) {
  return RunFatal(context, op, exit_code, [] {});
}

}  // namespace exception_handling
