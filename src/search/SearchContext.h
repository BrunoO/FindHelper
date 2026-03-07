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

#include "utils/Logger.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

/** Extension set with transparent lookup: find(string_view) without allocating. C++17: std::set with std::less<void>. */
using ExtensionSet = std::set<std::string, std::less<void>>;

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
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    std::string_view filename_query{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    std::string filename_query_lower{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    std::string_view path_query{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    std::string path_query_lower{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    
    // ========== Query Type Flags ==========
    // Pattern matching is handled by SearchPatternUtils functions directly
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    bool use_glob = false;                   // Filename uses glob pattern (*, ?)
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    bool use_glob_path = false;              // Path uses glob pattern (*, ?)
    
    // ========== Search Options ==========
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    bool case_sensitive = false;             // Case-sensitive search
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    bool folders_only = false;               // Only return directories
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    bool extension_only_mode = false;        // Only match extensions (no filename/path)
    
    // ========== Extension Filter ==========
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    std::vector<std::string> extensions{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    ExtensionSet extension_set{};  // NOLINT(readability-redundant-member-init) - Explicit initialization for member init check

    // ========== Pre-compiled Patterns ==========
    // Optional: Pre-compiled patterns for performance (avoids recompiling in each thread)
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    std::shared_ptr<path_pattern::CompiledPathPattern> filename_pattern{nullptr};
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    std::shared_ptr<path_pattern::CompiledPathPattern> path_pattern{nullptr};
    
    // ========== Execution Control ==========
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    const std::atomic<bool>* cancel_flag = nullptr;  // Cancellation flag (checked periodically)
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    size_t dynamic_chunk_size = 1000;  // NOLINT(readability-magic-numbers) - Default chunk size (1000 items), validated in ValidateAndClamp()
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    int hybrid_initial_percent = 75;  // NOLINT(readability-magic-numbers) - Default percentage (75%), validated in ValidateAndClamp()
    // Load balancing strategy name ("static", "hybrid", "dynamic", "interleaved"); set from settings or default
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
    std::string load_balancing_strategy = "hybrid";
    // Thread pool size for search (0 = auto from hardware_concurrency); set from settings or default
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes) - Struct with public members (POD type)
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
            if (!processed_ext.empty() && processed_ext[0] == '.') {
                processed_ext.erase(0, 1);
            }
            
            if (case_sensitive) {
                extension_set.insert(processed_ext);
            } else {
                // Insert lowercase for case-insensitive matching
                // ExtensionMatches expects lowercase extensions in the set when case_sensitive is false
            std::transform(processed_ext.begin(), processed_ext.end(),
                              processed_ext.begin(), ::tolower);
                extension_set.insert(processed_ext);
            }
        }
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
    void ValidateAndClamp() {
        // Constants for validation bounds (local constants use snake_case per naming conventions)
        constexpr size_t min_dynamic_chunk_size = 100;  // NOLINT(readability-magic-numbers) - Minimum chunk size
        constexpr size_t max_dynamic_chunk_size = 100000;  // NOLINT(readability-magic-numbers) - Maximum chunk size (100K for testing)
        constexpr int min_hybrid_initial_percent = 50;  // NOLINT(readability-magic-numbers) - Minimum percentage
        constexpr int max_hybrid_initial_percent = 95;  // NOLINT(readability-magic-numbers) - Maximum percentage
        
        // Helper: clamp value to [min_val, max_val] and log if clamped (avoids branch-clone)
        auto clamp_and_log = [](auto& value, auto min_val, auto max_val, const char* name) {
            (void)name;  // NOLINT(clang-diagnostic-unused-parameter) - Name is only used when logging is enabled
            if (value < min_val) {
                LOG_WARNING_BUILD(name << " too small (" << value << "), clamped to " << min_val);
                value = min_val;
            } else if (value > max_val) {
                LOG_WARNING_BUILD(name << " too large (" << value << "), clamped to " << max_val);
                value = max_val;
            }
        };
        clamp_and_log(dynamic_chunk_size, min_dynamic_chunk_size, max_dynamic_chunk_size, "dynamic_chunk_size");
        clamp_and_log(hybrid_initial_percent, min_hybrid_initial_percent, max_hybrid_initial_percent, "hybrid_initial_percent");
    }
};

