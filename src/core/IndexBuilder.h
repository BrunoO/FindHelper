#pragma once

/**
 * @file IndexBuilder.h
 * @brief Abstractions for platform-specific index building strategies.
 *
 * The goal of this layer is to provide a single, shared way for the
 * Application and UI to reason about "index building", regardless of
 * whether the underlying implementation uses:
 *  - Windows USN Journal replay (primary path on Windows), or
 *  - FolderCrawler-based directory traversal (macOS/Linux, or Windows fallback).
 *
 * The Application owns exactly one IIndexBuilder instance at a time and
 * drives it via Start()/Stop(), while all progress is reported through
 * IndexBuildState.
 */

#include "core/IndexBuildState.h"

#include <memory>
#include <string>

// Forward declarations
class FileIndex;
class UsnMonitor;

/**
 * @brief Configuration for index building.
 *
 * This struct captures the high-level intent for initial index population:
 *  - Using an index file (index_from_file)
 *  - Crawling a specific folder (--crawl-folder)
 *  - Using USN-based replay (Windows default when monitor is available)
 *
 * Not all fields are used on all platforms.
 */
struct IndexBuilderConfig {  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init) - std::string members default-constructed
  std::string crawl_folder;       ///< Root folder for FolderCrawler (macOS/Linux, Windows fallback)
  std::string index_file_path;    ///< Path to an index file (used for pre-built index)
  bool use_usn_monitor = false;    ///< Windows: use USN-based initial index build
};

/**
 * @brief Abstract interface for index building strategies.
 *
 * Implementations must be thread-safe with respect to:
 *  - Start() being called once before the main loop
 *  - Stop() being called once during shutdown
 *
 * All UI-visible state MUST be communicated via IndexBuildState; no
 * implementation is allowed to call ImGui or touch GUI types directly.
 */
class IIndexBuilder {
public:
  virtual ~IIndexBuilder() = default;
  
protected:
  // Protected default constructor (interface class)
  IIndexBuilder() = default;
  
public:
  // Non-copyable, non-movable (interface class)
  IIndexBuilder(const IIndexBuilder&) = delete;
  IIndexBuilder& operator=(const IIndexBuilder&) = delete;
  IIndexBuilder(IIndexBuilder&&) = delete;
  IIndexBuilder& operator=(IIndexBuilder&&) = delete;

  /**
   * @brief Start index building in the background (non-blocking).
   *
   * Implementations typically spawn one or more background threads and update
   * the provided IndexBuildState until completion or cancellation.
   *
   * @param state Shared index build state for progress reporting.
   */
  virtual void Start(IndexBuildState& state) = 0;

  /**
   * @brief Request cancellation and join any background threads.
   *
   * Implementations should:
   *  - Set state.cancel_requested = true
   *  - Join all worker threads before returning
   */
  virtual void Stop() = 0;
};

/**
 * @brief Factory helpers for platform-specific builders.
 *
 * These functions are implemented in platform-specific translation units.
 * The core Application only depends on this declaration to construct the
 * appropriate builder at runtime.
 */

// Windows-specific builder using USN monitor (declared for all platforms, defined on Windows)
std::unique_ptr<IIndexBuilder> CreateWindowsIndexBuilder(FileIndex& file_index,  // NOLINT(google-objc-function-naming) - C++ factory, not Objective-C
                                                         UsnMonitor* monitor,
                                                         const IndexBuilderConfig& config);

// FolderCrawler-based builder (implemented in a platform-agnostic way, used on macOS/Linux and as fallback)
std::unique_ptr<IIndexBuilder> CreateFolderCrawlerIndexBuilder(FileIndex& file_index,  // NOLINT(google-objc-function-naming) - C++ factory, not Objective-C
                                                               const IndexBuilderConfig& config);


