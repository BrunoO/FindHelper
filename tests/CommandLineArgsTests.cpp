/**
 * @file CommandLineArgsTests.cpp
 * @brief Unit tests for ParseCommandLineArgs()
 *
 * Tests cover:
 * - Default state (no arguments)
 * - Help / version / metrics / ImGui-test flags
 * - Integer options (thread-pool-size, window-width, window-height):
 *   both inline (--opt=val) and space-separated (--opt val) formats,
 *   valid range, and out-of-range rejection
 * - String options (load-balancing, index-from-file, crawl-folder,
 *   dump-index-to, win-volume): both formats
 * - Unknown arguments are silently ignored
 * - Multiple arguments combined
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <string>
#include <string_view>
#include <vector>

#include "core/CommandLineArgs.h"
#include "core/Settings.h"

namespace {

// Builds an argc/argv pair from an initializer list of string literals.
// Keeps owned storage alive for the duration of the call.
struct Args {
  std::vector<std::string> storage;
  std::vector<char*> ptrs;

  Args(std::initializer_list<std::string_view> args) {  // NOLINT(google-explicit-constructor) - initializer_list ctor; explicit would prevent brace-init
    storage.reserve(args.size());
    for (const auto sv : args) {
      storage.emplace_back(sv);
    }
    ptrs.reserve(storage.size());
    for (auto& s : storage) {
      ptrs.push_back(s.data());
    }
  }

  [[nodiscard]] int argc() const { return static_cast<int>(ptrs.size()); }
  [[nodiscard]] char** argv() { return ptrs.data(); }
};

}  // namespace

TEST_SUITE("ParseCommandLineArgs") {

  TEST_CASE("No arguments returns all defaults") {
    Args args({"program"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());

    CHECK(!result.show_help);
    CHECK(!result.show_version);
    CHECK(!result.show_metrics);
    CHECK(!result.exit_requested);
    CHECK(!result.run_imgui_tests_and_exit);
    CHECK(result.thread_pool_size_override == -1);
    CHECK(result.window_width_override == -1);
    CHECK(result.window_height_override == -1);
    CHECK(result.load_balancing_override.empty());
    CHECK(result.dump_index_to.empty());
    CHECK(result.index_from_file.empty());
    CHECK(result.crawl_folder.empty());
    CHECK(result.win_volume_override.empty());
  }

  // -------------------------------------------------------------------------
  // Exit / toggle flags
  // -------------------------------------------------------------------------

  TEST_CASE("--help sets show_help and exit_requested") {
    Args args({"prog", "--help"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.show_help);
    CHECK(result.exit_requested);
    CHECK(!result.show_version);
  }

  TEST_CASE("-h sets show_help and exit_requested") {
    Args args({"prog", "-h"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.show_help);
    CHECK(result.exit_requested);
  }

  TEST_CASE("-? sets show_help and exit_requested") {
    Args args({"prog", "-?"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.show_help);
    CHECK(result.exit_requested);
  }

  TEST_CASE("--version sets show_version and exit_requested") {
    Args args({"prog", "--version"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.show_version);
    CHECK(result.exit_requested);
    CHECK(!result.show_help);
  }

  TEST_CASE("-v sets show_version and exit_requested") {
    Args args({"prog", "-v"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.show_version);
    CHECK(result.exit_requested);
  }

  TEST_CASE("--show-metrics sets show_metrics but not exit_requested") {
    Args args({"prog", "--show-metrics"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.show_metrics);
    CHECK(!result.exit_requested);
  }

  TEST_CASE("--run-imgui-tests-and-exit sets the flag but not exit_requested") {
    Args args({"prog", "--run-imgui-tests-and-exit"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.run_imgui_tests_and_exit);
    CHECK(!result.exit_requested);
  }

  // -------------------------------------------------------------------------
  // --thread-pool-size
  // -------------------------------------------------------------------------

  TEST_CASE("--thread-pool-size inline format") {
    SUBCASE("valid midrange value") {
      Args args({"prog", "--thread-pool-size=8"});
      const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
      CHECK(result.thread_pool_size_override == 8);
    }
    SUBCASE("minimum value (0 = auto-detect)") {
      Args args({"prog", "--thread-pool-size=0"});
      const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
      CHECK(result.thread_pool_size_override == settings_defaults::kMinThreadPoolSize);
    }
    SUBCASE("maximum value") {
      Args args({"prog", "--thread-pool-size=64"});
      const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
      CHECK(result.thread_pool_size_override == settings_defaults::kMaxThreadPoolSize);
    }
    SUBCASE("out-of-range value is ignored (override stays -1)") {
      Args args({"prog", "--thread-pool-size=999"});
      const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
      CHECK(result.thread_pool_size_override == -1);
    }
  }

  TEST_CASE("--thread-pool-size space-separated format") {
    Args args({"prog", "--thread-pool-size", "4"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.thread_pool_size_override == 4);
  }

  // -------------------------------------------------------------------------
  // --window-width / --window-height
  // -------------------------------------------------------------------------

  TEST_CASE("--window-width inline format") {
    SUBCASE("valid width") {
      Args args({"prog", "--window-width=1920"});
      const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
      CHECK(result.window_width_override == 1920);
    }
    SUBCASE("out-of-range is ignored") {
      Args args({"prog", "--window-width=0"});
      const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
      CHECK(result.window_width_override == -1);
    }
  }

  TEST_CASE("--window-width space-separated format") {
    Args args({"prog", "--window-width", "1280"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.window_width_override == 1280);
  }

  TEST_CASE("--window-height inline format") {
    SUBCASE("valid height") {
      Args args({"prog", "--window-height=1080"});
      const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
      CHECK(result.window_height_override == 1080);
    }
    SUBCASE("out-of-range is ignored") {
      Args args({"prog", "--window-height=0"});
      const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
      CHECK(result.window_height_override == -1);
    }
  }

  TEST_CASE("--window-height space-separated format") {
    Args args({"prog", "--window-height", "768"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.window_height_override == 768);
  }

  // -------------------------------------------------------------------------
  // String options
  // -------------------------------------------------------------------------

  TEST_CASE("--load-balancing inline format") {
    Args args({"prog", "--load-balancing=static"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.load_balancing_override == "static");
  }

  TEST_CASE("--load-balancing space-separated format") {
    Args args({"prog", "--load-balancing", "dynamic"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.load_balancing_override == "dynamic");
  }

  TEST_CASE("--index-from-file inline format") {
    Args args({"prog", "--index-from-file=paths.txt"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.index_from_file == "paths.txt");
  }

  TEST_CASE("--index-from-file space-separated format") {
    Args args({"prog", "--index-from-file", "/tmp/index.txt"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.index_from_file == "/tmp/index.txt");
  }

  TEST_CASE("--crawl-folder inline format") {
    Args args({"prog", "--crawl-folder=/home/user/docs"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.crawl_folder == "/home/user/docs");
  }

  TEST_CASE("--dump-index-to inline format") {
    Args args({"prog", "--dump-index-to=output.txt"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.dump_index_to == "output.txt");
  }

  TEST_CASE("--win-volume inline format") {
    Args args({"prog", "--win-volume=D:"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.win_volume_override == "D:");
  }

  // -------------------------------------------------------------------------
  // Edge cases
  // -------------------------------------------------------------------------

  TEST_CASE("Unknown arguments are silently ignored") {
    Args args({"prog", "--unknown-flag", "--another=value", "bare-arg"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(!result.show_help);
    CHECK(!result.exit_requested);
    CHECK(result.thread_pool_size_override == -1);
  }

  TEST_CASE("Multiple valid arguments are all applied") {
    Args args({"prog",
               "--show-metrics",
               "--thread-pool-size=4",
               "--window-width=1280",
               "--window-height=720",
               "--load-balancing=hybrid"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.show_metrics);
    CHECK(result.thread_pool_size_override == 4);
    CHECK(result.window_width_override == 1280);
    CHECK(result.window_height_override == 720);
    CHECK(result.load_balancing_override == "hybrid");
    CHECK(!result.exit_requested);
  }

  TEST_CASE("Help flag alongside other arguments") {
    Args args({"prog", "--show-metrics", "--help", "--thread-pool-size=4"});
    const CommandLineArgs result = ParseCommandLineArgs(args.argc(), args.argv());
    CHECK(result.show_help);
    CHECK(result.exit_requested);
    CHECK(result.show_metrics);  // processed before --help
    CHECK(result.thread_pool_size_override == 4);  // processed after --help
  }

}  // TEST_SUITE("ParseCommandLineArgs")
