/**
 * @file ThreadUtils_linux.cpp
 * @brief Linux implementation of thread utility functions
 */

#include "utils/ThreadUtils.h"
#include "utils/Logger.h"

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

