#pragma once

/**
 * @file SearchContext.h
 * @brief Encapsulates all parameters needed for a search operation
 *
 * This struct replaces the 22+ parameter list in LaunchSearchTasks, making
 * the API more maintainable and self-documenting.
 *
 * DESIGN:
 * - Groups related parameters together logically
 * - Pre-computes values (e.g., lowercase queries, extension sets) for efficiency
 * - Enables parameter validation in one place
 * - Makes it easier to add new search parameters without breaking existing code
 */

#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/** Sorted extension list for O(log N) binary search with string_view keys (no heap allocation on lookup). */
using ExtensionSet = std::vector<std::string>;

namespace path_pattern {
    struct CompiledPathPattern;
}

/**
 * Encapsulates all parameters needed for a search operation.
 * Replaces the 22+ parameter list in LaunchSearchTasks.
 */
struct SearchContext {
    // ========== Query Parameters ==========
    // OPTIMIZATION: Use string_view for queries to avoid unnecessary allocations
    // Lifetime is guaranteed: queries are parameters to SearchAsync() which outlive SearchContext usage

    std::string_view filename_query{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check

    std::string filename_query_lower{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check

    std::string_view path_query{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check

    std::string path_query_lower{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check

    // ========== Query Type Flags ==========
    // Pattern matching is handled by SearchPatternUtils functions directly

    bool use_glob = false;                   // Filename uses glob pattern (*, ?)

    bool use_glob_path = false;              // Path uses glob pattern (*, ?)

    // ========== Search Options ==========

    bool case_sensitive = false;             // Case-sensitive search

    bool folders_only = false;               // Only return directories

    bool extension_only_mode = false;        // Only match extensions (no filename/path)

    // ========== Extension Filter ==========

    std::vector<std::string> extensions{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check

    ExtensionSet extension_set{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check

    // ========== Pre-compiled Patterns ==========
    // Optional: Pre-compiled patterns for performance (avoids recompiling in each thread)

    std::shared_ptr<path_pattern::CompiledPathPattern> filename_pattern{nullptr};

    std::shared_ptr<path_pattern::CompiledPathPattern> path_pattern{nullptr};

    // ========== Execution Control ==========

    const std::atomic<bool>* cancel_flag = nullptr;  // Cancellation flag (checked periodically)

    size_t dynamic_chunk_size = 1000;  // NOLINT(readability-magic-numbers) - Default chunk size (1000 items), validated in ValidateAndClamp()

    int hybrid_initial_percent = 80;  // NOLINT(readability-magic-numbers) - Default percentage (80%), validated in ValidateAndClamp()

    int guided_scheduling_divisor = 2;  // NOLINT(readability-magic-numbers) - Guided scheduling divisor multiplier (default 2 = standard OpenMP formula), validated in ValidateAndClamp()
    // Thread pool size for search (0 = auto from hardware_concurrency); set from settings or default

    int search_thread_pool_size = 0;

    // ========== Constructors ==========
    /**
     * Default constructor.
     * All members have default initializers, so this is safe.
     */
    SearchContext() = default;

    /**
     * Copy constructor.
     * All member types are copyable, so this is safe.
     */
    SearchContext(const SearchContext&) = default;

    /**
     * Move constructor (noexcept).
     * All member types have noexcept move constructors, making this safe.
     */
    SearchContext(SearchContext&&) noexcept = default;

    /**
     * Default destructor.
     * All member types have trivial destructors or are RAII-managed.
     */
    ~SearchContext() = default;

    /**
     * Copy assignment operator.
     * All member types are copyable, so this is safe.
     */
    SearchContext& operator=(const SearchContext&) = default;

    /**
     * Move assignment operator (noexcept).
     * All member types have noexcept move assignment, making this safe.
     */
    SearchContext& operator=(SearchContext&&) noexcept = default;

    // ========== Helper Methods ==========
    [[nodiscard]] bool HasFilenameQuery() const { return !filename_query.empty(); }
    [[nodiscard]] bool HasPathQuery() const { return !path_query.empty(); }
    [[nodiscard]] bool HasExtensionFilter() const { return !extensions.empty(); }

    /**
     * Prepare extension set for fast lookup.
     * Converts extensions to lowercase if case-insensitive search.
     * Should be called after setting extensions and case_sensitive.
     */
    void PrepareExtensionSet() {
        extension_set.clear();
        if (extensions.empty()) {
            return;
        }
        for (const auto& ext : extensions) {
            // Remove leading dot if present (matching ParseExtensions behavior)
            // File extensions are stored without the leading dot in the path arrays
            auto processed_ext = ext;
            if (!processed_ext.empty() && processed_ext[0] == '.') {  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - index 0 is safe: !processed_ext.empty() checked above
                processed_ext.erase(0, 1);
            }

            if (!case_sensitive) {
                // Lowercase for case-insensitive matching; ExtensionMatches expects lowercase entries
                std::transform(processed_ext.begin(), processed_ext.end(),  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
                               processed_ext.begin(), ::tolower);
            }
            extension_set.push_back(processed_ext);
        }
        std::sort(extension_set.begin(), extension_set.end());  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
        extension_set.erase(std::unique(extension_set.begin(), extension_set.end()), extension_set.end());  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
    }

    /**
     * Validate and clamp settings to reasonable ranges.
     *
     * Ensures dynamic_chunk_size and hybrid_initial_percent are within
     * acceptable bounds. Logs warnings if values are clamped.
     *
     * Should be called after setting dynamic_chunk_size and hybrid_initial_percent
     * from settings or other sources.
     *
     * Validation rules:
     * - dynamic_chunk_size: clamped to [100, 100000]
     * - hybrid_initial_percent: clamped to [50, 95]
     */
    void ValidateAndClamp();
};

