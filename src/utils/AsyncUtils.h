#pragma once

#include <exception>
#include <functional>
#include <future>
#include <string>
#include <utility>

#include "utils/Logger.h"

namespace async_utils {

/**
 * Executes a function/lambda and catches standard exceptions, logging potential errors.
 * Useful for executing future cleanup or other potentially throwing callbacks where
 * individual exception handling is identical.
 *
 * @param func The function to execute
 * @param error_prefix The prefix to add to log messages (e.g., "Error during cleanup: ")
 */
template <typename Func>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward,misc-unused-parameters) - func forwarded via std::invoke; both checks false positive
void ExecuteWithLogCatch(Func&& func, const char* error_prefix = "Error") {
  try {
    std::invoke(std::forward<Func>(func));
  } catch (const std::future_error& e) {
    LOG_ERROR_BUILD(error_prefix << " (future error): " << e.what());
  } catch (const std::exception& e) {  // NOSONAR(cpp:S2738) Intentional catch-all; API accepts arbitrary callables that may throw any exception type
    LOG_ERROR_BUILD(error_prefix << " (exception): " << e.what());
  } catch (...) {  // NOSONAR(cpp:S2738) - Final catch-all required; must not propagate into caller
    LOG_ERROR_BUILD(error_prefix << " (unknown exception)");
  }
}

}  // namespace async_utils
