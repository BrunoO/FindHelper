#pragma once

/**
 * @file main_common.h
 * @brief Common main entry point template for all platforms
 *
 * This template function eliminates duplication between platform-specific
 * main entry points (main_gui.cpp, main_mac.mm, future main_linux.cpp).
 *
 * DESIGN:
 * - Template parameters: BootstrapResult type and BootstrapTraits
 * - Handles: Command line parsing, help/version, bootstrap initialization,
 *   Application creation and execution, cleanup
 * - Platform-specific differences handled through template parameters
 */

#include <functional>

#include "core/AppBootstrapResultBase.h"
#include "core/Application.h"
#include "core/CommandLineArgs.h"
#include "core/IndexBuilder.h"
#include "core/Settings.h"
#include "index/FileIndex.h"
#include "path/PathUtils.h"
#include "utils/ExceptionHandling.h"
#include "utils/Logger.h"

#ifdef _WIN32
#include "platform/windows/ShellContextUtils.h"
#endif  // _WIN32

namespace main_common_detail {

  /**
   * @brief Execute application main logic
   *
   * Creates index builder, starts index building, creates Application, and runs it.
   *
   * @tparam BootstrapResult The platform-specific bootstrap result type
   * @tparam BootstrapTraits Traits struct with Initialize and Cleanup function pointers
   * @param cmd_args Command line arguments
   * @param file_index File index instance
   * @param bootstrap Bootstrap result
   * @param index_build_state Shared index build state
   * @return Exit code from application
   */
  template<typename BootstrapResult, typename BootstrapTraits>
  int ExecuteApplicationMain(const CommandLineArgs& cmd_args,
                             FileIndex& file_index,
                             BootstrapResult& bootstrap,
                             IndexBuildState& index_build_state) {
    // Decide index building strategy based on platform and command line
    IndexBuilderConfig index_builder_config;
    // Priority: command-line argument > settings > default
    if (!cmd_args.crawl_folder.empty()) {
      index_builder_config.crawl_folder = cmd_args.crawl_folder;
    } else if (bootstrap.settings && !bootstrap.settings->crawlFolder.empty()) {
      index_builder_config.crawl_folder = bootstrap.settings->crawlFolder;
    }
    index_builder_config.index_file_path = cmd_args.index_from_file;

#ifdef _WIN32
    const bool has_crawl_folder = !index_builder_config.crawl_folder.empty();
    index_builder_config.use_usn_monitor = (bootstrap.monitor != nullptr && !has_crawl_folder);
#else
    index_builder_config.use_usn_monitor = false;
#endif  // _WIN32

    // Determine if we should auto-crawl (no --crawl-folder, no USN monitoring, no --index-from-file)
    bool should_auto_crawl = false;
    std::string auto_crawl_folder;
    
    if (cmd_args.crawl_folder.empty() && 
        !index_builder_config.use_usn_monitor && 
        cmd_args.index_from_file.empty()) {
      // Determine which folder to crawl: settings folder or HOME folder
      if (bootstrap.settings && !bootstrap.settings->crawlFolder.empty()) {
        auto_crawl_folder = bootstrap.settings->crawlFolder;
        should_auto_crawl = true;
      } else {
        auto_crawl_folder = path_utils::GetUserHomePath();
        should_auto_crawl = true;
      }
    }

    // Create platform-appropriate index builder (may be nullptr if
    // index_from_file is used exclusively, bootstrap performed all work, or auto-crawl is enabled).
    std::unique_ptr<IIndexBuilder> index_builder;

    // Don't create index builder if we're doing auto-crawl (will be created later in Application)
    if (!should_auto_crawl) {
#ifdef _WIN32
      if (index_builder_config.use_usn_monitor) {
        index_builder = CreateWindowsIndexBuilder(file_index, bootstrap.monitor.get(), index_builder_config);
      } else {
        index_builder = CreateFolderCrawlerIndexBuilder(file_index, index_builder_config);
      }
#else
      // macOS / Linux: use FolderCrawler-based builder when crawl_folder is provided.
      if (!index_builder_config.crawl_folder.empty()) { // NOSONAR(cpp:S1066) - Cannot merge with outer if: platform-specific logic requires separate condition
        index_builder = CreateFolderCrawlerIndexBuilder(file_index, index_builder_config);
      }
#endif  // _WIN32
    }

    // If an index builder exists, prepare state and optionally defer start.
    // Defer folder-crawler start until after first frame so UI is visible before crawl
    // (fixes blank UI on Linux when starting with --crawl-folder; same behavior as macOS).
    bool start_index_build_after_first_frame = false;
    if (index_builder != nullptr) {
      index_build_state.Reset();
      if (index_builder_config.use_usn_monitor) {
        index_build_state.source_description = "USN Journal";
        index_builder->Start(index_build_state);
      } else if (!index_builder_config.crawl_folder.empty()) {
        index_build_state.source_description = "Folder: " + index_builder_config.crawl_folder;
        start_index_build_after_first_frame = true;
        // Do not call index_builder->Start() here; Application::Run() will start after first frame.
      } else {
        index_builder->Start(index_build_state);
      }
    } else {
      // No background builder: treat index as ready (e.g., index_from_file only, or auto-crawl will start later).
      index_build_state.Reset();
      index_build_state.completed.store(true, std::memory_order_relaxed);
    }

    // Create Application instance (owns all components and manages main loop)
    // Pass index_builder to Application (Application now owns it)
    // Pass auto-crawl parameters if auto-crawl should be triggered
    Application app(bootstrap, cmd_args, index_build_state, std::move(index_builder),
                   should_auto_crawl, auto_crawl_folder, start_index_build_after_first_frame);
    
    // Run the application (main loop)
    return app.Run();
  }
} // namespace main_common_detail

/**
 * @brief Unified main entry point template for all platforms
 *
 * This template function provides a common implementation for the main()
 * function across all platforms, eliminating code duplication.
 *
 * @tparam BootstrapResult The platform-specific bootstrap result type
 *                        (AppBootstrapResult, AppBootstrapResultMac, etc.)
 * @tparam BootstrapTraits Traits struct with Initialize and Cleanup function pointers
 * @param argc Command line argument count
 * @param argv Command line argument array
 * @return Exit code (0 for success, non-zero for error)
 */
template<typename BootstrapResult, typename BootstrapTraits>
int RunApplication(int argc, char** argv) {  // NOLINT(google-objc-function-naming)
  exception_handling::InstallTerminateHandler();

  // IMPORTANT: Log hash map and regex implementation at startup
  // Use LOG_IMPORTANT_BUILD so it's visible in both Debug and Release builds
#ifdef FAST_LIBS_BOOST
  LOG_IMPORTANT_BUILD("=== IMPORTANT: Using boost libraries (FAST_LIBS_BOOST enabled) ===");
  LOG_IMPORTANT_BUILD("    Hash maps: boost::unordered_map (~2x faster lookups, ~60% less memory overhead)");
  LOG_IMPORTANT_BUILD("    Regex: boost::regex (better performance than std::regex)");
#else
  LOG_IMPORTANT_BUILD("=== Using standard library (default) ===");
  LOG_IMPORTANT_BUILD("    Hash maps: std::unordered_map");
  LOG_IMPORTANT_BUILD("    Regex: std::regex");
#endif  // FAST_LIBS_BOOST

  // Parse command line arguments
  CommandLineArgs cmd_args = ParseCommandLineArgs(argc, argv);
  
  // Handle help request
  if (cmd_args.show_help) {
    const char* program_name =
        (argc > 0 && argv != nullptr)
            ? argv[0]  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            : "";
    ShowHelp(program_name);
    return 0;
  }
  
  // Handle version request
  if (cmd_args.show_version) {
    ShowVersion();
    return 0;
  }

  // Track last known main window size (updated on GLFW window resize callback).
  // Used to persist window dimensions across sessions without relying on
  // glfwGetWindowSize after the window has been destroyed.
  int last_window_width = settings_defaults::kDefaultWindowWidth;
  int last_window_height = settings_defaults::kDefaultWindowHeight;

  // Create FileIndex (will be passed to AppBootstrap and then owned by Application)
  FileIndex file_index;

  // Shared index build state (used by all platforms to report progress)
  IndexBuildState index_build_state;

#ifdef _WIN32
  // Offer elevation before creating the window when no explicit --crawl-folder.
  // If user accepts, we restart via ShellExecuteEx "runas" (then Windows shows UAC once).
  if (cmd_args.crawl_folder.empty() && !IsProcessElevated() &&
      PromptForElevationAndRestart(nullptr)) {
    return 0;  // Restart was initiated; no bootstrap to clean up
  }
#endif  // _WIN32

  // Initialize all platform-specific subsystems via AppBootstrap
  BootstrapResult bootstrap;
  if (int init_result = exception_handling::RunFatal(
          "in BootstrapTraits::Initialize",
          [&cmd_args, &file_index, &last_window_width, &last_window_height,
           &bootstrap]() {
            bootstrap = std::move(BootstrapTraits::Initialize(
                cmd_args, file_index, last_window_width, last_window_height));
            return 0;  // Success
          },
          1);
      init_result != 0) {
    return init_result;
  }
  
  if (!bootstrap.IsValid()) {
    LOG_ERROR("Failed to initialize application");
    return 1;
  }

#ifdef _WIN32
  // SECURITY: Check for fatal security errors (e.g., privilege drop failure)
  // This must be checked before creating Application to prevent running with elevated privileges
  if (bootstrap.security_fatal_error) {
    LOG_ERROR("FATAL SECURITY ERROR: Application cannot continue safely. Exiting.");
    BootstrapTraits::Cleanup(bootstrap);
    return 1;
  }
#endif  // _WIN32

  return exception_handling::RunFatal(
      "during application execution",
      [&cmd_args, &file_index, &bootstrap, &index_build_state]() {
        int exit_code = main_common_detail::ExecuteApplicationMain<
            BootstrapResult, BootstrapTraits>(cmd_args, file_index, bootstrap,
                                              index_build_state);

        // Cleanup all initialized resources via AppBootstrap
        BootstrapTraits::Cleanup(bootstrap);

        return exit_code;
      },
      1,
      [&bootstrap]() { BootstrapTraits::Cleanup(bootstrap); });
}

