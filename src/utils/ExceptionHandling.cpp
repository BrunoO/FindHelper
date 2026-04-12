#include "utils/ExceptionHandling.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "utils/Logger.h"

namespace exception_handling {

namespace {

[[noreturn]] void TerminateHandler() {
  std::cerr << "Fatal: std::terminate called (unhandled exception or destructor throw)\n";
  std::cerr.flush();
  std::abort();
}

}  // namespace

namespace detail {

// Standardized format: [operation] ([context]): [category]: [message]. Next: [action]
void LogExceptionStructured(std::string_view operation, std::string_view context,
                           std::string_view category, std::string_view message,
                           std::string_view next_action) {
  const std::string_view display_msg =
      message.empty() ? "unknown exception" : message;
  LOG_ERROR_BUILD(operation << " (" << context << "): " << category << ": "
                            << display_msg << ". Next: " << next_action);
}

}  // namespace detail

void InstallTerminateHandler() {
  std::set_terminate(TerminateHandler);  // NOLINT(readability-identifier-naming) - function name; PascalCase for terminate handler
}

void LogException(std::string_view operation, std::string_view context,
                  const std::exception& e) {
  detail::LogExceptionStructured(operation, context, "exception", e.what(),
                                "see stack trace");
}

void LogUnknownException(std::string_view operation, std::string_view context) {
  detail::LogExceptionStructured(operation, context, "unknown", "",
                                "see stack trace");
}

}  // namespace exception_handling
