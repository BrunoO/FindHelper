#include "search/SearchContext.h"

#include "utils/Logger.h"

namespace {

template <typename T>
void ClampAndLog(T& value, const T bound_min, const T bound_max, const char* name) {
  if (value < bound_min) {
    LOG_WARNING_BUILD(name << " too small (" << value << "), clamped to " << bound_min);
    value = bound_min;
  } else if (value > bound_max) {
    LOG_WARNING_BUILD(name << " too large (" << value << "), clamped to " << bound_max);
    value = bound_max;
  }
}

}  // namespace

void SearchContext::ValidateAndClamp() {
  constexpr size_t kMinDynamicChunkSize = 100;       // NOLINT(readability-magic-numbers) - Minimum chunk size
  constexpr size_t kMaxDynamicChunkSize = 100000;  // NOLINT(readability-magic-numbers) - Maximum chunk size (100K for testing)
  constexpr int kMinHybridInitialPercent = 50;       // NOLINT(readability-magic-numbers) - Minimum percentage
  constexpr int kMaxHybridInitialPercent = 95;       // NOLINT(readability-magic-numbers) - Maximum percentage
  constexpr int kMinGuidedSchedulingDivisor = 1;     // NOLINT(readability-magic-numbers)
  constexpr int kMaxGuidedSchedulingDivisor = 8;       // NOLINT(readability-magic-numbers)

  ClampAndLog(dynamic_chunk_size, kMinDynamicChunkSize, kMaxDynamicChunkSize, "dynamic_chunk_size");
  ClampAndLog(hybrid_initial_percent, kMinHybridInitialPercent, kMaxHybridInitialPercent,
              "hybrid_initial_percent");
  ClampAndLog(guided_scheduling_divisor, kMinGuidedSchedulingDivisor, kMaxGuidedSchedulingDivisor,
              "guided_scheduling_divisor");
}
