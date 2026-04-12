#pragma once

// Test-only helpers for setting up AsyncSortState staging buffers.
// Use these instead of writing to state.async_sort_ fields directly so that
// field renames or layout changes require updating only this header.

#include <cstddef>
#include <mutex>

#include "gui/GuiState.h"
#include "utils/FileTimeTypes.h"

// Fill staging with `slots` entries: every size set to `size_value`, every
// time set to `time_value`, every flag byte set to `flags`.
// Sets staging.staging_generation_ = gen (under the mutex).
// Mirrors what StartAttributeLoadingAsync writes into the staging buffers.
inline void PrepareStaging(AsyncSortState& staging,
                            size_t slots,
                            SortGeneration gen,
                            uint64_t size_value,
                            FILETIME time_value,
                            std::byte flags) {
    const std::scoped_lock lock(staging.mutex_);
    staging.sizes_.assign(slots, size_value);
    staging.times_.assign(slots, time_value);
    staging.flags_.assign(slots, flags);
    staging.staging_generation_ = gen;
}
