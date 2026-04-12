#pragma once

/**
 * @file SearchHistory.h
 * @brief In-memory mutation API for the unified search history.
 *
 * This module owns the search history data model operations:
 * - ID generation and normalization (dedup key)
 * - Recording a search execution (upsert + cap trim)
 * - Pin / unpin transitions
 * - Rename and delete
 * - Sorted read view for UI rendering
 *
 * Persistence (load/save to search_history.json) is handled by Settings.cpp,
 * following the same pattern as saved_searches.json and recent_searches.json.
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/Settings.h"
#include "search/SearchTypes.h"

// Forward declaration — full definition in SearchHistory.cpp via GuiState.h.
// GuiState.h transitively pulls in imgui.h which is not available in all test targets.
class GuiState;

/**
 * @brief Produce a canonical form of SearchHistoryParams for dedup/equality.
 *
 * Trimming and stable ordering ensure that semantically identical searches
 * always produce the same normalized form:
 * - Leading/trailing whitespace stripped from all string fields.
 * - Extensions split by comma, each element trimmed and lowercased, sorted,
 *   empty elements removed, rejoined with commas.
 * - Boolean and numeric fields are already canonical.
 *
 * @param params Input params (taken by value so the caller can move in).
 * @return Normalized copy.
 */
[[nodiscard]] SearchHistoryParams NormalizeHistoryParams(SearchHistoryParams params);

/**
 * @brief Generate a stable, deterministic id for a set of search params.
 *
 * Normalizes params and hashes the canonical representation with FNV-1a 64-bit.
 * The returned id is a lowercase 16-character hex string.
 *
 * Collision policy: this function returns the base id (no suffix). The suffix
 * ("-1", "-2", …) is appended by RecordHistoryEntry when two distinct canonical
 * payloads hash to the same value.
 *
 * @param params Search parameters (normalized internally).
 * @return 16-character lowercase hex id.
 */
[[nodiscard]] std::string GenerateHistoryId(const SearchHistoryParams& params);

/**
 * @brief Record a search execution in the history (upsert + cap trim).
 *
 * - If an entry with equivalent normalized params already exists:
 *     updates last_used_at_unix_ms; preserves is_pinned, custom_name,
 *     created_at_unix_ms.
 * - Otherwise: inserts a new non-pinned entry with all three timestamps = now_ms.
 * - After insert/update, trims the oldest non-pinned entries so the non-pinned
 *   count stays within settings_defaults::kMaxRecentSearches.
 *   Pinned entries are never trimmed.
 *
 * @param params   Search parameters describing the executed search.
 * @param now_ms   Current time as milliseconds since Unix epoch.
 * @param settings AppSettings to modify in-place.
 * @return The id of the upserted entry.
 */
[[nodiscard]] std::string RecordHistoryEntry(SearchHistoryParams params,
                                             std::int64_t now_ms,
                                             AppSettings& settings);

/**
 * @brief Pin an existing history entry.
 *
 * Sets is_pinned = true and pinned_at_unix_ms = now_ms.
 * No-op if the id is not found.
 *
 * @param id     Id of the entry to pin.
 * @param now_ms Current time as milliseconds since Unix epoch.
 * @param settings AppSettings to modify in-place.
 */
void PinHistoryEntry(std::string_view id, std::int64_t now_ms, AppSettings& settings);

/**
 * @brief Unpin an existing history entry.
 *
 * Sets is_pinned = false, pinned_at_unix_ms = 0,
 * and last_used_at_unix_ms = now_ms (so the entry sorts as most-recently-used).
 * Applies cap trim to non-pinned entries after unpinning.
 * No-op if the id is not found.
 *
 * @param id     Id of the entry to unpin.
 * @param now_ms Current time as milliseconds since Unix epoch.
 * @param settings AppSettings to modify in-place.
 */
void UnpinHistoryEntry(std::string_view id, std::int64_t now_ms, AppSettings& settings);

/**
 * @brief Rename a pinned history entry.
 *
 * Sets custom_name. An empty name is allowed (reverts to generated label).
 * No-op if the id is not found.
 *
 * @param id   Id of the entry to rename.
 * @param name New custom name (may be empty).
 * @param settings AppSettings to modify in-place.
 */
void RenameHistoryEntry(std::string_view id, std::string_view name, AppSettings& settings);

/**
 * @brief Delete a history entry by id.
 *
 * Removes exactly the entry with the given id. No-op if not found.
 *
 * @param id   Id of the entry to delete.
 * @param settings AppSettings to modify in-place.
 */
void DeleteHistoryEntry(std::string_view id, AppSettings& settings);

/**
 * @brief Return pointers to history entries in display order.
 *
 * Ordering:
 * - Pinned entries first, sorted by pinned_at_unix_ms DESC,
 *   then last_used_at_unix_ms DESC, then id ASC (deterministic tie-break).
 * - Non-pinned (recent) entries follow, sorted by last_used_at_unix_ms DESC,
 *   then id ASC.
 *
 * The returned pointers are into settings.searchHistory; they are invalidated
 * if the vector is modified. Callers must not hold them across mutations.
 *
 * @param settings Source settings (read-only).
 * @return Sorted vector of non-owning pointers.
 */
[[nodiscard]] std::vector<const SearchHistoryEntry*> GetSortedHistoryView(
    const AppSettings& settings);

/**
 * @brief Build a SearchHistoryParams from the runtime search params and current GUI state.
 *
 * Maps the search-engine operational SearchParams (filename, path, extensions, flags)
 * plus the additional GUI-state fields (timeFilter, sizeFilter, ai description) into
 * the persisted SearchHistoryParams shape.
 *
 * @param params  Runtime search params (from last_search_params_).
 * @param state   Current GuiState (for timeFilter, sizeFilter, gemini_description_input_).
 * @return Populated SearchHistoryParams ready for RecordHistoryEntry.
 */
[[nodiscard]] SearchHistoryParams BuildHistoryParams(const SearchParams& params,
                                                     const GuiState& state);

/**
 * @brief Apply a history entry's params to the GUI state (for re-running a search).
 *
 * Applies numeric time/size filter values via static_cast<TimeFilter> / static_cast<SizeFilter>.
 * Calls state.MarkInputChanged() to trigger search.
 *
 * @param entry History entry whose params should be applied.
 * @param state GuiState to modify.
 */
void ApplyHistoryEntryToGuiState(const SearchHistoryEntry& entry, GuiState& state);
