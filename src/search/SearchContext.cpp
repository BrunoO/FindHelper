#include "search/SearchContext.h"

#include "core/Settings.h"
#include "utils/Logger.h"

namespace {

template <typename T>
void ClampAndLog(T& value, T min_bound, T max_bound, const char* name) {
  if (value < min_bound) {
    LOG_WARNING_BUILD(name << " too small (" << value << "), clamped to " << min_bound);
    value = min_bound;
  } else if (value > max_bound) {
    LOG_WARNING_BUILD(name << " too large (" << value << "), clamped to " << max_bound);
    value = max_bound;
  }
}

}  // namespace

void SearchContext::ValidateAndClamp() {
  ClampAndLog(dynamic_chunk_size,
              static_cast<size_t>(settings_defaults::kMinDynamicChunkSize),
              static_cast<size_t>(settings_defaults::kMaxDynamicChunkSize), "dynamic_chunk_size");
  ClampAndLog(hybrid_initial_percent, settings_defaults::kMinHybridInitialWorkPercent,
              settings_defaults::kMaxHybridInitialWorkPercent, "hybrid_initial_percent");
  ClampAndLog(guided_scheduling_divisor, settings_defaults::kMinGuidedSchedulingDivisor,
              settings_defaults::kMaxGuidedSchedulingDivisor, "guided_scheduling_divisor");
}
