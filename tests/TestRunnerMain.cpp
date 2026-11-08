/**
 * @file TestRunnerMain.cpp
 * @brief Centralized doctest runner main function for unit tests.
 *
 * Compiles the doctest test engine (CLI parser, reporter, exception handlers)
 * once into test_doctest_main_obj instead of recompiling it across dozens of
 * test translation units.
 */

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "utils/ThreadUtils.h"

int main(int argc, char** argv) {
  MarkCurrentThreadAsUI();
  doctest::Context context(argc, argv);
  return context.run();
}
