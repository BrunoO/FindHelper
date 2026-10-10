/**
 * @file ExceptionHandling.inl
 * @brief Inline implementations for ExceptionHandling template functions
 *
 * Included by ExceptionHandling.h - do not include directly.
 */

#include <string>

#include "utils/Logger.h"

namespace exception_handling {

namespace detail {

// Standardized format: [operation] ([context]): [category]: [message]. Next: [action]
void LogExceptionStructured(std::string_view operation, std::string_view context,
                           std::string_view category, std::string_view message,
                           std::string_view next_action);

}  // namespace detail

template <typename Op>
int RunFatal(std::string_view context, const Op& op, int exit_code) {
  return RunFatal(context, op, exit_code, []() {});
}

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

template <typename Op, typename OnError>
void RunRecoverable(std::string_view context, const Op& op,
                   const OnError& on_error) {
  constexpr std::string_view kNextAction =
      "setting error state; cancelling search";
  try {
    op();
  } catch (const std::bad_alloc& e) {
    (void)e;  // Suppress unused variable warning in Release mode
    detail::LogExceptionStructured("RunRecoverable", context, "bad_alloc",
                                  e.what(), kNextAction);
    on_error(e.what());
  } catch (const std::system_error& e) {
    (void)e;  // Suppress unused variable warning in Release mode
    detail::LogExceptionStructured("RunRecoverable", context, "system_error",
                                  e.what(), kNextAction);
    on_error(e.what());
  } catch (const std::runtime_error& e) {  // NOSONAR(cpp:S1181) - Part of exception hierarchy
    (void)e;  // Suppress unused variable warning in Release mode
    detail::LogExceptionStructured("RunRecoverable", context, "runtime_error",
                                  e.what(), kNextAction);
    on_error(e.what());
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all for std exceptions
    (void)e;  // Suppress unused variable warning in Release mode
    detail::LogExceptionStructured("RunRecoverable", context, "exception",
                                  e.what(), kNextAction);
    on_error(e.what());
  } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for non-standard exceptions
    detail::LogExceptionStructured("RunRecoverable", context, "unknown", "",
                                  kNextAction);
    on_error("Unknown exception");
  }
}

template <typename DrainOp>
void DrainFutureImpl(const DrainOp& drain_op, std::string_view context) {
  constexpr std::string_view kNextAction = "continuing drain";
  try {
    drain_op();
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all for std exceptions
    (void)e;  // Suppress unused variable warning in Release mode
    detail::LogExceptionStructured("DrainFuture", context, "exception", e.what(),
                                  kNextAction);
  } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for drain; must not propagate
    detail::LogExceptionStructured("DrainFuture", context, "unknown", "",
                                  kNextAction);
  }
}

template <typename T>
void DrainFuture(std::future<T>& f, std::string_view context) {
  DrainFutureImpl([&f]() { (void)f.get(); }, context);
}

}  // namespace exception_handling
