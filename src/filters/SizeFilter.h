#pragma once

/**
 * @file SizeFilter.h
 * @brief SizeFilter enum and utility functions for file size quick filters
 */

#include <cstdint>
#include <string_view>

// Size filter options for file size quick filters
// NOLINTNEXTLINE(performance-enum-size) - int base type is acceptable for enum, provides better compatibility
enum class SizeFilter {
  None,     // No filter
  Empty,    // = 0 bytes (empty files)
  Tiny,     // < 1 KB (config files, shortcuts)
  Small,    // 1 KB - 100 KB (small documents, text files)
  Medium,   // 100 KB - 10 MB (photos, PDFs)
  Large,    // 10 MB - 100 MB (videos, archives)
  Huge,     // 100 MB - 1 GB (movies, game files)
  Massive   // > 1 GB (VMs, ISOs, databases)
};

// Size constants in bytes (using constexpr for compile-time evaluation)
namespace size_filter_constants {
  constexpr uint64_t kOneKB = 1024ULL;
  constexpr uint64_t kOneHundredKB = 100ULL * kOneKB;
  constexpr uint64_t kOneMB = 1024ULL * kOneKB;
  constexpr uint64_t kTenMB = 10ULL * kOneMB;
  constexpr uint64_t kOneHundredMB = 100ULL * kOneMB;
  constexpr uint64_t kOneGB = 1024ULL * kOneMB;
}  // namespace size_filter_constants

/**
 * @brief Map SizeFilter enum to persisted string value
 *
 * Converts a SizeFilter enum value to its string representation for persistence
 * in settings files.
 *
 * @param filter SizeFilter enum value
 * @return String representation (e.g., "Empty", "Tiny", "None")
 */
const char *SizeFilterToString(SizeFilter filter);

/**
 * @brief Map persisted string back to SizeFilter enum
 *
 * Converts a string value from settings files back to a SizeFilter enum.
 * Returns SizeFilter::None if the string is unrecognized.
 *
 * @param value String representation (e.g., "Empty", "Tiny")
 * @return SizeFilter enum value, or SizeFilter::None if unrecognized
 */
SizeFilter SizeFilterFromString(std::string_view value);

/**
 * @brief Get the display label for a size filter
 *
 * Returns a human-readable label including the size range for UI display.
 *
 * @param filter SizeFilter enum value
 * @return Display label (e.g., "Tiny (< 1 KB)", "Large (10-100 MB)")
 */
const char *SizeFilterDisplayLabel(SizeFilter filter);

/**
 * @brief Check if a file size matches the given size filter
 *
 * Evaluates whether a file's size falls within the range defined by the filter:
 * - Empty: size == 0
 * - Tiny: 0 < size < 1 KB
 * - Small: 1 KB <= size < 100 KB
 * - Medium: 100 KB <= size < 10 MB
 * - Large: 10 MB <= size < 100 MB
 * - Huge: 100 MB <= size < 1 GB
 * - Massive: size >= 1 GB
 *
 * @param file_size Size of the file in bytes
 * @param filter SizeFilter enum value
 * @return true if the file size matches the filter criteria
 */
bool MatchesSizeFilter(uint64_t file_size, SizeFilter filter);
