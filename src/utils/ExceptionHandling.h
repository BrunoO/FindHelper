#pragma once

/**
 * @file ExceptionHandling.h
 * @brief Centralized exception handling helpers for consistent error reporting
 *
 * This module provides cross-platform helpers that encapsulate try/catch chains
 * with proper logging. All NOSONAR for catch-all blocks is centralized here.
 *
 * DESIGN:
 * - RunFatal: For bootstrap/main - log and exit
 * - RunRecoverable: For worker threads - log and set error state, no rethrow
 * - DrainFuture: For shutdown - drain future.get(), log on exception
 * - LogException / LogUnknownException: Low-level logging helpers
 *
 * See docs/review/2026-02-14-v2/2026-02-14_EXCEPTION_HANDLING_IMPROVEMENT_PROPOSAL.md
 */

#include <exception>
#include <future>
#include <string_view>

namespace exception_handling {

/**
 * @brief Installs a custom std::terminate handler for unified fatal error reporting.
 *
 * Call as early as possible in main() (e.g. at start of RunApplication).
 * The handler logs to std::cerr and aborts; does not use Logger (may not be ready).
 * Covers: unhandled exceptions in threads, destructor throws, noexcept violations.
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
template <typename Op>
int RunFatal(std::string_view context, const Op& op, int exit_code = 1);

template <typename Op, typename Cleanup>
int RunFatal(std::string_view context, const Op& op, int exit_code,
             const Cleanup& cleanup);

/**
 * @brief Executes op(); on exception: logs, calls on_error(msg), does NOT rethrow.
 *
 * Use for worker threads / recoverable paths. Catches all exception types.
 * Single NOSONAR in implementation for catch-all.
 *
 * @param context Context string for error messages
 * @param op Callable to execute that may throw
 * @param on_error Called with error message; e.g. [](auto msg) { collector.SetError(msg); }
 */
template <typename Op, typename OnError>
void RunRecoverable(std::string_view context, const Op& op,
                    const OnError& on_error);

/**
 * @brief Drains future.get(); logs on exception. For shutdown/cleanup paths.
 *
 * Use when draining futures during teardown. Logs if Logger is available.
 * For static destruction (Logger may be destroyed), use empty catch with NOSONAR.
 *
 * @param f Future to drain (must be valid)
 * @param context Context for log messages
 */
template <typename T>
void DrainFuture(std::future<T>& f, std::string_view context);  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming) - function template, not variable; PascalCase API

/**
 * @brief Internal: drains a callable (e.g. future.get()); logs on exception.
 * Used by DrainFuture template.
 */
template <typename DrainOp>
void DrainFutureImpl(const DrainOp& drain_op, std::string_view context);

}  // namespace exception_handling

#include "utils/ExceptionHandling.inl"
