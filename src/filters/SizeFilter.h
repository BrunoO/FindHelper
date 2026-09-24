#pragma once

/**
 * @file SizeFilter.h
 * @brief SizeFilter enum for file size quick filters
 *
 * This enum is separated from GuiState.h to allow size filter utilities
 * to be used and tested independently of the GUI layer.
 */

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


