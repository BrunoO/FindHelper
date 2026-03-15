/**
 * @file ThreadUtils_mac.cpp
 * @brief macOS implementation of thread utility functions
 */

#include "utils/ThreadUtils.h"
#include "utils/Logger.h"

#include <pthread.h>
#include <thread>

void SetThreadName(const char* thread_name) {  // NOLINT(readability-identifier-naming) - macOS pthread_setname_np API
  if (thread_name == nullptr || *thread_name == '\0') {
    LOG_WARNING_BUILD("SetThreadName: called with null or empty threadName");
    return;
  }

  // macOS: Use pthread_setname_np (available on macOS 10.6+)
  // Note: pthread_setname_np has a 64-character limit (including null terminator)
  // Thread names are visible in Instruments, lldb, and Activity Monitor
  const int result = pthread_setname_np(thread_name);
  // NOLINTNEXTLINE(bugprone-branch-clone) - then: log warning; else: log debug (different messages)
  if (result != 0) {
    LOG_WARNING_BUILD("SetThreadName: pthread_setname_np failed; "
                      "result=" << result << " threadName=\"" << thread_name << "\"");
  } else {
    LOG_DEBUG_BUILD("SetThreadName: successfully set thread name to \"" << thread_name << "\"");
  }
}

size_t GetLogicalProcessorCount() {
  const unsigned int n = std::thread::hardware_concurrency();
  return (n != 0) ? static_cast<size_t>(n) : 2;
}

