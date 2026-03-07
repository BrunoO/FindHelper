#pragma once

#include "utils/FileTimeTypes.h"
#include "utils/FileAttributeConstants.h"
#include <cstdint>

/**
 * @file LazyValue.h
 * @brief Minimal wrapper for lazy-loaded values with sentinel state checks
 *
 * This class addresses the "Missing Abstraction for Lazy-Loaded Attributes"
 * code smell by encapsulating sentinel value checks. It does NOT handle
 * loading logic or thread-safety - those remain in FileIndex methods.
 *
 * DESIGN PRINCIPLES:
 * - Minimal abstraction: Only encapsulates sentinel value checks
 * - No loading logic: Loading remains in FileIndex (preserves double-check locking)
 * - No thread-safety: Thread-safety handled by FileIndex locks
 * - Zero overhead: Inline methods, no virtual functions, no allocations
 *
 * USAGE:
 * - Replace: `if (fileSize != kFileSizeNotLoaded)` with `if (!fileSize.IsNotLoaded())`
 * - Replace: `if (fileSize == kFileSizeFailed)` with `if (fileSize.IsFailed())`
 * - Direct value access: `fileSize.value` or `fileSize.GetValue()`
 */

/**
 * Minimal wrapper for lazy-loaded uint64_t values (file size)
 * Encapsulates sentinel value checks without changing loading logic
 */
class LazyFileSize {
public:
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - POD-like wrapper, public value is intentional
  uint64_t value = kFileSizeNotLoaded;

  // Default constructor - value initialized in-class above
  LazyFileSize() = default;

  // Constructor with initial value
  explicit LazyFileSize(uint64_t val) : value(val) {}

  // Check if value is not yet loaded
  [[nodiscard]] bool IsNotLoaded() const { return value == kFileSizeNotLoaded; }

  // Check if load attempt failed
  [[nodiscard]] bool IsFailed() const { return value == kFileSizeFailed; }

  // Check if value is loaded (not sentinel)
  [[nodiscard]] bool IsLoaded() const {
    return value != kFileSizeNotLoaded && value != kFileSizeFailed;
  }

  // Get value (direct access - caller must check state)
  [[nodiscard]] uint64_t GetValue() const { return value; }

  // Set value directly (for loading)
  void SetValue(uint64_t val) { value = val; }

  // Mark as failed
  void MarkFailed() { value = kFileSizeFailed; }

  // Get value or return 0 if failed/not-loaded (convenience for UI)
  [[nodiscard]] uint64_t GetValueOrZero() const {
    return (value == kFileSizeNotLoaded || value == kFileSizeFailed) ? 0 : value;
  }

  // Implicit conversion to uint64_t (intentionally used throughout codebase, e.g. size = attrs.fileSize)
  operator uint64_t() const { return value; }  // NOSONAR(cpp:S1709) NOLINT(google-explicit-constructor,hicpp-explicit-conversions) - implicit conversion used at call sites

  // Assignment operator
  LazyFileSize& operator=(uint64_t val) {
    value = val;
    return *this;
  }
};

/**
 * Minimal wrapper for lazy-loaded FILETIME values (modification time)
 * Encapsulates sentinel value checks without changing loading logic
 */
class LazyFileTime {
public:
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - POD-like wrapper, public value is intentional
  FILETIME value = kFileTimeNotLoaded;

  // Default constructor - value initialized in-class above
  LazyFileTime() = default;

  // Constructor with initial value
  explicit LazyFileTime(const FILETIME& val) : value(val) {}

  // Check if value is not yet loaded
  [[nodiscard]] bool IsNotLoaded() const { return IsSentinelTime(value); }

  // Check if load attempt failed
  [[nodiscard]] bool IsFailed() const { return IsFailedTime(value); }

  // Check if value is loaded (not sentinel, not failed)
  [[nodiscard]] bool IsLoaded() const { return IsValidTime(value); }

  // Get value (direct access - caller must check state)
  [[nodiscard]] const FILETIME& GetValue() const { return value; }

  // Set value directly (for loading)
  void SetValue(const FILETIME& val) { value = val; }

  // Mark as failed
  void MarkFailed() { value = kFileTimeFailed; }

  // Implicit conversion to FILETIME (intentionally used throughout codebase)
  explicit operator const FILETIME&() const { return value; }  // NOSONAR(cpp:S1709) - explicit to satisfy clang-tidy; conversion used at call sites

  // Assignment operator
  LazyFileTime& operator=(const FILETIME& val) {
    value = val;
    return *this;
  }
};

