#pragma once

/**
 * @file ImGuiTestEngineRegressionExpected.h
 * @brief Golden expected counts and index size for ImGui Test Engine regression tests.
 *
 * Single source of truth for the std-linux-filesystem dataset. When the dataset or
 * test cases change, update these constants to match tests/data/std-linux-filesystem-expected.json
 * (regenerate that file via search_benchmark; see specs/2026-02-19_IMGUI_REGRESSION_TESTS_STD_LINUX_FILESYSTEM_SPEC.md).
 *
 * Only used when ENABLE_IMGUI_TEST_ENGINE is defined.
 */

#ifdef ENABLE_IMGUI_TEST_ENGINE

#include <cstddef>

namespace regression_expected {

/** Index size for dataset tests/data/std-linux-filesystem.txt. Must match golden JSON. */
constexpr size_t kIndexSize = 706120U;

/** Expected result count per test case (must match std-linux-filesystem-expected.json). */
constexpr size_t kShowAll = 706120U;
constexpr size_t kFilenameTty = 651U;
constexpr size_t kPathDev = 19796U;
constexpr size_t kExtConf = 683U;
constexpr size_t kPathEtcExtConf = 229U;
constexpr size_t kFoldersOnly = 86924U;

}  // namespace regression_expected

/**
 * Expected search result counts for the tests/data/fixture crawl
 * (index_configuration/select_folder_and_start_indexing test).
 *
 * Fixture layout (Fibonacci-byte files, 10 files + 6 dirs = 16 owned entries):
 *   alpha/a.txt(1B) b.txt(1B) c.cpp(2B)
 *   beta/x.h(3B)  y.json(5B)  z.md(8B)  sub/p.txt(13B) sub/q.cpp(21B)
 *   gamma/r.txt(55B)  deep/deeper/leaf.h(34B)
 *
 * Extension counts are EXACT: ancestor dirs created by DirectoryResolver have
 * no extension so they never appear in extension-filtered results.
 * Total-entry counts use >= because the runtime path depth adds ancestor dirs.
 *
 * Regenerate by running: scripts/create_test_fixture.sh
 */
namespace fixture_expected {

/** Lower bound for "show all" (10 files + 6 dirs; ancestor dirs add more). */
constexpr size_t kMinTotal = 16U;

/** Lower bound for "folders only" (6 fixture dirs; ancestor dirs add more). */
constexpr size_t kMinDirs = 6U;

/** Exact file counts by extension (ancestor dirs carry no extension). */
constexpr size_t kExtTxt  = 4U;  // a.txt, b.txt, p.txt, r.txt
constexpr size_t kExtCpp  = 2U;  // c.cpp, q.cpp
constexpr size_t kExtH    = 2U;  // x.h, leaf.h
constexpr size_t kExtJson = 1U;  // y.json

/** ext=cpp + folders_only=true: no directories have a .cpp extension. */
constexpr size_t kExtCppFoldersOnly = 0U;

}  // namespace fixture_expected

#endif  // ENABLE_IMGUI_TEST_ENGINE
