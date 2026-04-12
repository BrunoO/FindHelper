/**
 * @file SearchHistory.cpp
 * @brief Implementation of in-memory search history mutations.
 *
 * Persistence (JSON load/save) lives in Settings.cpp; this module is
 * purely in-memory: normalize, hash, upsert, pin/unpin, rename, delete,
 * sort.
 */

#include "search/SearchHistory.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "core/Settings.h"
#include "filters/SizeFilter.h"
#include "filters/TimeFilter.h"
#include "gui/GuiState.h"
#include "utils/StringUtils.h"

namespace {

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

[[nodiscard]] std::string TrimWhitespace(std::string_view sv) {
  const auto start = sv.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) {
    return {};
  }
  const auto end = sv.find_last_not_of(" \t\r\n");
  return std::string(sv.substr(start, end - start + 1));
}

// Split a comma-separated string, trim and lowercase each token, remove empties.
[[nodiscard]] std::vector<std::string> SplitAndNormalizeExtensions(std::string_view ext) {
  std::vector<std::string> parts;
  std::string_view remaining = ext;
  while (!remaining.empty()) {
    const auto comma = remaining.find(',');
    const std::string_view token = (comma == std::string_view::npos)
        ? remaining
        : remaining.substr(0, comma);
    std::string trimmed = TrimWhitespace(token);
    // Lowercase
    for (char& c : trimmed) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (!trimmed.empty()) {
      parts.push_back(std::move(trimmed));
    }
    if (comma == std::string_view::npos) {
      break;
    }
    remaining = remaining.substr(comma + 1);
  }
  std::sort(parts.begin(), parts.end());  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
  return parts;
}

// ---------------------------------------------------------------------------
// FNV-1a 64-bit hash (stable across runs — pure function of input bytes)
// ---------------------------------------------------------------------------

constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

[[nodiscard]] uint64_t Fnv1a64(std::string_view data) {
  uint64_t hash = kFnvOffsetBasis;
  for (const char c : data) {
    hash ^= static_cast<uint64_t>(static_cast<uint8_t>(c));
    hash *= kFnvPrime;
  }
  return hash;
}

[[nodiscard]] std::string ToHex16(uint64_t value) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0') << std::setw(16) << value;
  return oss.str();
}

// ---------------------------------------------------------------------------
// Canonical string for hashing
// ---------------------------------------------------------------------------

[[nodiscard]] std::string BuildCanonicalString(const SearchHistoryParams& p) {
  const std::vector<std::string> ext_parts = SplitAndNormalizeExtensions(p.extensions);
  std::string ext_joined;
  for (const auto& part : ext_parts) {
    if (!ext_joined.empty()) {
      ext_joined += ',';
    }
    ext_joined += part;
  }

  std::ostringstream oss;
  oss << "filename=" << p.filename
      << "|path=" << p.path
      << "|extensions=" << ext_joined
      << "|folders_only=" << (p.folders_only ? '1' : '0')
      << "|case_sensitive=" << (p.case_sensitive ? '1' : '0')
      << "|time_filter=" << p.time_filter
      << "|size_filter=" << p.size_filter
      << "|ai=" << p.ai_search_description;
  return oss.str();
}

// ---------------------------------------------------------------------------
// Cap trim: remove oldest non-pinned entries over kMaxRecentSearches
// ---------------------------------------------------------------------------

void TrimNonPinnedEntries(AppSettings& settings) {
  // Count non-pinned entries
  size_t non_pinned_count = 0;
  for (const auto& e : settings.searchHistory) {
    if (!e.is_pinned) {
      ++non_pinned_count;
    }
  }
  while (non_pinned_count > settings_defaults::kMaxRecentSearches) {
    // Find the oldest non-pinned entry (last_used_at ASC, then id ASC as tie-break)
    auto oldest = settings.searchHistory.end();
    for (auto it = settings.searchHistory.begin(); it != settings.searchHistory.end(); ++it) {
      if (it->is_pinned) {
        continue;
      }
      if (oldest == settings.searchHistory.end() ||
          it->last_used_at_unix_ms < oldest->last_used_at_unix_ms ||
          (it->last_used_at_unix_ms == oldest->last_used_at_unix_ms &&
           it->id < oldest->id)) {
        oldest = it;
      }
    }
    if (oldest == settings.searchHistory.end()) {
      break;
    }
    settings.searchHistory.erase(oldest);
    --non_pinned_count;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SearchHistoryParams NormalizeHistoryParams(SearchHistoryParams params) {
  params.filename = TrimWhitespace(params.filename);
  params.path = TrimWhitespace(params.path);
  params.ai_search_description = TrimWhitespace(params.ai_search_description);

  // Normalize extensions: split, trim, lowercase, sort, rejoin
  const std::vector<std::string> ext_parts = SplitAndNormalizeExtensions(params.extensions);
  std::string ext_joined;
  for (const auto& part : ext_parts) {
    if (!ext_joined.empty()) {
      ext_joined += ',';
    }
    ext_joined += part;
  }
  params.extensions = std::move(ext_joined);

  return params;
}

std::string GenerateHistoryId(const SearchHistoryParams& params) {
  const SearchHistoryParams normalized = NormalizeHistoryParams(params);
  const std::string canonical = BuildCanonicalString(normalized);
  return ToHex16(Fnv1a64(canonical));
}

std::string RecordHistoryEntry(SearchHistoryParams params,
                               std::int64_t now_ms,
                               AppSettings& settings) {
  const SearchHistoryParams normalized = NormalizeHistoryParams(std::move(params));
  const std::string canonical = BuildCanonicalString(normalized);
  const std::string base_id = ToHex16(Fnv1a64(canonical));

  // Find an existing entry that matches by id, handling hash collisions via suffix scan.
  std::string effective_id = base_id;
  int suffix = 0;
  while (true) {
    const auto it = std::find_if(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
        settings.searchHistory.begin(), settings.searchHistory.end(),
        [&effective_id](const SearchHistoryEntry& e) { return e.id == effective_id; });

    if (it == settings.searchHistory.end()) {
      break;  // ID not yet taken — this is our slot
    }

    // Check if the stored entry has the same canonical params (true dedup)
    if (BuildCanonicalString(NormalizeHistoryParams(it->params)) == canonical) {
      // Match: upsert — update timestamp, preserve pin/name/created
      it->last_used_at_unix_ms = now_ms;
      TrimNonPinnedEntries(settings);
      return effective_id;
    }

    // Hash collision: different canonical payload shares the same base id.
    // Advance to the next suffix slot.
    ++suffix;
    effective_id = base_id + "-" + std::to_string(suffix);
  }

  // Insert new non-pinned entry
  SearchHistoryEntry entry;
  entry.id = effective_id;
  entry.params = normalized;
  entry.is_pinned = false;
  entry.created_at_unix_ms = now_ms;
  entry.last_used_at_unix_ms = now_ms;
  entry.pinned_at_unix_ms = 0;
  settings.searchHistory.push_back(std::move(entry));

  TrimNonPinnedEntries(settings);
  return effective_id;
}

void PinHistoryEntry(std::string_view id, std::int64_t now_ms, AppSettings& settings) {
  const auto it = std::find_if(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
      settings.searchHistory.begin(), settings.searchHistory.end(),
      [id](const SearchHistoryEntry& e) { return e.id == id; });
  if (it == settings.searchHistory.end()) {
    return;
  }
  it->is_pinned = true;
  it->pinned_at_unix_ms = now_ms;
}

void UnpinHistoryEntry(std::string_view id, std::int64_t now_ms, AppSettings& settings) {
  const auto it = std::find_if(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
      settings.searchHistory.begin(), settings.searchHistory.end(),
      [id](const SearchHistoryEntry& e) { return e.id == id; });
  if (it == settings.searchHistory.end()) {
    return;
  }
  it->is_pinned = false;
  it->pinned_at_unix_ms = 0;
  it->last_used_at_unix_ms = now_ms;  // treat as most-recently-used after unpin
  TrimNonPinnedEntries(settings);
}

void RenameHistoryEntry(std::string_view id, std::string_view name, AppSettings& settings) {
  const auto it = std::find_if(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
      settings.searchHistory.begin(), settings.searchHistory.end(),
      [id](const SearchHistoryEntry& e) { return e.id == id; });
  if (it == settings.searchHistory.end()) {
    return;
  }
  it->custom_name.assign(name);
}

void DeleteHistoryEntry(std::string_view id, AppSettings& settings) {
  const auto it = std::find_if(  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
      settings.searchHistory.begin(), settings.searchHistory.end(),
      [id](const SearchHistoryEntry& e) { return e.id == id; });
  if (it != settings.searchHistory.end()) {
    settings.searchHistory.erase(it);
  }
}

std::vector<const SearchHistoryEntry*> GetSortedHistoryView(const AppSettings& settings) {
  std::vector<const SearchHistoryEntry*> result;
  result.reserve(settings.searchHistory.size());
  for (const auto& e : settings.searchHistory) {
    result.push_back(&e);
  }
  std::sort(  // NOLINT(llvm-use-ranges,bugprone-nondeterministic-pointer-iteration-order) - C++17; comparator is fully deterministic (id tie-break) — pointer value never used
      result.begin(), result.end(),
      [](const SearchHistoryEntry* a, const SearchHistoryEntry* b) {
        // Pinned entries before non-pinned
        if (a->is_pinned != b->is_pinned) {
          return a->is_pinned;
        }
        if (a->is_pinned) {
          // Both pinned: pinned_at DESC, last_used DESC, id ASC
          if (a->pinned_at_unix_ms != b->pinned_at_unix_ms) {
            return a->pinned_at_unix_ms > b->pinned_at_unix_ms;
          }
          if (a->last_used_at_unix_ms != b->last_used_at_unix_ms) {
            return a->last_used_at_unix_ms > b->last_used_at_unix_ms;
          }
          return a->id < b->id;
        }
        // Both non-pinned (recent): last_used DESC, id ASC
        if (a->last_used_at_unix_ms != b->last_used_at_unix_ms) {
          return a->last_used_at_unix_ms > b->last_used_at_unix_ms;
        }
        return a->id < b->id;
      });
  return result;
}

SearchHistoryParams BuildHistoryParams(const SearchParams& params, const GuiState& state) {
  SearchHistoryParams p;
  p.filename = params.filenameInput;
  p.path = params.pathInput;
  p.extensions = params.extensionInput;
  p.folders_only = params.foldersOnly;
  p.case_sensitive = params.caseSensitive;
  p.time_filter = static_cast<int>(state.timeFilter);
  p.size_filter = static_cast<std::uint64_t>(state.sizeFilter);
  p.ai_search_description = std::string(state.gemini_description_input_.data());
  return p;
}

void ApplyHistoryEntryToGuiState(const SearchHistoryEntry& entry, GuiState& state) {
  state.pathInput.SetValue(entry.params.path);
  state.extensionInput.SetValue(entry.params.extensions);
  state.filenameInput.SetValue(entry.params.filename);
  state.foldersOnly = entry.params.folders_only;
  state.caseSensitive = entry.params.case_sensitive;
  state.timeFilter = static_cast<TimeFilter>(entry.params.time_filter);
  state.sizeFilter = static_cast<SizeFilter>(entry.params.size_filter);
  strcpy_safe(state.gemini_description_input_.data(),
              state.gemini_description_input_.size(),
              entry.params.ai_search_description.c_str());
  state.MarkInputChanged();
}
