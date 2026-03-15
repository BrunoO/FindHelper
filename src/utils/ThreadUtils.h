#pragma once

#include <cstddef>

/**
 * @file ThreadUtils.h
 * @brief Cross-platform thread utility functions
 *
 * This header provides utility functions for thread management that work
 * across Windows, macOS, and Linux. Platform-specific implementations are
 * in separate .cpp files.
 */

// Helper function to set thread name for debugging (cross-platform)
// Windows: Uses SetThreadDescription API (Windows 10 version 1607+)
// macOS: Uses pthread_setname_np (available on macOS 10.6+)
// Linux: Not supported (logs debug message)
// This makes threads visible with names in debuggers and profilers
void SetThreadName(const char* thread_name);

/**
 * @brief Returns the number of logical processors (CPU cores) for thread pool sizing.
 *
 * Uses std::thread::hardware_concurrency() where available. On Linux, when that
 * returns 0 (e.g. in some cgroups/Docker environments), falls back to
 * sysconf(_SC_NPROCESSORS_ONLN). Returns at least 2 when detection fails so
 * that search and crawl retain parallelism.
 */
[[nodiscard]] size_t GetLogicalProcessorCount();
