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

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/Settings.h"
#include "search/SearchTypes.h"

// Forward declaration — full definition in SearchHistory.cpp via GuiState.h.
// GuiState.h transitively pulls in imgui.h which is not available in all test targets.
class GuiState;

// Forward declaration — plain substate (no imgui) defined in gui/WorkflowStates.h.
struct ExportWorkflowState;
struct SearchNavigationState;

// Forward declaration — MUST match the definition site's kind (MSVC C4099):
// gui/UIActions.h declares `class UIActions` inside namespace ui.
namespace ui { class UIActions; }

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
 *     created_at_unix_ms. Also updates last_run_result_count when
 *     result_count >= 0; an unknown count (-1) preserves the stored value
 *     (a run without a snapshot must never downgrade a known count).
 * - Otherwise: inserts a new non-pinned entry with all three timestamps = now_ms
 *     and last_run_result_count = result_count.
 * - After insert/update, trims the oldest non-pinned entries so the non-pinned
 *   count stays within settings_defaults::kMaxRecentSearches.
 *   Pinned entries are never trimmed.
 *
 * @param params   Search parameters describing the executed search.
 * @param now_ms   Current time as milliseconds since Unix epoch.
 * @param settings AppSettings to modify in-place.
 * @param result_count Engine match count at completion, or -1 when the run did not
 *                     snapshot one (defaults to unknown to keep call sites explicit
 *                     only where a count exists).
 * @return The id of the upserted entry.
 */
[[nodiscard]] std::string RecordHistoryEntry(SearchHistoryParams params,
                                             std::int64_t now_ms,
                                             AppSettings& settings,
                                             std::int64_t result_count = -1);

/**
 * @brief Whether a completed manual search should be written to history this frame.
 *
 * Sample searchActive at the start of Application::UpdateSearchState (before
 * application_logic::Update). RenderMainWindow (Search button / Enter) runs after
 * that function, so a UI-triggered search is visible at the start of the next
 * frame — not via an end-of-frame sentinel from the previous update.
 *
 * Same-frame start+complete (F5 / crawl-complete) leaves searchWasManual set even
 * when search was inactive at update start; those completions are also recorded.
 * Auto-refresh and debounced instant search set searchWasManual = false.
 * If they replace a just-completed manual search in the same update, pass
 * pending_manual_history_record so the completed query is still recorded.
 *
 * @param search_was_manual True if the in-flight (or just-finished) search was user-initiated.
 * @param search_active_after_update searchActive after application_logic::Update() returns.
 * @param pending_manual_history_record True if PollResults completed a manual search this frame.
 */
[[nodiscard]] constexpr bool ShouldRecordManualSearchCompletion(
    bool search_was_manual, bool search_active_after_update,
    bool pending_manual_history_record = false) noexcept {
  return pending_manual_history_record ||
         (search_was_manual && !search_active_after_update);
}

/**
 * @brief Record the history entry for a completed manual search (gate + upsert + persist + consume).
 *
 * Extraction of the recording block that previously lived inline in
 * Application::UpdateSearchState (src/core/Application.cpp). Behavior-preserving
 * move so the gate+record interaction — the re-entrancy-critical path for the
 * Back/Forward navigation stack — is unit-testable without an Application
 * instance. The persist seam is a function pointer (no window/GLFW dependency).
 *
 * Gate: ShouldRecordManualSearchCompletion(state.search_pipeline.*). When the
 * gate does not fire, nothing is recorded, consumed, or persisted. When it
 * fires: BuildHistoryParams(last_search_params, state) → RecordHistoryEntry
 * (result count = completion-time snapshot
 * state.search_pipeline.completed_manual_result_count) → NavPush(state
 * .navigation, id) (P2-T1: single push point for the back/forward stack, same
 * gate so auto-refresh completions can never advance it) → persist (LOG_ERROR
 * on failure, mirroring the app) → ConsumeManualHistoryRecord.
 *
 * @param state               GUI state; reads + consumes search_pipeline record fields.
 * @param last_search_params  Snapshot of the completed search's params (caller-owned).
 * @param settings            AppSettings whose searchHistory is upserted in place.
 * @param persist_settings    Seam to durably save settings (Application::PersistSettings at the
 *                            app call site; a counting stub in tests). Called exactly once when
 *                            the gate fires. May be null to skip persisting.
 * @param persist_context     Opaque context handed to persist_settings (the Application* at the
 *                            app call site). Ignored when persist_settings is null.
 * @return Recorded/upserted history entry id; empty string when the gate did not fire.
 */
[[nodiscard]] std::string RecordCompletedManualSearch(
    GuiState& state, const SearchParams& last_search_params, AppSettings& settings,
    bool (*persist_settings)(void* context), void* persist_context);

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
 * @brief Status-bar toast for history pin/unpin across every entry point
 * (Ctrl/Cmd+P, welcome row button/menu, Quick Access card button/menu).
 *
 * Success names the search by its display headline; a persist failure is
 * reported via the error variant (the in-memory toggle still applied).
 *
 * @param banner        Target status banner (GuiState::export_workflow). UI thread only.
 * @param headline      Entry headline, captured BEFORE the toggle — pin/unpin may
 *                      erase entries (trim), invalidating entry references.
 * @param is_pinned_now Pin state after the action (true = just pinned).
 * @param persist_ok    False when PersistSettings failed.
 */
void NotifyPinAction(ExportWorkflowState& banner, std::string_view headline,
                     bool is_pinned_now, bool persist_ok);

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
 * @brief Look up a stored history entry by its id (single source of truth for
 * the plain find-by-id scan; the id-collision suffix probe inside
 * RecordHistoryEntry is NOT this lookup and intentionally stays separate).
 *
 * Linear scan over the history vector; no ordering assumption. Returns a
 * non-owning pointer into the vector — invalidated by any mutation of it
 * (same rule as GetSortedHistoryView).
 *
 * @param history History vector to search (typically settings.searchHistory).
 * @param id      Entry id to find.
 * @return Pointer to the matching entry, or nullptr when absent.
 */
[[nodiscard]] SearchHistoryEntry* FindHistoryEntryById(std::vector<SearchHistoryEntry>& history,
                                                       std::string_view id);

[[nodiscard]] const SearchHistoryEntry* FindHistoryEntryById(
    const std::vector<SearchHistoryEntry>& history, std::string_view id);

/**
 * @brief Seed default initial pinned searches into settings if search history is empty.
 *
 * Converts standard example search patterns (*.cpp, Makefile, ^main, *.h, *.py, *.json,
 * Folders only, Modified today, Empty files, Large files) into pinned history entries.
 *
 * @param settings AppSettings to populate if settings.searchHistory is empty.
 */
void SeedDefaultPinnedSearchesIfNeeded(AppSettings& settings);

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
 * @param state   Current GuiState (for timeFilter, sizeFilter, gemini.description_input).
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

/**
 * @brief Apply a history entry's params to GuiState and re-run the search.
 *
 * Extraction of the apply-then-trigger pair previously inlined in the history
 * row click handlers (WelcomePanel rows, Quick Access cards). Every recall
 * path — row/card click and the Back/Forward navigation steps alike — shares
 * this one apply+run implementation (spec §3.1.2).
 *
 * @param entry   History entry to recall.
 * @param state   GuiState to modify (UI thread only).
 * @param actions Trigger seam; null-safe so unit tests can verify param
 *                application without an Application/window instance.
 */
void ApplyAndRunHistoryEntry(const SearchHistoryEntry& entry, GuiState& state,
                             ui::UIActions* actions);

// ---------------------------------------------------------------------------
// Browser-style back/forward navigation over search history (spec §3/§4.1)
// ---------------------------------------------------------------------------

// Upper bound for the navigation stack (DRY: defined once here, never
// re-declared in GUI code). On overflow NavPush drops the oldest entry that is
// NOT the cursor entry (§4.1), so the currently displayed search survives.
inline constexpr size_t kMaxNavigationStackDepth = 100;

/**
 * @brief Push a recorded history id onto the navigation stack (browser push-state).
 *
 * The ONLY tail-truncating operation (spec §6 invariant 4). Dedup rule:
 * id == stack[cursor] → complete no-op (no resize, no truncate) — this is the
 * re-entrancy guard: navigating to an entry re-runs its search, whose
 * completion lands back here with the same id and must leave cursor AND
 * forward tail untouched (§3.1.2). Otherwise truncate the forward tail
 * (resize to cursor + 1), append an OWNING copy of id, and move the cursor to
 * the new tip. Overflow: while size > kMaxNavigationStackDepth, drop the
 * oldest entry that is not the cursor entry (cursor shifts down when an entry
 * before it is erased; the cursor's entry always stays).
 *
 * @param nav Navigation stack (UI thread only).
 * @param id  Recorded history entry id (non-empty; copied, never referenced).
 */
void NavPush(SearchNavigationState& nav, std::string_view id);

// Direction availability (spec §3.1.2/§3.1.3): Back iff cursor > 0,
// Forward iff cursor + 1 < stack.size(). False for an empty stack.
[[nodiscard]] bool NavCanGoBack(const SearchNavigationState& nav);
[[nodiscard]] bool NavCanGoForward(const SearchNavigationState& nav);

// Move the cursor one step; return false (cursor unchanged) when the direction
// is disabled. Callers navigate to stack[cursor] only after a true return.
bool NavBack(SearchNavigationState& nav);
bool NavForward(SearchNavigationState& nav);

/**
 * @brief Remove stack[index], fixing up the cursor (spec §3.1.4 building block).
 *
 * Bounds-checked: out-of-range index is a no-op (asserted — programmer error;
 * navigation paths only pass indices taken from cursor). If index < cursor,
 * cursor shifts down; if index == cursor, the cursor STAYS so it references
 * the entry that shifted into the slot (the next candidate in the direction of
 * travel — US-7 retry semantics). Always clamped back in range; an emptied
 * stack resets cursor to 0.
 */
void NavDrop(SearchNavigationState& nav, size_t index);

/**
 * @brief Resolve nav.stack[index] and re-run the entry (spec §4.1 navigate step).
 *
 * Hit: ApplyAndRunHistoryEntry(*entry, state, actions) — params applied to
 * GuiState and the search triggered through the shared recall path.
 * Miss (entry deleted from settings.searchHistory elsewhere): NavDrop(nav,
 * index) + false; the cursor fix-up inside NavDrop leaves it on the neighbor
 * that shifted into the slot, which is what NavigateHistory's single retry
 * consumes (§3.1.4).
 *
 * @param nav     Navigation stack (UI thread only).
 * @param index   Position of the id in nav.stack to resolve (cursor-range).
 * @param state   GuiState to apply the entry's params to.
 * @param settings Source of truth for id resolution (read-only).
 * @param actions Trigger seam (null-safe: unit tests verify application without
 *                a window). Passes the Application instance at real call sites.
 * @return True when the entry resolved and was applied + re-run.
 */
bool NavigateToHistoryId(SearchNavigationState& nav, size_t index, GuiState& state,
                         const AppSettings& settings, ui::UIActions* actions);

/**
 * @brief One Back/Forward step: move cursor, navigate, retry once on a dead id.
 *
 * The composition shared by every navigation entry point (mandatory
 * Cmd/Ctrl+J / Cmd/Ctrl+K shortcuts in DispatchGlobalShortcut, and the
 * optional §5 buttons if they ever ship). Boundary presses are silent no-ops
 * (§3.1.6): NavBack/NavForward returning false exits immediately without
 * touching state or triggering a search. After a move, the entry at the
 * cursor is resolved; on a miss NavDrop already removed the dead id and fixed
 * up the cursor, so exactly one retry targets the neighbor now under it
 * (§3.1.4 — at most one retry, no error UI).
 *
 * @param direction -1 = Back, +1 = Forward (any non-negative value steps forward).
 */
void NavigateHistory(int direction, SearchNavigationState& nav, GuiState& state,
                     const AppSettings& settings, ui::UIActions* actions);

/**
 * @brief Compact two-line labels for a history row (headline + subtitle).
 *
 * Headline prefers custom name or AI prompt; otherwise a short sentence
 * ("*.cpp in …/src", "Folders only in …/docs"). Filter-only searches use
 * filter-bar wording (never the word "search"). Subtitle uses labeled crumbs
 * (Folder:, Types:, Modified:, Size:) for anything not already in the headline,
 * led by a "N items last run" crumb when the entry has a known last-run
 * result count (>= 0) — leading so it survives one-line card truncation on
 * narrow Quick Access cards; omitted for unknown (-1), "No items last run" for 0.
 */
struct HistoryEntryDisplayInfo {
  std::string headline;  // NOLINT(readability-identifier-naming) - POD display DTO; snake_case without trailing _
  std::string subtitle;  // NOLINT(readability-identifier-naming) - POD display DTO; snake_case without trailing _
};

[[nodiscard]] HistoryEntryDisplayInfo BuildHistoryEntryDisplayInfo(const SearchHistoryEntry& entry);

/**
 * @brief Full labeled query recipe for a history-row hover tooltip, plus a
 * "Last run: N items" line when the last-run result count is known (>= 0).
 *
 * Paths are not truncated. Empty fields are omitted.
 */
[[nodiscard]] std::string BuildHistoryEntryTooltip(const SearchHistoryEntry& entry);
