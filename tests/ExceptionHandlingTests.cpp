/**
 * @file ExceptionHandlingTests.cpp
 * @brief Unit tests for exception_handling template utilities
 *
 * Tests cover RunFatal, RunRecoverable, DrainFutureImpl and DrainFuture.
 * All tests are pure logic tests — no mocks, no file I/O, no threads.
 * The LOG_ERROR_BUILD calls inside the handlers are side-effects that do not
 * affect observable test outcomes (they log to a file/stderr in debug builds).
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <future>
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
    const int result = RunFatal("ctx", []() { return 42; }, 1);
    CHECK(result == 42);
  }

  TEST_CASE("successful op with default exit_code returns op's value") {
    const int result = RunFatal("ctx", []() { return 7; });
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
        RunFatal("ctx", []() -> int { throw 42; }, 9);  // NOLINT(hicpp-exception-baseclass) - intentional non-std throw for test coverage
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
        [&cleaned_up]() { cleaned_up = true; });
    CHECK(cleaned_up);
  }

  TEST_CASE("cleanup lambda is NOT called on success") {
    bool cleaned_up = false;
    RunFatal(
        "ctx", []() { return 0; }, 1,
        [&cleaned_up]() { cleaned_up = true; });
    CHECK(!cleaned_up);
  }

}

TEST_SUITE("RunRecoverable") {

  TEST_CASE("successful op does not invoke on_error") {
    bool error_called = false;
    RunRecoverable(
        "ctx",
        []() {
          /* Intentionally empty: success path; on_error must not run (Sonar cpp:S1186). */
        },
        [&error_called](const char* /*msg*/) { error_called = true; });
    CHECK(!error_called);
  }

  TEST_CASE("runtime_error invokes on_error with exception message") {
    std::string captured;
    RunRecoverable(
        "ctx", []() { throw TestCaseException("test error"); },
        [&captured](const char* msg) { captured = msg; });
    CHECK(!captured.empty());
    CHECK(captured.find("test error") != std::string::npos);
  }

  TEST_CASE("bad_alloc invokes on_error") {
    bool error_called = false;
    RunRecoverable(
        "ctx", []() { throw std::bad_alloc(); },
        [&error_called](const char* /*msg*/) { error_called = true; });
    CHECK(error_called);
  }

  TEST_CASE("system_error invokes on_error") {
    bool error_called = false;
    RunRecoverable(
        "ctx",
        []() {
          throw std::system_error(
              std::make_error_code(std::errc::permission_denied));
        },
        [&error_called](const char* /*msg*/) { error_called = true; });
    CHECK(error_called);
  }

  TEST_CASE("unknown exception invokes on_error with 'Unknown exception'") {
    std::string captured;
    RunRecoverable(
        "ctx",
        []() { throw 99; },  // NOLINT(hicpp-exception-baseclass) - intentional non-std throw for test coverage
        [&captured](const char* msg) { captured = msg; });
    CHECK(captured == "Unknown exception");
  }

}

TEST_SUITE("DrainFutureImpl") {

  TEST_CASE("non-throwing op runs to completion") {
    bool ran = false;
    DrainFutureImpl([&ran]() { ran = true; }, "ctx");
    CHECK(ran);
  }

  TEST_CASE("std::exception from op is swallowed") {
    CHECK_NOTHROW(
        DrainFutureImpl(
            []() { throw std::runtime_error("drain error"); }, "ctx"));
  }

  TEST_CASE("unknown exception from op is swallowed") {
    CHECK_NOTHROW(
        DrainFutureImpl(
            []() { throw 42; }, "ctx"));  // NOLINT(hicpp-exception-baseclass) - intentional non-std throw for test coverage
  }

}

TEST_SUITE("DrainFuture") {

  TEST_CASE("ready future is drained without throwing") {
    std::promise<int> p;
    p.set_value(7);
    std::future<int> f = p.get_future();
    CHECK_NOTHROW(DrainFuture(f, "ctx"));
  }

  TEST_CASE("future holding an exception is drained without rethrowing") {
    std::promise<int> p;
    p.set_exception(
        std::make_exception_ptr(std::runtime_error("async error")));
    std::future<int> f = p.get_future();
    CHECK_NOTHROW(DrainFuture(f, "ctx"));
  }

  TEST_CASE("void future is drained without throwing") {
    std::promise<void> p;
    p.set_value();
    std::future<void> f = p.get_future();
    CHECK_NOTHROW(DrainFuture(f, "ctx"));
  }

}
