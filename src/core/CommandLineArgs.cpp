/**
 * @file CommandLineArgs.cpp
 * @brief Implementation of command-line argument parsing
 *
 * This file implements command-line argument parsing for the FindHelper
 * application. It supports various flags for help, version, index dumping,
 * and other startup options.
 *
 * SUPPORTED ARGUMENTS:
 * - --help, -h, -?: Display help message and exit
 * - --version, -v: Display version information and exit
 * - --dump-index <path>: Dump file index to specified path after indexing
 * - Other flags: See ParseCommandLineArgs() implementation
 *
 * USAGE:
 * - Called from main() before application initialization
 * - Returns CommandLineArgs struct with parsed flags
 * - May set exit_requested flag to indicate early termination
 *
 * ERROR HANDLING:
 * - Invalid arguments are logged but don't cause fatal errors
 * - Unknown arguments are ignored (for forward compatibility)
 * - Help and version flags cause immediate exit
 *
 * @see CommandLineArgs.h for struct definition
 * @see main_gui.cpp and main_mac.mm for usage
 * @see Application.cpp for index dumping usage
 */

#include "core/CommandLineArgs.h"

#include <cstdlib>   // For std::atoi
#include <cstring>   // For strcmp, strncmp
#include <fstream>   // For std::ofstream
#include <ios>       // For std::ios_base::failure
#include <iostream>
#include <stdexcept> // For std::runtime_error
#include <string>
#include <string_view>

#include "core/Settings.h"
#include "core/Version.h"
#include "index/FileIndex.h"
#include "utils/Logger.h"

namespace {

// Command-line argument name lengths (including '=') for parsing
constexpr int kThreadPoolSizeArgLen = 20;
constexpr int kLoadBalancingArgLen = 17;
constexpr int kWindowWidthArgLen = 15;
constexpr int kWindowHeightArgLen = 16;
constexpr int kDumpIndexToArgLen = 16;
constexpr int kIndexFromFileArgLen = 18;
constexpr int kCrawlFolderArgLen = 15;
constexpr int kWinVolumeArgLen = 13;

// Configuration for integer argument parsing (pointer avoids ref data member)
struct IntArgConfig {
  int* target;
  int min_val;
  int max_val;
  const char* arg_display_name;
};

// Helper function to parse string arguments (supports both --arg=value and --arg value formats)
// Returns true if argument was parsed, false otherwise
// Modifies i if argument uses separate value format (--arg value)
inline bool ParseStringArg(const char* arg, int& i, int argc,
                           [[maybe_unused]] char** argv,
                           const char* arg_name, int name_len_with_equals,
                           std::string& target) {
  // Check for --arg=value format (use string_view to avoid pointer arithmetic)
  const std::string_view arg_sv(arg);
  if (strncmp(arg, arg_name, name_len_with_equals) == 0) {
    if (arg_sv.size() > static_cast<size_t>(name_len_with_equals)) {
      const std::string_view value_sv = arg_sv.substr(static_cast<size_t>(name_len_with_equals));
      if (!value_sv.empty()) {
        target = std::string(value_sv);
      }
    }
    return true;
  }

  // Check for --arg value format (arg_name includes "=", so we need to remove it)
  if (const std::string arg_name_only(arg_name, name_len_with_equals - 1);
      arg_sv == arg_name_only && i + 1 < argc) {
    target = argv[i + 1];
    ++i;
    return true;
  }

  return false;
}

// Helper function to parse integer arguments with validation (supports both --arg=value and --arg value formats)
// Returns true if argument was parsed, false otherwise
// Modifies i if argument uses separate value format (--arg value)
inline bool ParseIntArg(const char* arg, int& i, int argc,
                        [[maybe_unused]] char** argv,
                        const char* arg_name, int name_len_with_equals,
                        const IntArgConfig& config) {
  // Check for --arg=value format (use string_view to avoid pointer arithmetic)
  std::string_view arg_sv(arg);
  if (const int value{[&arg_sv, name_len_with_equals]() {
        if (arg_sv.size() <= static_cast<size_t>(name_len_with_equals)) {
          return 0;
        }
        return std::atoi(std::string(arg_sv.substr(static_cast<size_t>(name_len_with_equals))).c_str());  // NOLINT(cert-err34-c,bugprone-unchecked-string-to-number-conversion) - atoi acceptable for CLI; 0 on error
      }()};
      strncmp(arg, arg_name, name_len_with_equals) == 0) {
    if (value >= config.min_val && value <= config.max_val) {
      *config.target = value;
    } else {
      LOG_WARNING_BUILD("Invalid " << config.arg_display_name << ": " << value
                        << " (must be " << config.min_val << "-" << config.max_val << ", ignoring)");
    }
    return true;
  }

  // Check for --arg value format
  if (const std::string arg_name_only(arg_name, name_len_with_equals - 1);
      arg_sv == arg_name_only && i + 1 < argc) {

    if (const int value{std::atoi(argv[i + 1])}; value >= config.min_val && value <= config.max_val) {  // NOLINT(cert-err34-c,bugprone-unchecked-string-to-number-conversion) - atoi/argv standard C CLI
      *config.target = value;
      ++i;
    } else {
      LOG_WARNING_BUILD("Invalid " << config.arg_display_name << ": " << value
                        << " (must be " << config.min_val << "-" << config.max_val << ", ignoring)");
      ++i;
    }
    return true;
  }

  return false;
}

// Returns true if arg was handled (help/version/metrics)
bool ProcessExitAndToggleFlags(const char* arg, CommandLineArgs& args) {
  if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0 || strcmp(arg, "-?") == 0) {
    args.show_help = true;
    args.exit_requested = true;
    return true;
  }
  if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
    args.show_version = true;
    args.exit_requested = true;
    return true;
  }
  if (strcmp(arg, "--show-metrics") == 0) {
    args.show_metrics = true;
    return true;
  }
  if (strcmp(arg, "--run-imgui-tests-and-exit") == 0) {
    args.run_imgui_tests_and_exit = true;
    return true;
  }
  return false;
}

// Returns true if arg was handled (int or string options)
bool ProcessOptionArgs(const char* arg, int& i, int argc, char** argv, CommandLineArgs& args) {
  if (ParseIntArg(arg, i, argc, argv, "--thread-pool-size=", kThreadPoolSizeArgLen,
                  {&args.thread_pool_size_override, settings_defaults::kMinThreadPoolSize,
                   settings_defaults::kMaxThreadPoolSize, "thread pool size"})) {
    return true;
  }
  if (ParseStringArg(arg, i, argc, argv, "--load-balancing=", kLoadBalancingArgLen,
                     args.load_balancing_override)) {
    return true;
  }
  if (ParseIntArg(arg, i, argc, argv, "--window-width=", kWindowWidthArgLen,
                  {&args.window_width_override, settings_defaults::kMinWindowWidth, settings_defaults::kMaxWindowWidth, "window width"})) {
    return true;
  }
  if (ParseIntArg(arg, i, argc, argv, "--window-height=", kWindowHeightArgLen,
                  {&args.window_height_override, settings_defaults::kMinWindowHeight, settings_defaults::kMaxWindowHeight, "window height"})) {
    return true;
  }
  if (ParseStringArg(arg, i, argc, argv, "--dump-index-to=", kDumpIndexToArgLen, args.dump_index_to)) {
    return true;
  }
  if (ParseStringArg(arg, i, argc, argv, "--index-from-file=", kIndexFromFileArgLen, args.index_from_file)) {
    return true;
  }
  if (ParseStringArg(arg, i, argc, argv, "--crawl-folder=", kCrawlFolderArgLen, args.crawl_folder)) {
    return true;
  }
  if (ParseStringArg(arg, i, argc, argv, "--win-volume=", kWinVolumeArgLen, args.win_volume_override)) {
    return true;
  }
  return false;
}

}  // anonymous namespace

// Parse command line arguments
CommandLineArgs ParseCommandLineArgs(int argc, char** argv) {
  CommandLineArgs args;

  for (int i = 1; i < argc; ++i) {  // NOSONAR(cpp:S886) - Standard argc/argv loop pattern, index needed for argv access
    const char* arg = argv[i];

    if (ProcessExitAndToggleFlags(arg, args)) {
      continue;
    }
    if (ProcessOptionArgs(arg, i, argc, argv, args)) {
      continue;
    }

    LOG_INFO_BUILD("Unknown command line argument: " << arg);
  }

  return args;
}

// Show help message
void ShowHelp(const char* program_name) {
  std::cout << APP_DISPLAY_NAME << " - USN Journal Search Tool\n";
  std::cout << "\n";
  std::cout << "Usage: " << program_name << " [OPTIONS]\n";
  std::cout << "\n";
  std::cout << "Options:\n";
  std::cout << "  -h, --help                    Show this help message and exit\n";
  std::cout << "  -v, --version                 Show version information and exit\n";
  std::cout << "  --show-metrics                Show Metrics button and window (for power users / debugging)\n";
  std::cout << "  --thread-pool-size=<n>        Override thread pool size (0=auto, 1-"
            << settings_defaults::kMaxThreadPoolSize << ")\n";
  std::cout << "  --load-balancing=<strategy>   Override load balancing strategy\n";
  std::cout << "                                (static, hybrid, dynamic, interleaved"
#if defined(FAST_LIBS_BOOST)
            << ", work_stealing"
#endif  // FAST_LIBS_BOOST
            << ")\n";
  std::cout << "  --window-width=<n>            Override initial window width ("
            << settings_defaults::kMinWindowWidth << "-" << settings_defaults::kMaxWindowWidth << ")\n";
  std::cout << "  --window-height=<n>           Override initial window height ("
            << settings_defaults::kMinWindowHeight << "-" << settings_defaults::kMaxWindowHeight << ")\n";
  std::cout << "  --dump-index-to=<file>        Save all indexed paths to file (one per line)\n";
  std::cout << "  --index-from-file=<file>      Populate index from a text file (one path per line)\n";
  std::cout << "  --crawl-folder=<path>          Folder to crawl and index (alternative to USN Journal).\n";
  std::cout << "                                Platform: macOS/Linux: required if no index file.\n";
  std::cout << "                                Platform: Windows: used when no admin rights and no index file.\n";
  std::cout << "  --win-volume=<volume>         Override volume to monitor (e.g., D:). Platform: Windows only; default C:.\n";
  std::cout << "  --run-imgui-tests-and-exit    Run all ImGui Test Engine tests; the window closes automatically when tests finish.\n";
  std::cout << "                                Requires a build with ENABLE_IMGUI_TEST_ENGINE; the flag is ignored otherwise.\n";
  std::cout << "\n";
  std::cout << "Examples:\n";
  std::cout << "  " << program_name << " --help\n";
  std::cout << "  " << program_name << " --thread-pool-size=8\n";
  std::cout << "  " << program_name << " --load-balancing=dynamic --window-width=1920\n";
  std::cout << "  " << program_name << " --dump-index-to=index_paths.txt\n";
  std::cout << "\n";
}

// Show version information
void ShowVersion() {
  std::cout << APP_DISPLAY_NAME << " version " << APP_VERSION << "\n";
}

// Dump all indexed paths to a file
[[nodiscard]] bool DumpIndexToFile(const FileIndex& file_index, std::string_view file_path) {
  LOG_INFO_BUILD("Dumping index to file: " << file_path);

  try {
    std::ofstream out_file(std::string(file_path), std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out_file.is_open()) {
      LOG_ERROR_BUILD("Failed to open file for writing: " << file_path);
      return false;
    }

    // Iterate over all entries and write paths to file (callback-based, memory efficient)
    // Uses ForEachEntryWithPath to avoid nested lock acquisition
    size_t entry_count = 0;
    file_index.ForEachEntryWithPath([&out_file, &entry_count](uint64_t /*id*/, const FileEntry& /*entry*/, std::string_view path_view) {
      // Convert to string only when writing (required for file output)
      out_file << std::string(path_view) << "\n";
      ++entry_count;
      return true; // Continue iteration
    });

    LOG_INFO_BUILD("Writing " << entry_count << " paths to file...");

    out_file.flush();
    if (!out_file.good()) {
      LOG_ERROR_BUILD("Error writing to file: " << file_path);
      out_file.close();
      return false;
    }

    out_file.close();
    LOG_INFO_BUILD("Successfully dumped " << entry_count << " paths to: " << file_path);
    return true;
  } catch (const std::ios_base::failure& e) {
    LOG_ERROR_BUILD("I/O error while dumping index: " << e.what());
    return false;
  } catch (const std::runtime_error& e) {  // NOSONAR(cpp:S1181) - Generic catch needed: file I/O may throw various runtime errors
    LOG_ERROR_BUILD("Runtime error while dumping index: " << e.what());
    return false;
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Generic catch needed: fallback for any other standard exceptions
    LOG_ERROR_BUILD("Exception while dumping index: " << e.what());
    return false;
  }
}
