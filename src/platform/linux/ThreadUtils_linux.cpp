/**
 * @file ThreadUtils_linux.cpp
 * @brief Linux implementation of thread utility functions
 */

#include "utils/ThreadUtils.h"
#include "utils/Logger.h"

#include <thread>
#include <unistd.h>

void SetThreadName(const char* thread_name) {  // NOLINT(readability-identifier-naming) - snake_case for parameters
  if (thread_name == nullptr || *thread_name == '\0') {
    LOG_WARNING_BUILD("SetThreadName: called with null or empty threadName");
    return;
  }

  // On Linux, thread naming is not directly supported via pthread
  // pthread_setname_np exists but requires a pthread_t handle
  // For simplicity, we log that it's not supported
  // Thread naming is primarily a debugging aid
  LOG_DEBUG_BUILD("SetThreadName: not supported on this platform; "
                  "threadName=\"" << thread_name << "\"");
}

size_t GetLogicalProcessorCount() {
  if (const unsigned int n = std::thread::hardware_concurrency(); n != 0) {
    return static_cast<size_t>(n);
  }
  // Fallback when hardware_concurrency() returns 0 (e.g. cgroups/Docker)
  if (const long nproc = sysconf(_SC_NPROCESSORS_ONLN); nproc > 0) {
    return static_cast<size_t>(nproc);
  }
  return 2;
}

