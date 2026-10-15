#pragma once

#include <utility>  // For std::pair

// CPU feature detection for runtime optimization selection
// Detects AVX2 support to enable SIMD optimizations
namespace cpu_features {

// Detect if the current CPU supports AVX2 instructions
// Returns true if AVX2 is available, false otherwise
// Thread-safe: result is cached on first call
bool SupportsAVX2();

// Get cached AVX2 support status (faster than SupportsAVX2)
// Use this if you've already called SupportsAVX2() at least once
bool GetAVX2Support();

// Detect if Hyper-Threading (HT) is enabled on the system
// Returns true if HT is enabled, false otherwise
// Thread-safe: result is cached on first call
// Windows only: Uses GetLogicalProcessorInformation API
// On non-Windows or if detection fails, returns false
bool IsHyperThreadingEnabled();

// Get physical and logical core counts
// Returns: {physical_cores, logical_cores}
// If detection fails, returns {0, 0}
// Thread-safe: results are cached on first call
std::pair<unsigned int, unsigned int> GetCoreCounts();

// Get build feature string for UI display
// Returns a cached string describing build configuration:
// - AVX2 compile-time availability (STRING_SEARCH_AVX2_AVAILABLE)
// - AVX2 runtime detection (via SupportsAVX2())
// Returns a static string (e.g., "AVX2-CT:on AVX2-RT:on")
// Thread-safe: result is cached on first call
const char *GetBuildFeatureString();

} // namespace cpu_features
