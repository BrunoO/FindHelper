/**
 * @file SizeFilterUtils.cpp
 * @brief Implementation of SizeFilter utility functions
 *
 * This module implements:
 * - SizeFilter enum to/from string conversion
 * - Size matching logic for each filter category
 */

#include "filters/SizeFilterUtils.h"

#include <cassert>

const char *SizeFilterToString(SizeFilter filter) {
  switch (filter) {
  case SizeFilter::Empty:
    return "Empty";
  case SizeFilter::Tiny:
    return "Tiny";
  case SizeFilter::Small:
    return "Small";
  case SizeFilter::Medium:
    return "Medium";
  case SizeFilter::Large:
    return "Large";
  case SizeFilter::Huge:
    return "Huge";
  case SizeFilter::Massive:
    return "Massive";
  case SizeFilter::None:
  default:
    return "None";
  }
}

SizeFilter SizeFilterFromString(std::string_view value) {
  if (value == "Empty") {
    return SizeFilter::Empty;
  }
  if (value == "Tiny") {
    return SizeFilter::Tiny;
  }
  if (value == "Small") {
    return SizeFilter::Small;
  }
  if (value == "Medium") {
    return SizeFilter::Medium;
  }
  if (value == "Large") {
    return SizeFilter::Large;
  }
  if (value == "Huge") {
    return SizeFilter::Huge;
  }
  if (value == "Massive") {
    return SizeFilter::Massive;
  }
  return SizeFilter::None;
}

const char *SizeFilterDisplayLabel(SizeFilter filter) {
  switch (filter) {
  case SizeFilter::Empty:
    return "Empty (0 bytes)";
  case SizeFilter::Tiny:
    return "Tiny (< 1 KB)";
  case SizeFilter::Small:
    return "Small (1-100 KB)";
  case SizeFilter::Medium:
    return "Medium (100 KB - 10 MB)";
  case SizeFilter::Large:
    return "Large (10-100 MB)";
  case SizeFilter::Huge:
    return "Huge (100 MB - 1 GB)";
  case SizeFilter::Massive:
    return "Massive (> 1 GB)";
  case SizeFilter::None:
  default:
    return "None";
  }
}

bool MatchesSizeFilter(uint64_t file_size, SizeFilter filter) {
  using size_filter_constants::kOneKB;
  using size_filter_constants::kOneHundredKB;
  using size_filter_constants::kTenMB;
  using size_filter_constants::kOneHundredMB;
  using size_filter_constants::kOneGB;

  // Handle special cases first
  if (filter == SizeFilter::None) {
    return true; // No filter - all sizes match
  }

  if (filter == SizeFilter::Empty) {
    return file_size == 0;
  }

  if (filter == SizeFilter::Tiny) {
    // 0 < size < 1 KB (note: Empty files handled separately)
    return file_size > 0 && file_size < kOneKB;
  }

  if (filter == SizeFilter::Massive) {
    // size >= 1 GB
    return file_size >= kOneGB;
  }

  // Handle range-based filters (Small, Medium, Large, Huge) using lookup
  if (filter == SizeFilter::Small) {
    // 1 KB <= size < 100 KB
    return file_size >= kOneKB && file_size < kOneHundredKB;
  }

  if (filter == SizeFilter::Medium) {
    // 100 KB <= size < 10 MB
    return file_size >= kOneHundredKB && file_size < kTenMB;
  }

  if (filter == SizeFilter::Large) {
    // 10 MB <= size < 100 MB
    return file_size >= kTenMB && file_size < kOneHundredMB;
  }

  if (filter == SizeFilter::Huge) {
    // 100 MB <= size < 1 GB
    return file_size >= kOneHundredMB && file_size < kOneGB;
  }

  // Unknown filter value - should never reach here
  // NOLINTNEXTLINE(readability-simplify-boolean-expr,readability-implicit-bool-conversion) - Common assert pattern with message
  assert(false && "Unknown SizeFilter value");
  return false;
}


