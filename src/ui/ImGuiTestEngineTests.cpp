/**
 * @file ImGuiTestEngineTests.cpp
 * @brief ImGui Test Engine registration and smoke/regression tests for FindHelper.
 *
 * Only compiled when ENABLE_IMGUI_TEST_ENGINE is ON (macOS in this integration).
 * All test logic lives here; Application only calls RegisterFindHelperTests().
 * Regression tests use IRegressionTestHook to drive search and assert against
 * golden expected counts (see tests/data/std-linux-filesystem-expected.json).
 *
 * Guard: Entire file body is under #ifdef ENABLE_IMGUI_TEST_ENGINE. When the flag is off,
 * this TU compiles to nothing. CMake adds this file only when the option is ON to avoid
 * accidental inclusion in production builds and ODR/linkage issues.
 */

#ifdef ENABLE_IMGUI_TEST_ENGINE

#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/Version.h"
#include "imgui.h"
#include "imgui_te_context.h"  // ImGuiTestContext (SetRef, Yield), IM_CHECK, IM_CHECK_EQ, IM_CHECK_RETV, IM_ERRORF
#include "imgui_te_engine.h"
#include "path/PathUtils.h"
#include "ui/IconsFontAwesome.h"  // ICON_FA_BOOK_OPEN - ref must match FilterPanel/SearchInputs button labels for ItemClick
#include "ui/ImGuiTestEngineRegressionExpected.h"
#include "ui/ImGuiTestEngineRegressionHook.h"
#include "utils/FileAttributeConstants.h"
#include "utils/Logger.h"
#include "utils/ThreadUtils.h"

// Max frames to wait for index ready or search complete (avoid infinite loop).
constexpr int kMaxWaitFrames = 600;  // ~10 s at 60 fps

// Max attempts to wait for a floating window to appear after clicking toolbar/search button (tune for CI/slow builds).
// Use enough attempts so the window can appear and receive focus; fallback //$FOCUSED often finds it when name lookup fails.
constexpr int kMaxWindowWaitAttempts = 40;

// Set by RegisterFindHelperTests; used by regression TestFunc (function pointer, no capture).
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - Test engine requires non-capturing TestFunc; hook stored here for regression tests
static IRegressionTestHook* g_regression_hook = nullptr;  // NOSONAR(cpp:S5421) - Must be reassigned in RegisterFindHelperTests

// Main window title (UIRenderer); used for precondition checks in smoke and UI window tests.
static constexpr const char* kMainWindowTitle = APP_DISPLAY_NAME_STR;  // NOLINT(readability-identifier-naming)

// Settings window / toolbar button refs (shared by RunSelectFolderAndIndexTest and ui_windows/settings_window_open test).
static constexpr const char* kSettingsWindowTitle = "Settings";  // SettingsWindow.cpp WindowGuard  // NOLINT(readability-identifier-naming)
static constexpr const char* kSettingsButtonRef = ICON_FA_GEAR " Settings##toolbar_settings";  // NOLINT(readability-identifier-naming)
static constexpr const char* kSettingsButtonPreconditionMsg =  // NOLINT(readability-identifier-naming)
  "Precondition failed: Settings button not found. Ensure main window is visible.";
static constexpr const char* kSettingsWindowOpenErrorMsg =  // NOLINT(readability-identifier-naming)
  "Settings window did not open after clicking Settings button. Check toolbar_settings ref and window title.";

// Forward declarations for RunSelectFolderAndIndexTest (defined below).
static void AssertIndexReplacedAndClose(ImGuiTestContext* ctx,
                                        ImGuiWindow* win,
                                        size_t index_size_before,
                                        size_t index_size_after,
                                        const std::string& folder_path,
                                        const char* case_id);
static void RunFixtureCrawlSearchAssertions(ImGuiTestContext* ctx,
                                            IRegressionTestHook* hook,
                                            const char* case_id);
static void RunFixtureFoldersOnlyCase(ImGuiTestContext* ctx,
                                      IRegressionTestHook* hook,
                                      const char* case_id);
static void RunFixtureExtFoldersAndCase(ImGuiTestContext* ctx,
                                        IRegressionTestHook* hook,
                                        const char* case_id);
static void RunFixtureFolderFileCountCase(ImGuiTestContext* ctx,
                                          IRegressionTestHook* hook,
                                          const char* case_id);

template <typename Predicate>
static bool WaitUntilFramesOrFail(ImGuiTestContext* ctx,
                                  const char* case_id,
                                  Predicate predicate,
                                  const char* error_message_template) {
  int waited = 0;
  while (!predicate() && waited < kMaxWaitFrames) {
    ctx->Yield();
    ++waited;
  }
  if (!predicate()) {
    IM_ERRORF(error_message_template, case_id, kMaxWaitFrames);
    return false;
  }
  return true;
}

static bool WaitForIndexReady(ImGuiTestContext* ctx, const IRegressionTestHook* hook, const char* case_id) {
  return WaitUntilFramesOrFail(ctx, case_id, [hook] { return hook->IsIndexReady(); },
                               "Regression test %s: index not ready after %d frames");
}

static bool WaitForSearchComplete(ImGuiTestContext* ctx, const IRegressionTestHook* hook, const char* case_id) {
  return WaitUntilFramesOrFail(ctx, case_id, [hook] { return hook->IsSearchComplete(); },
                               "Regression test %s: search did not complete after %d frames");
}

// --- UI window test helpers (reduce duplication; see docs/analysis/2026-02-23_IMGUI_TEST_ENGINE_HELPER_EXTRACTION_STUDY.md) ---

/** Fails test with precondition_msg if item_ref is not found. Call before ItemClick when the button is required. */
static void RequireItemExists(ImGuiTestContext* ctx, const char* item_ref, const char* precondition_msg) {
  if (!ctx->ItemExists(item_ref)) {
    IM_ERRORF("%s", precondition_msg);
    // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
    IM_CHECK(ctx->ItemExists(item_ref));
  }
}

/**
 * Clicks button_ref, waits for a floating window with the given title (or //$FOCUSED with matching name).
 * Returns the window, or nullptr if not found (caller must IM_CHECK(win != nullptr) to fail the test;
 * IM_CHECK cannot be used inside this function because it expands to a void return).
 */
static ImGuiWindow* WaitForFloatingWindowAfterClick(ImGuiTestContext* ctx,
                                                    const char* button_ref,
                                                    const char* window_title,
                                                    const char* error_msg) {
  ctx->ItemClick(button_ref);
  ImGuiWindow* win = nullptr;  // NOLINT(misc-const-correctness) - Returned pointer used with non-const APIs (SetRef)
  for (int attempt = 0; attempt < kMaxWindowWaitAttempts; ++attempt) {
    ctx->Yield();
    win = ctx->GetWindowByRef(window_title);
    if (win == nullptr) {
      win = ctx->GetWindowByRef("//$FOCUSED");
    }
    if (win != nullptr && std::strcmp(win->Name, window_title) == 0 && !win->Hidden) {
      break;
    }
    win = nullptr;
  }
  if (win == nullptr || win->Hidden) {
    IM_ERRORF("%s", error_msg);
    return nullptr;
  }
  return win;
}

/** Sets ref to the floating window, closes it, yields twice. Call after WaitForFloatingWindowAfterClick. */
static void CloseFloatingWindowAndYield(ImGuiTestContext* ctx, ImGuiWindow* win) {
  ctx->SetRef(win);
  ctx->WindowClose("");
  ctx->Yield();
  ctx->Yield();
}

/** Restores clipboard after shortcut tests (same pattern in all copy-shortcut tests). */
static void RestoreClipboardToOriginalOrClear(const std::string& original_clipboard) {
  if (!original_clipboard.empty()) {
    ImGui::SetClipboardText(original_clipboard.c_str());
  } else {
    ImGui::SetClipboardText("");
  }
}

/**
 * Opens a floating window from a main-window toolbar button (Help, Settings, Search Syntax).
 * Reduces duplication across ui_windows smoke tests.
 */
static void RunToolbarOpensFloatingWindowTest(ImGuiTestContext* ctx,
                                              const char* button_ref,
                                              const char* precondition_msg,
                                              const char* window_title,
                                              const char* error_msg) {
  ctx->SetRef(kMainWindowTitle);
  ctx->Yield();
  RequireItemExists(ctx, button_ref, precondition_msg);
  ImGuiWindow* win = WaitForFloatingWindowAfterClick(ctx, button_ref, window_title, error_msg);
  // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(win != nullptr);
  if (win != nullptr) {
    CloseFloatingWindowAndYield(ctx, win);
  }
}

/** Resolves folder path for index_configuration test: IMGUI_TEST_INDEX_FOLDER if set, else walk up from executable to find tests/data/fixture. */
static std::string ResolveIndexConfigFolderPath() {
  static std::string folder_path;
  if (!folder_path.empty()) {
    return folder_path;
  }
  if (const char* env_path = std::getenv("IMGUI_TEST_INDEX_FOLDER");  // NOLINT(concurrency-mt-unsafe) - test runs once on main thread
      env_path != nullptr && env_path[0] != '\0') {
    folder_path = env_path;
    return folder_path;
  }
  constexpr int kMaxResolutionLevels = 8;
  std::string base = path_utils::GetExecutableDirectory();
  if (!base.empty() && base.back() == path_utils::kPathSeparator) {
    base.pop_back();
  }
  for (int level = 0; level < kMaxResolutionLevels; ++level) {
    if (base.empty()) {
      break;
    }
    const std::filesystem::path candidate(path_utils::JoinPath(base, "tests/data/fixture"));
    try {
      if (std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate)) {
        folder_path = candidate.string();
        return folder_path;
      }
    } catch (const std::filesystem::filesystem_error&) {  // NOLINT(bugprone-empty-catch) - ignore and try parent directory. NOSONAR(cpp:S2486) - intentionally swallow to try next parent path
    }
    base = path_utils::GetParentDirectory(base);
  }
  folder_path = "tests/data/fixture";
  return folder_path;
}

/** Runs the "select folder and start indexing" test steps. Extracted to reduce lambda length (S1188) and nesting (S134). */
static void RunSelectFolderAndIndexTest(ImGuiTestContext* ctx) {
  constexpr const char* kCaseId = "select_folder_and_start_indexing";
  ctx->SetRef(kMainWindowTitle);
  ctx->Yield();
  IRegressionTestHook* hook = g_regression_hook;
  if (hook == nullptr) {
    IM_ERRORF("Index config test %s: IRegressionTestHook is null.", kCaseId);
    return;
  }
  const size_t index_size_before = hook->GetIndexSize();
  const std::string folder_path = ResolveIndexConfigFolderPath();
  RequireItemExists(ctx, kSettingsButtonRef, kSettingsButtonPreconditionMsg);
  ImGuiWindow* win =
      WaitForFloatingWindowAfterClick(ctx, kSettingsButtonRef, kSettingsWindowTitle, kSettingsWindowOpenErrorMsg);
  // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(win != nullptr);
  if (win == nullptr) {
    return;
  }
  ctx->SetRef(win);
  ctx->Yield();
  ctx->ItemClick("Index Configuration");
  ctx->Yield();
  ctx->ItemInput("##crawl_folder");
  ctx->KeyCharsReplace(folder_path.c_str());
  ctx->Yield();
  ctx->Yield();
  ctx->Yield();
  ctx->ItemClick(ICON_FA_PLAY " Start Indexing");
  ctx->Yield();
  if (!WaitForIndexReady(ctx, hook, kCaseId)) {
    CloseFloatingWindowAndYield(ctx, win);
    return;
  }
  const size_t index_size_after = hook->GetIndexSize();
  AssertIndexReplacedAndClose(ctx, win, index_size_before, index_size_after, folder_path, kCaseId);
  // After the Settings window is closed, run exact search assertions against
  // the known fixture structure. Extension-based counts are portable: ancestor
  // dirs added by DirectoryResolver carry no extension.
  ctx->SetRef(kMainWindowTitle);
  RunFixtureCrawlSearchAssertions(ctx, hook, kCaseId);
}

/** Asserts index was replaced (size changed) and closes Settings window; call from select_folder_and_start_indexing. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) - IM_CHECK macros and two branches; threshold 25
static void AssertIndexReplacedAndClose(ImGuiTestContext* ctx,
                                        ImGuiWindow* win,
                                        size_t index_size_before,
                                        size_t index_size_after,
                                        const std::string& folder_path,
                                        const char* case_id) {
  const bool size_zero = (index_size_after == 0U);
  const bool not_replaced = (index_size_after == index_size_before);
  if (size_zero) {
    IM_ERRORF("Index config test %s: index size 0 after indexing (path=%s). Check folder exists.",
              case_id, folder_path.c_str());
    // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
    IM_CHECK(index_size_after > 0U);
  }
  if (not_replaced) {
    IM_ERRORF(
      "Index config test %s: index was not replaced (size still %zu). UI may show 'Error: Folder does not exist' "
      "if path is wrong. Use absolute path: IMGUI_TEST_INDEX_FOLDER=/path/to/tests/data/fixture",
      case_id, index_size_after);
    // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
    IM_CHECK(index_size_after != index_size_before);
  }
  CloseFloatingWindowAndYield(ctx, win);
}

/**
 * Runs extension-filter search assertions against the crawled fixture index.
 * Called after select_folder_and_start_indexing has replaced the index with
 * tests/data/fixture contents.
 *
 * Extension counts are exact: ancestor dirs created by DirectoryResolver carry
 * no extension, so they never appear in extension-filtered results. This makes
 * these assertions portable across machines regardless of the fixture path depth.
 */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Test orchestration; IM_CHECK/IM_ERRORF macros add nesting
static void RunFixtureCrawlSearchAssertions(ImGuiTestContext* ctx,
                                            IRegressionTestHook* hook,
                                            const char* case_id) {
  struct FixtureCase {
    const char* ext;
    size_t expected;
  };
  static constexpr int kFixtureCaseCount = 4;
  static constexpr std::array<FixtureCase, kFixtureCaseCount> kFixtureCases = {{
    {"txt",  fixture_expected::kExtTxt},
    {"cpp",  fixture_expected::kExtCpp},
    {"h",    fixture_expected::kExtH},
    {"json", fixture_expected::kExtJson},
  }};

  for (const auto& fc : kFixtureCases) {
    hook->SetSearchParams(std::string_view{}, std::string_view{}, std::string_view{fc.ext}, false);
    hook->TriggerManualSearch();
    if (!WaitForSearchComplete(ctx, hook, case_id)) {
      return;
    }
    ctx->Yield();
    ctx->Yield();
    const size_t count = hook->GetSearchResultCount();
    if (count != fc.expected) {
      IM_ERRORF("Fixture crawl test %s: ext=%s expected %zu results, got %zu. "
                "Recreate fixture: scripts/create_test_fixture.sh",
                case_id, fc.ext, fc.expected, count);
    }
    // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK_EQ macro declares non-const bool internally
    IM_CHECK_EQ(count, fc.expected);
  }

  // Show-all: at least kMinTotal entries (exact count depends on fixture path depth).
  hook->SetSearchParams(std::string_view{}, std::string_view{}, std::string_view{}, false);
  hook->TriggerManualSearch();
  if (!WaitForSearchComplete(ctx, hook, case_id)) {
    return;
  }
  ctx->Yield();
  ctx->Yield();
  const size_t total = hook->GetSearchResultCount();
  if (total < fixture_expected::kMinTotal) {
    IM_ERRORF("Fixture crawl test %s: show_all expected >= %zu results, got %zu. "
              "Recreate fixture: scripts/create_test_fixture.sh",
              case_id, fixture_expected::kMinTotal, total);
  }
  // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(total >= fixture_expected::kMinTotal);
}

/**
 * Verifies that the folders_only filter returns at least kMinDirs results on the fixture index.
 * The lower bound (not exact) is used because DirectoryResolver adds ancestor dirs whose count
 * depends on the runtime path depth.
 */
static void RunFixtureFoldersOnlyCase(ImGuiTestContext* ctx,
                                      IRegressionTestHook* hook,
                                      const char* case_id) {
  hook->SetSearchParams(std::string_view{}, std::string_view{}, std::string_view{}, true);
  hook->TriggerManualSearch();
  if (!WaitForSearchComplete(ctx, hook, case_id)) {
    return;
  }
  ctx->Yield();
  ctx->Yield();
  const size_t count = hook->GetSearchResultCount();
  if (count < fixture_expected::kMinDirs) {
    IM_ERRORF("Fixture folders_only %s: expected >= %zu results, got %zu.",
              case_id, fixture_expected::kMinDirs, count);
  }
  // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(count >= fixture_expected::kMinDirs);
}

/**
 * Verifies that ext=cpp AND folders_only=true returns exactly 0 results: no directory in the
 * fixture has a .cpp extension. Tests that extension and folders_only filters are correctly AND'd.
 */
static void RunFixtureExtFoldersAndCase(ImGuiTestContext* ctx,
                                        IRegressionTestHook* hook,
                                        const char* case_id) {
  hook->SetSearchParams(std::string_view{}, std::string_view{}, std::string_view{"cpp"}, true);
  hook->TriggerManualSearch();
  if (!WaitForSearchComplete(ctx, hook, case_id)) {
    return;
  }
  ctx->Yield();
  ctx->Yield();
  const size_t count = hook->GetSearchResultCount();
  if (count != fixture_expected::kExtCppFoldersOnly) {
    IM_ERRORF("Fixture ext+folders_only AND %s: expected %zu results (ext=cpp, folders_only=true), got %zu.",
              case_id, fixture_expected::kExtCppFoldersOnly, count);
  }
  // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK_EQ macro declares non-const bool internally
  IM_CHECK_EQ(count, fixture_expected::kExtCppFoldersOnly);
}

/** Robust helper for C++17 string_view::ends_with (not in standard until C++20). */
static bool StringEndsWith(std::string_view str, std::string_view suffix) {
  return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/** Robust path matcher for fixture folders, handling cross-platform separators and root/nested paths. */
static bool MatchFixturePath(std::string_view full_path, std::string_view folder_name) {
  if (full_path == folder_name) {
    return true;
  }
  const size_t name_len = folder_name.size();
  if (full_path.size() <= name_len) {
    return false;
  }
  const size_t separator_index = full_path.size() - name_len - 1U;
  const char separator = full_path.at(separator_index);
  return (separator == '/' || separator == '\\') && StringEndsWith(full_path, folder_name);
}

[[nodiscard]] static bool FixtureFolderCountsAllReady(const IRegressionTestHook* hook) {
  const size_t total = hook->GetSearchResultCount();
  bool alpha_ready = false;
  bool beta_ready = false;
  bool sub_ready = false;
  bool gamma_ready = false;
  for (size_t i = 0; i < total; ++i) {
    const std::string_view path = hook->GetSearchResultPath(i);
    if (const uint64_t count = hook->GetSearchResultFolderFileCount(i);
        count == kFolderFileCountNotLoaded) {
      continue;
    }
    if (MatchFixturePath(path, "alpha")) {
      alpha_ready = true;
    } else if (MatchFixturePath(path, "beta")) {
      beta_ready = true;
    } else if (MatchFixturePath(path, "sub")) {
      sub_ready = true;
    } else if (MatchFixturePath(path, "gamma")) {
      gamma_ready = true;
    }
  }
  return alpha_ready && beta_ready && sub_ready && gamma_ready;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - IM_CHECK_EQ/IM_ERRORF per branch inflate metric; logic is linear over result rows
static void AssertFixtureFolderFileCountExpectations(const IRegressionTestHook* hook, const char* case_id) {
  const size_t total = hook->GetSearchResultCount();
  bool found_alpha = false;
  bool found_beta = false;
  bool found_sub = false;
  bool found_gamma = false;
  bool found_file = false;

  for (size_t i = 0; i < total; ++i) {
    const std::string_view path = hook->GetSearchResultPath(i);
    const uint64_t count = hook->GetSearchResultFolderFileCount(i);

    if (MatchFixturePath(path, "alpha")) {
      found_alpha = true;
      if (count != fixture_expected::kFolderFilesAlpha) {
        IM_ERRORF("Fixture folder_file_counts %s: alpha expected %llu files, got %llu",
                  case_id, static_cast<unsigned long long>(fixture_expected::kFolderFilesAlpha), static_cast<unsigned long long>(count));
      }
      IM_CHECK_EQ(count, fixture_expected::kFolderFilesAlpha); // NOLINT(misc-const-correctness) - IM_CHECK_EQ macro declares non-const bool internally
    } else if (MatchFixturePath(path, "beta")) {
      found_beta = true;
      if (count != fixture_expected::kFolderFilesBeta) {
        IM_ERRORF("Fixture folder_file_counts %s: beta expected %llu files, got %llu",
                  case_id, static_cast<unsigned long long>(fixture_expected::kFolderFilesBeta), static_cast<unsigned long long>(count));
      }
      IM_CHECK_EQ(count, fixture_expected::kFolderFilesBeta); // NOLINT(misc-const-correctness) - IM_CHECK_EQ macro declares non-const bool internally
    } else if (MatchFixturePath(path, "sub")) {
      found_sub = true;
      if (count != fixture_expected::kFolderFilesSub) {
        IM_ERRORF("Fixture folder_file_counts %s: sub expected %llu files, got %llu",
                  case_id, static_cast<unsigned long long>(fixture_expected::kFolderFilesSub), static_cast<unsigned long long>(count));
      }
      IM_CHECK_EQ(count, fixture_expected::kFolderFilesSub); // NOLINT(misc-const-correctness) - IM_CHECK_EQ macro declares non-const bool internally
    } else if (MatchFixturePath(path, "gamma")) {
      found_gamma = true;
      if (count != fixture_expected::kFolderFilesGamma) {
        IM_ERRORF("Fixture folder_file_counts %s: gamma expected %llu files, got %llu",
                  case_id, static_cast<unsigned long long>(fixture_expected::kFolderFilesGamma), static_cast<unsigned long long>(count));
      }
      IM_CHECK_EQ(count, fixture_expected::kFolderFilesGamma); // NOLINT(misc-const-correctness) - IM_CHECK_EQ macro declares non-const bool internally
    } else if (StringEndsWith(path, "a.txt")) {
      found_file = true;
      if (count != kFolderFileCountNotLoaded) {
        IM_ERRORF("Fixture folder_file_counts %s: file a.txt expected sentinel, got %llu",
                  case_id, static_cast<unsigned long long>(count));
      }
      IM_CHECK_EQ(count, kFolderFileCountNotLoaded); // NOLINT(misc-const-correctness) - IM_CHECK_EQ macro declares non-const bool internally
    }
  }

  IM_CHECK(found_alpha); // NOLINT(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(found_beta);  // NOLINT(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(found_sub);   // NOLINT(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(found_gamma); // NOLINT(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(found_file);  // NOLINT(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
}

/**
 * Verifies the "# Files" column for fixture results: recursive non-directory counts
 * for directories, and the sentinel value for file entries.
 */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Preconditions use many IM_CHECK branches; kept in one void function (IM_CHECK bare return)
static void RunFixtureFolderFileCountCase(ImGuiTestContext* ctx,
                                          IRegressionTestHook* hook,
                                          const char* case_id) {
  hook->SetSearchParams(std::string_view{}, std::string_view{}, std::string_view{}, false);
  if (!hook->GetSearchParamFilename().empty() || !hook->GetSearchParamPath().empty() ||
      !hook->GetSearchParamExtensions().empty() || hook->GetSearchParamFoldersOnly()) {
    IM_ERRORF("Fixture folder_file_counts %s: search params were not correctly reset.", case_id);
    IM_CHECK(hook->GetSearchParamFilename().empty());  // NOLINT(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
    IM_CHECK(hook->GetSearchParamPath().empty());  // NOLINT(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
    IM_CHECK(hook->GetSearchParamExtensions().empty());  // NOLINT(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
    IM_CHECK(!hook->GetSearchParamFoldersOnly());  // NOLINT(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  }

  hook->TriggerManualSearch();
  if (!WaitForSearchComplete(ctx, hook, case_id)) {
    return;
  }

  if (const bool folder_counts_loaded =
          WaitUntilFramesOrFail(ctx, case_id,
                                [hook] { return FixtureFolderCountsAllReady(hook); },
                                "Regression test %s: folder counts for alpha/beta/sub/gamma not loaded after %d frames");
      !folder_counts_loaded) {
    return;
  }

  ctx->Yield();
  ctx->Yield();

  AssertFixtureFolderFileCountExpectations(hook, case_id);
}

/**
 * After SetSearchParams: trigger search, wait, yield×2, require non-empty results, snapshot clipboard,
 * select first result. Returns false if search did not complete (caller should return).
 */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Test orchestration; IM_CHECK/IM_ERRORF macros add nesting
[[nodiscard]] static bool RunShortcutSearchPreambleForClipboardTest(ImGuiTestContext* ctx,
                                                                    IRegressionTestHook* hook,
                                                                    const char* case_id,
                                                                    std::string& out_original_clipboard) {
  hook->TriggerManualSearch();
  if (!WaitForSearchComplete(ctx, hook, case_id)) {
    return false;
  }
  ctx->Yield();
  ctx->Yield();

  if (const size_t result_count = hook->GetSearchResultCount();
      result_count == 0U) {
    IM_ERRORF("Shortcut test %s: search returned 0 results; expected non-empty result list.", case_id);
    // IM_CHECK uses bare `return`; use IM_CHECK_RETV so this bool function always returns a value (Sonar S836).
    // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK_RETV macro declares non-const bool internally
    IM_CHECK_RETV(result_count > 0U, false);
  }

  const char* original_cstr = ImGui::GetClipboardText();
  out_original_clipboard =
    (original_cstr != nullptr) ? std::string(original_cstr) : std::string{};
  hook->RequestSetSelectionAndMarkFirstResult();
  return true;
}

/** Runs the copy-marked-paths shortcut test body (W / Copy Paths path). Call after index ready. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Test orchestration; IM_CHECK/IM_ERRORF macros add nesting
static void RunCopyMarkedPathsShortcutTest(ImGuiTestContext* ctx,
                                           IRegressionTestHook* hook,
                                           const char* case_id) {
  hook->SetSearchParams(std::string_view{}, std::string_view{}, std::string_view{}, false);
  std::string original_clipboard;
  if (!RunShortcutSearchPreambleForClipboardTest(ctx, hook, case_id, original_clipboard)) {
    return;
  }
  hook->RequestCopyMarkedPathsToClipboard();
  ctx->Yield();
  ctx->Yield();
  ctx->Yield();

  const std::string clipboard_after = hook->GetClipboardText();
  if (clipboard_after.empty()) {
    IM_ERRORF("Shortcut test %s: clipboard is empty after copy; expected at least one path.", case_id);
  }
  // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(!clipboard_after.empty());

  RestoreClipboardToOriginalOrClear(original_clipboard);
}

/** Runs the copy-marked-filenames shortcut test body (Shift+W). Call after index ready. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Test orchestration; IM_CHECK/IM_ERRORF macros add nesting
static void RunCopyMarkedFilenamesShortcutTest(ImGuiTestContext* ctx,
                                               IRegressionTestHook* hook,
                                               const char* case_id) {
  // Narrow search to a deterministic, small subset: files with ".log" extension.
  // This makes the clipboard assertion less random while still exercising the shortcut path.
  hook->SetSearchParams(std::string_view{}, std::string_view{}, std::string_view{"log"}, false);
  std::string original_clipboard;
  if (!RunShortcutSearchPreambleForClipboardTest(ctx, hook, case_id, original_clipboard)) {
    return;
  }
  hook->RequestCopyMarkedFilenamesToClipboard();
  ctx->Yield();
  ctx->Yield();
  ctx->Yield();

  const std::string clipboard_after = hook->GetClipboardText();
  if (clipboard_after.empty()) {
    IM_ERRORF("Shortcut test %s: clipboard is empty after copy; expected at least one filename.", case_id);
  }
  // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(!clipboard_after.empty());
  // Shift+W copies filenames only (no path separators). Evaluate this per line (newline-separated
  // filenames) so tests remain robust if multiple files are ever marked.
  bool all_lines_without_separators = true;
  std::string_view remaining{clipboard_after};
  while (!remaining.empty() && all_lines_without_separators) {
    const size_t newline_pos = remaining.find('\n');
    if (const std::string_view line =
          (newline_pos == std::string_view::npos) ? remaining : remaining.substr(0, newline_pos);
        !line.empty() &&
        (line.find('/') != std::string_view::npos ||
         line.find('\\') != std::string_view::npos)) {
      all_lines_without_separators = false;
    }
    if (newline_pos == std::string_view::npos) {
      remaining = {};
    } else {
      remaining.remove_prefix(newline_pos + 1U);
    }
  }
  if (!all_lines_without_separators) {
    IM_ERRORF("Shortcut test %s: clipboard should contain filenames only (no / or \\ in any line); got path-like content.", case_id);
  }
  // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(all_lines_without_separators);

  RestoreClipboardToOriginalOrClear(original_clipboard);
}

/** Runs the copy-selected-path shortcut test body (Ctrl/Cmd+Shift+C). Call after index ready. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Test orchestration; IM_CHECK/IM_ERRORF macros add nesting
static void RunCopySelectedPathShortcutTest(ImGuiTestContext* ctx,
                                            IRegressionTestHook* hook,
                                            const char* case_id) {
  hook->SetSearchParams(std::string_view{}, std::string_view{}, std::string_view{}, false);
  std::string original_clipboard;
  if (!RunShortcutSearchPreambleForClipboardTest(ctx, hook, case_id, original_clipboard)) {
    return;
  }
  hook->RequestCopySelectedPathToClipboard();
  ctx->Yield();
  ctx->Yield();
  ctx->Yield();

  const std::string clipboard_after = hook->GetClipboardText();
  if (clipboard_after.empty()) {
    IM_ERRORF("Shortcut test %s: clipboard is empty after copy; expected selected path.", case_id);
  }
  // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(!clipboard_after.empty());
  // Ctrl+Shift+C copies full path (should contain path separator)
  const bool looks_like_path = clipboard_after.find('/') != std::string::npos ||
                              clipboard_after.find('\\') != std::string::npos;
  if (!looks_like_path) {
    IM_ERRORF("Shortcut test %s: clipboard should contain a path (with / or \\); got non-path content.", case_id);
  }
  // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(looks_like_path);

  RestoreClipboardToOriginalOrClear(original_clipboard);
}

/** Shared hook-null check, index wait, and body for shortcut tests. */
template<typename RunBody>
static void RunShortcutTestWithHook(ImGuiTestContext* ctx,
                                    const char* case_id,
                                    RunBody&& run_body) {
  // Mark this coroutine thread as the UI thread so UIThreadOwned<T> assertions pass.
  // IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL runs each TestFunc on a new
  // std::thread, but the coroutine and render loop are cooperatively scheduled
  // (never concurrent) — the test thread IS the active/owning thread during its slice.
  MarkCurrentThreadAsUI();
  ctx->SetRef(kMainWindowTitle);
  IRegressionTestHook* hook = g_regression_hook;
  if (hook == nullptr) {
    IM_ERRORF("Shortcut test %s: IRegressionTestHook is null (misconfiguration). Ensure Application passes a non-null hook to RegisterFindHelperTests.", case_id);
    return;
  }
  if (!WaitForIndexReady(ctx, hook, case_id)) {
    return;
  }
  std::invoke(std::forward<RunBody>(run_body), ctx, hook, case_id);
}

// Parameters for RunRegressionTestCase (single struct avoids cpp:S107 too-many-parameters on the runner).
struct RegressionCase {
  const char* case_id;
  const char* filename;
  const char* path;
  const char* extensions;
  bool folders_only;
  size_t expected;
};

// Runs one regression case: wait index ready, set params, trigger search,
// wait for completion, then assert final result count.
// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Test orchestration; splitting would scatter related steps
static void RunRegressionTestCase(ImGuiTestContext* ctx, const RegressionCase& c) {
  // Mark this coroutine thread as the UI thread so UIThreadOwned<T> assertions pass.
  // See RunShortcutTestWithHook for the full rationale.
  MarkCurrentThreadAsUI();
  IRegressionTestHook* hook = g_regression_hook;
  ctx->SetRef(kMainWindowTitle);
  if (hook == nullptr) {
    IM_ERRORF("Regression test %s: IRegressionTestHook is null (misconfiguration). Ensure Application passes a non-null hook to RegisterFindHelperTests.", c.case_id);
    return;
  }

  if (!WaitForIndexReady(ctx, hook, c.case_id)) {
    return;
  }

  if (const size_t index_size = hook->GetIndexSize();
      index_size != regression_expected::kIndexSize) {
    IM_ERRORF("Regression test %s: wrong index (got %zu entries, expected %zu). Run app with: --index-from-file=tests/data/std-linux-filesystem.txt", c.case_id, index_size, regression_expected::kIndexSize);
    // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
    IM_CHECK(index_size == regression_expected::kIndexSize);
    return;
  }

  // Normalize once: nullptr → empty string so setters and getters use the same convention; pass strings to SetSearchParams to avoid UB (string_view from nullptr).
  const std::string expected_filename(c.filename != nullptr ? c.filename : "");
  const std::string expected_path(c.path != nullptr ? c.path : "");
  const std::string expected_ext(c.extensions != nullptr ? c.extensions : "");
  hook->SetSearchParams(expected_filename, expected_path, expected_ext, c.folders_only);
  // Precondition: verify search params were applied (surfaces state_ or hook misapplication early).
  if (hook->GetSearchParamFilename() != expected_filename || hook->GetSearchParamPath() != expected_path ||
      hook->GetSearchParamExtensions() != expected_ext || hook->GetSearchParamFoldersOnly() != c.folders_only) {
    IM_ERRORF("Search params precondition failed for %s: set filename=\"%s\" path=\"%s\" ext=\"%s\" folders_only=%s but getters differ. Check state_ and hook.",
              c.case_id, expected_filename.c_str(), expected_path.c_str(), expected_ext.c_str(), c.folders_only ? "true" : "false");
  }
  // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(hook->GetSearchParamFilename() == expected_filename);
  // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(hook->GetSearchParamPath() == expected_path);
  // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(hook->GetSearchParamExtensions() == expected_ext);
  // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
  IM_CHECK(hook->GetSearchParamFoldersOnly() == c.folders_only);
  hook->TriggerManualSearch();

  if (!WaitForSearchComplete(ctx, hook, c.case_id)) {
    return;
  }

  // CRITICAL: Two consecutive yields so the main loop applies results to state_ before we read
  // GetSearchResultCount (PollResults may run one frame after the worker completes).
  // Do not remove or reduce.
  ctx->Yield();
  ctx->Yield();

  const size_t count = hook->GetSearchResultCount();
  if (count != c.expected) {
    IM_ERRORF("Regression test %s: expected %zu results, got %zu (run app with --index-from-file=tests/data/std-linux-filesystem.txt)", c.case_id, c.expected, count);
  }
  // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK_EQ macro declares non-const bool internally
  IM_CHECK_EQ(count, c.expected);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Registration orchestrates all tests; splitting would scatter related cases
void RegisterFindHelperTests(ImGuiTestEngine* engine, IRegressionTestHook* hook) {  // NOSONAR(cpp:S995) - Hook stored in g_regression_hook and used for non-const calls (SetSearchParams, TriggerManualSearch)
  ImGuiTest* t = IM_REGISTER_TEST(engine, "smoke", "main_window_ref");
  t->TestFunc = [](ImGuiTestContext* ctx) {
    ctx->SetRef(kMainWindowTitle);
    ctx->Yield();  // Yield one frame (proves engine/coroutine run). Main window existence is assumed; UI window tests validate it via ItemExists(button).
  };

  // UI window tests: open floating windows via toolbar to improve coverage (HelpWindow, etc.). See docs/analysis/2026-02-22_IMGUI_TEST_ENGINE_COVERAGE_OPPORTUNITIES.md
  // Try exact title first; fall back to //$FOCUSED when lookup by name fails (e.g. cross-viewport). Always validate win->Name so we never accept the wrong window.
  // Precondition: main window must exist and be visible; enforced at start of smoke and UI tests. See docs/analysis/2026-02-23_IMGUI_TEST_ENGINE_PRECONDITIONS.md
  static constexpr const char* kHelpWindowTitle = HELP_WINDOW_TITLE_STR;  // HelpWindow.cpp  // NOLINT(readability-identifier-naming)
  // kSettingsWindowTitle, kSettingsButtonRef, kSettingsButtonPreconditionMsg, kSettingsWindowOpenErrorMsg are at file scope (used by RunSelectFolderAndIndexTest)
  static constexpr const char* kSearchSyntaxGuideTitle = "Search Syntax Guide";  // SearchHelpWindow.cpp  // NOLINT(readability-identifier-naming)
  static constexpr const char* kMetricsWindowTitle = METRICS_WINDOW_TITLE_STR;  // MetricsWindow.cpp (only when app run with --show-metrics)  // NOLINT(readability-identifier-naming)

  static const char* kHelpButtonRef = ICON_FA_BOOK_OPEN " Help##toolbar_help";  // Full label so ItemClick finds widget  // NOLINT(readability-identifier-naming)
  static const char* kHelpButtonPreconditionMsg =  // NOLINT(readability-identifier-naming)
    "Precondition failed: Help button not found. Ensure main window is visible.";
  static const char* kHelpWindowOpenErrorMsg =  // NOLINT(readability-identifier-naming)
    "Help window did not open after clicking Help button. Check toolbar_help ref and window title.";
  ImGuiTest* t_help = IM_REGISTER_TEST(engine, "ui_windows", "help_window_open");
  t_help->TestFunc = [](ImGuiTestContext* ctx) {
    RunToolbarOpensFloatingWindowTest(ctx, kHelpButtonRef, kHelpButtonPreconditionMsg, kHelpWindowTitle,
                                      kHelpWindowOpenErrorMsg);
  };

  // Settings window: open via toolbar button (FilterPanel ##toolbar_settings). Uses file-scope kSettings* constants.
  ImGuiTest* t_settings = IM_REGISTER_TEST(engine, "ui_windows", "settings_window_open");
  t_settings->TestFunc = [](ImGuiTestContext* ctx) {
    RunToolbarOpensFloatingWindowTest(ctx, kSettingsButtonRef, kSettingsButtonPreconditionMsg, kSettingsWindowTitle,
                                      kSettingsWindowOpenErrorMsg);
  };

  // Search Syntax Guide: open via search-area button (SearchInputs ##SearchHelpToggle).
  static const char* kSearchHelpButtonRef = "**/" ICON_FA_BOOK_OPEN "##SearchHelpToggle";  // NOLINT(readability-identifier-naming)
  static const char* kSearchHelpButtonPreconditionMsg =  // NOLINT(readability-identifier-naming)
    "Precondition failed: Search Syntax button not found. Ensure main window is visible.";
  static const char* kSearchHelpWindowOpenErrorMsg =  // NOLINT(readability-identifier-naming)
    "Search Syntax Guide did not open after clicking Search Help button. Check SearchHelpToggle ref and window title.";
  ImGuiTest* t_search_help = IM_REGISTER_TEST(engine, "ui_windows", "search_help_window_open");
  t_search_help->TestFunc = [](ImGuiTestContext* ctx) {
    RunToolbarOpensFloatingWindowTest(ctx, kSearchHelpButtonRef, kSearchHelpButtonPreconditionMsg,
                                      kSearchSyntaxGuideTitle, kSearchHelpWindowOpenErrorMsg);
  };

  // Metrics window: open via toolbar button (FilterPanel; button only visible when app run with --show-metrics).
  // Pre-check: skip test gracefully when Metrics button is not available (avoids false negatives in CI without --show-metrics).
  static const char* kMetricsButtonRef = ICON_FA_CHART_BAR " Metrics";  // No ##id in FilterPanel; label is the ref  // NOLINT(readability-identifier-naming)
  static const char* kMetricsWindowOpenErrorMsg =  // NOLINT(readability-identifier-naming)
    "Metrics window did not open after clicking Metrics button. Run app with --show-metrics; check window title.";
  ImGuiTest* t_metrics = IM_REGISTER_TEST(engine, "ui_windows", "metrics_window_open");
  t_metrics->TestFunc = [](ImGuiTestContext* ctx) {
    ctx->SetRef(kMainWindowTitle);
    ctx->Yield();
    if (!ctx->ItemExists(kMetricsButtonRef)) {
      return;  // Skip: Metrics button not available (app not run with --show-metrics); avoid false negative in CI
    }
    ImGuiWindow* win = WaitForFloatingWindowAfterClick(ctx, kMetricsButtonRef, kMetricsWindowTitle, kMetricsWindowOpenErrorMsg);
    // NOLINTNEXTLINE(misc-const-correctness) - IM_CHECK macro expands to non-const bool internally
    IM_CHECK(win != nullptr);
    CloseFloatingWindowAndYield(ctx, win);
  };

  // Shortcuts: verify that copying marked paths to the clipboard works (same path as W and "Copy Paths" button).
  // Uses the regression hook to set selection+mark and request copy, then asserts clipboard content changed.
  ImGuiTest* t_copy_marked_paths_w = IM_REGISTER_TEST(engine, "shortcuts", "copy_marked_paths_W_single");
  t_copy_marked_paths_w->TestFunc = [](ImGuiTestContext* ctx) {
    RunShortcutTestWithHook(ctx, "copy_marked_paths_W_single", RunCopyMarkedPathsShortcutTest);
  };

  ImGuiTest* t_copy_marked_filenames_shift_w = IM_REGISTER_TEST(engine, "shortcuts", "copy_marked_filenames_Shift_W_single");
  t_copy_marked_filenames_shift_w->TestFunc = [](ImGuiTestContext* ctx) {
    RunShortcutTestWithHook(ctx, "copy_marked_filenames_Shift_W_single", RunCopyMarkedFilenamesShortcutTest);
  };

  ImGuiTest* t_copy_selected_path_ctrl_shift_c = IM_REGISTER_TEST(engine, "shortcuts", "copy_selected_path_Ctrl_Shift_C");
  t_copy_selected_path_ctrl_shift_c->TestFunc = [](ImGuiTestContext* ctx) {
    RunShortcutTestWithHook(ctx, "copy_selected_path_Ctrl_Shift_C", RunCopySelectedPathShortcutTest);
  };

  if (g_regression_hook != nullptr && g_regression_hook != hook) {
    LOG_ERROR_BUILD("RegisterFindHelperTests: g_regression_hook already set to different hook; possible double registration with different engine or fixture.");
    return;
  }

  g_regression_hook = hook;
  if (hook == nullptr) {
    LOG_ERROR_BUILD("RegisterFindHelperTests: hook is null (misconfiguration). Regression tests require a non-null IRegressionTestHook. Skipping regression test registration.");
    return;
  }

  // Single source of truth for regression cases (reduces duplication and drift).
  // TestFunc must be a non-capturing lambda (raw function pointer), so we register each test explicitly and reference this table.
  // Expected counts come from regression_expected (ImGuiTestEngineRegressionExpected.h); all registered tests use kRegressionCases[0..5].
  static constexpr int kRegressionCaseCount = 6;
  using RegressionCaseArray = std::array<RegressionCase, kRegressionCaseCount>;
  static const RegressionCaseArray kRegressionCases = {{
    {"show_all", "", "", "", false, regression_expected::kShowAll},
    {"filename_tty", "tty", "", "", false, regression_expected::kFilenameTty},
    {"path_dev", "", "/dev", "", false, regression_expected::kPathDev},
    {"ext_conf", "", "", "conf", false, regression_expected::kExtConf},
    {"path_etc_ext_conf", "", "/etc", "conf", false, regression_expected::kPathEtcExtConf},
    {"folders_only", "", "", "", true, regression_expected::kFoldersOnly},
  }};
  static_assert(static_cast<int>(kRegressionCases.size()) == kRegressionCaseCount,
                "Regression case table size must match registration (r0..r5)");

  static constexpr int kLastRegressionCaseIndex = kRegressionCaseCount - 1;

  // Regression: kRegressionCases indices are 0..5 and kLastRegressionCaseIndex (5).
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  // Regression: one test per case (index 0..5).
  ImGuiTest* r0 = IM_REGISTER_TEST(engine, "regression", kRegressionCases[0].case_id);
  r0->TestFunc = [](ImGuiTestContext* ctx) {
    const auto& c = kRegressionCases[0];
    RunRegressionTestCase(ctx, c);
  };
  ImGuiTest* r1 = IM_REGISTER_TEST(engine, "regression", kRegressionCases[1].case_id);
  r1->TestFunc = [](ImGuiTestContext* ctx) {
    const auto& c = kRegressionCases[1];
    RunRegressionTestCase(ctx, c);
  };
  ImGuiTest* r2 = IM_REGISTER_TEST(engine, "regression", kRegressionCases[2].case_id);
  r2->TestFunc = [](ImGuiTestContext* ctx) {
    const auto& c = kRegressionCases[2];
    RunRegressionTestCase(ctx, c);
  };
  ImGuiTest* r3 = IM_REGISTER_TEST(engine, "regression", kRegressionCases[3].case_id);
  r3->TestFunc = [](ImGuiTestContext* ctx) {
    const auto& c = kRegressionCases[3];
    RunRegressionTestCase(ctx, c);
  };
  ImGuiTest* r4 = IM_REGISTER_TEST(engine, "regression", kRegressionCases[4].case_id);
  r4->TestFunc = [](ImGuiTestContext* ctx) {
    const auto& c = kRegressionCases[4];
    RunRegressionTestCase(ctx, c);
  };
  ImGuiTest* r5 = IM_REGISTER_TEST(engine, "regression", kRegressionCases[5].case_id);
  r5->TestFunc = [](ImGuiTestContext* ctx) {
    const auto& c = kRegressionCases[kLastRegressionCaseIndex];
    RunRegressionTestCase(ctx, c);
  };

  // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

  // Index configuration: select a folder and start indexing. Must run before the fixture wildcard tests
  // because it replaces the initial index (from --index-from-file); all regression/shortcut tests
  // depend on that index. Folder path: IMGUI_TEST_INDEX_FOLDER env var if
  // set, else resolved by walking up from the executable directory (so it works when .app CWD is
  // bundle Resources). See internal-docs/analysis/2026-03-14_IMGUI_TEST_SELECT_FOLDER_TO_INDEX_ASSESSMENT.md.
  ImGuiTest* t_index_cfg = IM_REGISTER_TEST(engine, "index_configuration", "select_folder_and_start_indexing");
  t_index_cfg->TestFunc = [](ImGuiTestContext* ctx) { RunSelectFolderAndIndexTest(ctx); };

  // Fixture tests: run after select_folder_and_start_indexing has crawled tests/data/fixture.
  // These tests exercise the same search dimensions as the regression suites but on a real crawled
  // index rather than a text-file-loaded snapshot, and on a corpus small enough for exact (or
  // lower-bounded) assertions. Extension counts are exact (ancestor dirs carry no extension);
  // dir/total counts use >= because DirectoryResolver inserts ancestor dirs whose count depends on
  // the runtime path depth.

  // Folders-only filter: must return at least kMinDirs=6 on the fixture.
  ImGuiTest* fx_dirs = IM_REGISTER_TEST(engine, "fixture", "folders_only");
  fx_dirs->TestFunc = [](ImGuiTestContext* ctx) {
    IRegressionTestHook* hook = g_regression_hook;
    if (hook != nullptr) {
      RunFixtureFoldersOnlyCase(ctx, hook, "folders_only");
    }
  };

  // Extension AND folders_only: ext=cpp + folders_only=true must return kExtCppFoldersOnly=0.
  ImGuiTest* fx_ext_dirs = IM_REGISTER_TEST(engine, "fixture", "ext_cpp_folders_only_zero");
  fx_ext_dirs->TestFunc = [](ImGuiTestContext* ctx) {
    IRegressionTestHook* hook = g_regression_hook;
    if (hook != nullptr) {
      RunFixtureExtFoldersAndCase(ctx, hook, "ext_cpp_folders_only_zero");
    }
  };

  // Folder file count: check that "# Files" column contains expected recursive non-directory counts.
  ImGuiTest* fx_ffc = IM_REGISTER_TEST(engine, "fixture", "folder_file_counts");
  fx_ffc->TestFunc = [](ImGuiTestContext* ctx) {
    IRegressionTestHook* hook = g_regression_hook;
    if (hook != nullptr) {
      RunFixtureFolderFileCountCase(ctx, hook, "folder_file_counts");
    }
  };
}

#else
// ENABLE_IMGUI_TEST_ENGINE is not defined: this TU is intentionally empty.
// Do not add code here; this file is only compiled when the option is ON (CMake).
#endif  // ENABLE_IMGUI_TEST_ENGINE
