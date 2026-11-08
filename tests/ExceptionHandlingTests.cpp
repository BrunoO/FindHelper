/**
 * @file ExceptionHandlingTests.cpp
 * @brief Unit tests for exception_handling utilities (RunFatal, LogException, LogUnknownException)
 *
 * All tests are pure logic tests — no mocks, no file I/O, no threads.
 * The LOG_ERROR_BUILD calls inside the handlers are side-effects that do not
 * affect observable test outcomes (they log to a file/stderr in debug builds).
 */

#include "doctest/doctest.h"

#include <stdexcept>
#include <string>
#include <system_error>

#include "utils/ExceptionHandling.h"

using namespace exception_handling;  // NOLINT(google-build-using-namespace) - test scope only

namespace {

// Sonar cpp:S112 — dedicated exception type instead of throwing std::runtime_error in tests.
class TestCaseException : public std::exception {
public:
  explicit TestCaseException(std::string message) : message_(std::move(message)) {}
  [[nodiscard]] const char* what() const noexcept override { return message_.c_str(); }

private:
  std::string message_;
};

}  // namespace

TEST_SUITE("RunFatal") {

  TEST_CASE("successful op returns the op's return value") {
    const int result = RunFatal("ctx", [] { return 42; }, 1);
    CHECK(result == 42);
  }

  TEST_CASE("successful op with default exit_code returns op's value") {
    const int result = RunFatal("ctx", [] { return 7; });
    CHECK(result == 7);
  }

  TEST_CASE("runtime_error returns exit_code") {
    const int result = RunFatal(
        "ctx", []() -> int { throw TestCaseException("boom"); }, 5);
    CHECK(result == 5);
  }

  TEST_CASE("bad_alloc returns exit_code") {
    const int result =
        RunFatal("ctx", []() -> int { throw std::bad_alloc(); }, 7);
    CHECK(result == 7);
  }

  TEST_CASE("system_error returns exit_code") {
    const int result = RunFatal(
        "ctx",
        []() -> int {
          throw std::system_error(
              std::make_error_code(std::errc::io_error));
        },
        3);
    CHECK(result == 3);
  }

  TEST_CASE("unknown exception (non-std) returns exit_code") {
    const int result =
        RunFatal("ctx", []() -> int { throw 42; }, 9);  // NOLINT(bugprone-std-exception-baseclass,hicpp-exception-baseclass) - intentional non-std throw for test coverage
    CHECK(result == 9);
  }

  TEST_CASE("default exit_code is 1 when not specified") {
    const int result =
        RunFatal("ctx", []() -> int { throw TestCaseException("x"); });
    CHECK(result == 1);
  }

  TEST_CASE("cleanup lambda is called on exception") {
    bool cleaned_up = false;
    RunFatal(
        "ctx", []() -> int { throw TestCaseException("boom"); }, 1,
        [&cleaned_up] { cleaned_up = true; });
    CHECK(cleaned_up);
  }

  TEST_CASE("cleanup lambda is NOT called on success") {
    bool cleaned_up = false;
    RunFatal(
        "ctx", [] { return 0; }, 1,
        [&cleaned_up] { cleaned_up = true; });
    CHECK(!cleaned_up);
  }

}

TEST_SUITE("LogException / LogUnknownException") {

  TEST_CASE("LogException does not throw") {
    const std::runtime_error err("something went wrong");
    CHECK_NOTHROW(LogException("TestOp", "unit-test context", err));
  }

  TEST_CASE("LogUnknownException does not throw") {
    CHECK_NOTHROW(LogUnknownException("TestOp", "unit-test context"));
  }

}
