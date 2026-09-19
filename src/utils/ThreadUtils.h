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

// ---------------------------------------------------------------------------
// UI-thread ownership guard
// ---------------------------------------------------------------------------

// Thread-local flag: true only on the thread that called MarkCurrentThreadAsUI().
// inline thread_local: one bool per thread, ODR-safe in header (Sonar S6018).
inline thread_local bool tl_ui_thread = false;  // NOSONAR(cpp:S5421) NOLINT(cppcoreguidelines-avoid-non-const-global-variables) - per-thread mutable UI marker

// Mark the calling thread as the UI thread. Call once at application startup
// on the main/render thread before any GuiState writes occur.
inline void MarkCurrentThreadAsUI() {
  tl_ui_thread = true;
}

// Returns true when called from the UI thread (i.e. the thread that called
// MarkCurrentThreadAsUI). Used by UIThreadOwned<T> debug assertions.
inline bool IsUIThread() {
  return tl_ui_thread;
}

// Helper function to set thread name for debugging (cross-platform)
// Windows: Uses SetThreadDescription API (Windows 10 version 1607+)
// macOS: Uses pthread_setname_np (available on macOS 10.6+)
// Linux: Not supported (logs debug message)
// This makes threads visible with names in debuggers and profilers
void SetThreadName(const char* thread_name);

// Lower the calling thread's scheduling priority so the OS favours the render/UI thread
// when CPU is contested. Call once at worker thread startup (after SetThreadName).
// Windows: THREAD_PRIORITY_BELOW_NORMAL  macOS/Linux: nice +5
void SetWorkerThreadPriorityBelowNormal();

/**
 * @brief Returns the number of logical processors (CPU cores) for thread pool sizing.
 *
 * Uses std::thread::hardware_concurrency() where available. On Linux, when that
 * returns 0 (e.g. in some cgroups/Docker environments), falls back to
 * sysconf(_SC_NPROCESSORS_ONLN). Returns at least 2 when detection fails so
 * that search and crawl retain parallelism.
 */
[[nodiscard]] size_t GetLogicalProcessorCount();
