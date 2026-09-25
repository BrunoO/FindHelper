#pragma once

#include <string>
#include <vector>

// Forward declaration
class FileIndex;

// Command line argument parsing structure
struct CommandLineArgs {
  bool show_help = false;
  bool show_version = false;
  bool exit_requested = false; // Set to true if help/version was shown

  // Optional overrides for settings
  int thread_pool_size_override = -1; // -1 means no override
  int window_width_override = -1; // -1 means no override
  int window_height_override = -1; // -1 means no override

  // Index dump option
  std::string dump_index_to;  // Empty means don't dump

  // Index from file option
  std::string index_from_file;  // Empty means don't use

  // Folder to crawl and index (when no index file is provided)
  std::string crawl_folder;  // Empty means don't crawl

  // Windows-specific: volume to monitor (e.g., "C:", "D:")
  std::string win_volume_override;  // Empty means no override

  // Run ImGui Test Engine tests then exit (for coverage; requires ENABLE_IMGUI_TEST_ENGINE build)
  bool run_imgui_tests_and_exit = false;

  // Full startup command-line arguments excluding program path (argv[0]).
  // Empty means no arguments were provided.
  std::string startup_args_display;

  // Unrecognized arguments (kept for diagnostics; parsing continues).
  // Surfaced on stderr at startup and in Help -> About.
  // Invariant: unknown_args_display is the space-join of unknown_args,
  // built once at the end of ParseCommandLineArgs; do not mutate
  // unknown_args afterwards (use ParseCommandLineArgs to rebuild).
  std::vector<std::string> unknown_args;

  // Space-joined unknown_args for string_view plumbing (owned here).
  std::string unknown_args_display;
};

// Parse command line arguments
CommandLineArgs ParseCommandLineArgs(int argc, char* const* argv);

// Show help message
void ShowHelp(const char* program_name);

// Show version information
void ShowVersion();

// Dump all indexed paths to a file
bool DumpIndexToFile(const FileIndex& file_index, std::string_view file_path);
