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

#endif  // ENABLE_IMGUI_TEST_ENGINE
