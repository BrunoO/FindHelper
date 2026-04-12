#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <shared_mutex>
#include <string>
#include <vector>

#include "index/FileIndexStorage.h"
#include "path/PathBuilder.h"
#include "path/PathUtils.h"
#include "utils/FileTimeTypes.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Insert an entry and record its name in the storage's name cache.
void Insert(FileIndexStorage& storage, uint64_t id, uint64_t parent_id,
            std::string_view name, bool is_dir = false) {
    storage.InsertLocked(id, parent_id, name, is_dir, kFileTimeNotLoaded);
}

[[nodiscard]] std::string BuildExpectedPath(const std::vector<std::string_view>& components) {
    std::string expected(path_utils::GetDefaultVolumeRootPathView());
    bool is_first = true;
    for (const std::string_view component : components) {
        if (!is_first) {
            expected.push_back(path_utils::kPathSeparator);
        }
        expected.append(component);
        is_first = false;
    }
    return expected;
}

}  // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_SUITE("PathBuilder::BuildFullPathWithLogging") {

  TEST_CASE("single-level: child of self-parented root covers root-detection branch") {
    // Root entry: parentID == its own ID — this is the stop condition in
    // CollectPathComponents (lines 41-47 in PathBuilder.cpp).
    std::shared_mutex mutex;
    FileIndexStorage storage(mutex);
    Insert(storage, 1, 1, "home", true);   // root: parentID = self
    Insert(storage, 2, 1, "file.txt");     // direct child of root

    const std::string path = PathBuilder::BuildFullPathWithLogging(
        2, 1, "file.txt", storage, storage.GetNameCache());

    CHECK(path == BuildExpectedPath({"home", "file.txt"}));
  }

  TEST_CASE("multi-level path covers separator injection between components") {
    // BuildPathFromComponents inserts a separator before each component after
    // the first (lines 102-103). This requires effective_component_count >= 2.
    std::shared_mutex mutex;
    FileIndexStorage storage(mutex);
    Insert(storage, 1, 1, "usr", true);
    Insert(storage, 2, 1, "local", true);
    Insert(storage, 3, 2, "bin", true);
    Insert(storage, 4, 3, "tool");

    const std::string path = PathBuilder::BuildFullPathWithLogging(
        4, 3, "tool", storage, storage.GetNameCache());

    CHECK(path == BuildExpectedPath({"usr", "local", "bin", "tool"}));
  }

  TEST_CASE("depth limit warning path fires at kMaxPathDepth entries") {
    // kMaxPathDepth is 64. Build a chain of 66 entries so component_count
    // reaches kMaxPathDepth, triggering the LOG_WARNING in BuildFullPathWithLogging
    // (lines 123-128). The function must still return a non-empty path.
    std::shared_mutex mutex;
    FileIndexStorage storage(mutex);

    // Root
    Insert(storage, 1, 1, "root", true);

    // 65 intermediate directories (ids 2..66), each parenting the next
    for (uint64_t i = 2; i <= 66; ++i) {
        Insert(storage, i, i - 1, "d", true);
    }

    // Leaf at depth 66 — chain is deeper than kMaxPathDepth (64)
    Insert(storage, 67, 66, "leaf");

    const std::string path = PathBuilder::BuildFullPathWithLogging(
        67, 66, "leaf", storage, storage.GetNameCache());

    CHECK(!path.empty());
  }

}
