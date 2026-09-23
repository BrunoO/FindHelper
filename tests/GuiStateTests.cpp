/**
 * @file GuiStateTests.cpp
 * @brief Unit tests for GuiState input field operations
 *
 * Tests cover:
 * - Input field initialization and default values
 * - ClearInputs() method
 * - GuiState::BuildCurrentSearchParams() conversion
 * - Path and filename fields independent (no path-to-filename move)
 * - String operations (assignment, copying, length checks)
 * - ApplySearchConfig() and ApplyShowAllPreset()
 *
 * These tests ensure safe refactoring from char[256] arrays to SearchInputField class.
 */

// Mark test thread as UI thread so UIThreadOwned<T> asserts pass in tests.
// All GuiState mutations in tests happen on this (single test) thread.
#define DOCTEST_CONFIG_IMPLEMENT

#include <chrono>
#include <string>
#include <vector>

#include "api/GeminiApiUtils.h"
#include "doctest/doctest.h"
#include "gui/GuiState.h"
#include "index/LazyValue.h"
#include "search/SearchControllerDetail.h"
#include "search/SearchHistory.h"
#include "utils/StringUtils.h"
#include "utils/ThreadUtils.h"

int main(int argc, char** argv) {
    MarkCurrentThreadAsUI();
    doctest::Context context(argc, argv);
    return context.run();
}

// Helper function to set SearchInputField value (replaces SetInputField)
void SetInputField(SearchInputField& field, const std::string& value) {
  field.SetValue(value);
}

// Helper function to check if SearchInputField is empty (replaces IsInputFieldEmpty)
bool IsInputFieldEmpty(const SearchInputField& field) {
  return field.IsEmpty();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Aggregated doctest suite with many small cases
TEST_SUITE("GuiState Input Fields") {

  TEST_CASE("Default construction - all inputs empty") {
    GuiState state;

    REQUIRE(IsInputFieldEmpty(state.searchCriteria.extension_input));
    REQUIRE(IsInputFieldEmpty(state.searchCriteria.filename_input));
    REQUIRE(IsInputFieldEmpty(state.searchCriteria.path_input));
    REQUIRE(state.searchCriteria.extension_input.AsString().empty());
    REQUIRE(state.searchCriteria.filename_input.AsString().empty());
    REQUIRE(state.searchCriteria.path_input.AsString().empty());
  }

  TEST_CASE("ClearInputs - clears all input fields") {
    GuiState state;

    // Set some values
    SetInputField(state.searchCriteria.extension_input, "txt;cpp");
    SetInputField(state.searchCriteria.filename_input, "test");
    SetInputField(state.searchCriteria.path_input, "/path/to/file");
    state.searchCriteria.folders_only = true;
    state.searchCriteria.case_sensitive = true;

    // Verify they're set
    REQUIRE(!IsInputFieldEmpty(state.searchCriteria.extension_input));
    REQUIRE(!IsInputFieldEmpty(state.searchCriteria.filename_input));
    REQUIRE(!IsInputFieldEmpty(state.searchCriteria.path_input));

    // Clear
    state.ClearInputs();

    // Verify they're cleared
    REQUIRE(IsInputFieldEmpty(state.searchCriteria.extension_input));
    REQUIRE(IsInputFieldEmpty(state.searchCriteria.filename_input));
    REQUIRE(IsInputFieldEmpty(state.searchCriteria.path_input));
    REQUIRE(state.searchCriteria.folders_only == false);
    REQUIRE(state.searchCriteria.case_sensitive == false);
  }

  TEST_CASE("Input field assignment - normal strings") {
    GuiState state;

    SetInputField(state.searchCriteria.extension_input, "txt");
    SetInputField(state.searchCriteria.filename_input, "test");
    SetInputField(state.searchCriteria.path_input, "/path");

    REQUIRE(state.searchCriteria.extension_input.AsString() == "txt");
    REQUIRE(state.searchCriteria.filename_input.AsString() == "test");
    REQUIRE(state.searchCriteria.path_input.AsString() == "/path");
  }

  TEST_CASE("Input field assignment - long strings (truncation)") {
    GuiState state;

    constexpr std::size_t kBufferLength = 256;
    constexpr std::size_t kMaxChars = kBufferLength - 1U;
    constexpr std::size_t kOverlongChars = kMaxChars + 45U;

    // Create a string longer than 256 characters
    const std::string long_string(kOverlongChars, 'a');
    SetInputField(state.searchCriteria.filename_input, long_string);

    // Should be truncated to 255 characters (256 - 1 for null terminator)
    REQUIRE(state.searchCriteria.filename_input.AsString().length() == kMaxChars);
    REQUIRE(state.searchCriteria.filename_input.AsString()[kMaxChars - 1U] == 'a');
    REQUIRE(state.searchCriteria.filename_input.Data()[kMaxChars] == '\0');
  }

  TEST_CASE("Input field assignment - maximum length string") {
    GuiState state;

    constexpr std::size_t kBufferLength = 256;
    constexpr std::size_t kMaxChars = kBufferLength - 1U;

    // Create a string exactly at max length (255 chars + null terminator = 256)
    const std::string max_string(kMaxChars, 'a');
    SetInputField(state.searchCriteria.filename_input, max_string);

    REQUIRE(state.searchCriteria.filename_input.AsString().length() == kMaxChars);
    REQUIRE(!IsInputFieldEmpty(state.searchCriteria.filename_input));
  }

  TEST_CASE("Input field assignment - empty string") {
    GuiState state;

    SetInputField(state.searchCriteria.filename_input, "test");
    REQUIRE(!IsInputFieldEmpty(state.searchCriteria.filename_input));

    SetInputField(state.searchCriteria.filename_input, "");
    REQUIRE(IsInputFieldEmpty(state.searchCriteria.filename_input));
  }

  TEST_CASE("Input field direct assignment (clear with Clear())") {
    GuiState state;

    SetInputField(state.searchCriteria.filename_input, "test");
    REQUIRE(!IsInputFieldEmpty(state.searchCriteria.filename_input));

    // Use Clear() method
    state.searchCriteria.filename_input.Clear();
    REQUIRE(IsInputFieldEmpty(state.searchCriteria.filename_input));
  }

  TEST_CASE("Input field - special characters") {
    GuiState state;

    std::string special = "test/file\\path:name*?";
    SetInputField(state.searchCriteria.path_input, special);

    REQUIRE(state.searchCriteria.path_input.AsString() == special);
  }

  TEST_CASE("Input field - conversion to std::string") {
    GuiState state;

    SetInputField(state.searchCriteria.filename_input, "test");

    // Implicit conversion (as used in BuildSearchParams)
    std::string str = state.searchCriteria.filename_input;
    REQUIRE(str == "test");
  }

}

TEST_SUITE("GuiState::BuildCurrentSearchParams") {

  TEST_CASE("BuildCurrentSearchParams - all fields populated") {
    GuiState state;
    state.searchCriteria.filename_input.SetValue("test");
    state.searchCriteria.extension_input.SetValue("txt");
    state.searchCriteria.path_input.SetValue("/path");
    state.searchCriteria.folders_only = true;
    state.searchCriteria.case_sensitive = true;

    SearchParams params = state.BuildCurrentSearchParams();

    REQUIRE(params.filenameInput == "test");
    REQUIRE(params.extensionInput == "txt");
    REQUIRE(params.pathInput == "/path");
    REQUIRE(params.foldersOnly == true);
    REQUIRE(params.caseSensitive == true);
  }

  TEST_CASE("BuildCurrentSearchParams - empty fields") {
    GuiState state;
    // All fields empty by default

    SearchParams params = state.BuildCurrentSearchParams();

    REQUIRE(params.filenameInput.empty());
    REQUIRE(params.extensionInput.empty());
    REQUIRE(params.pathInput.empty());
    REQUIRE(params.foldersOnly == false);
    REQUIRE(params.caseSensitive == false);
  }

  TEST_CASE("BuildCurrentSearchParams - path only (path and filename stay separate)") {
    GuiState state;
    state.searchCriteria.path_input.SetValue("/path/to/search");
    // filenameInput and extensionInput empty

    SearchParams params = state.BuildCurrentSearchParams();

    REQUIRE(params.filenameInput.empty());
    REQUIRE(params.pathInput == "/path/to/search");
    REQUIRE(params.extensionInput.empty());
  }

  TEST_CASE("BuildCurrentSearchParams - filename only") {
    GuiState state;
    state.searchCriteria.filename_input.SetValue("test");
    // extensionInput and pathInput empty

    SearchParams params = state.BuildCurrentSearchParams();

    REQUIRE(params.filenameInput == "test");
    REQUIRE(params.extensionInput.empty());
    REQUIRE(params.pathInput.empty());
  }

  TEST_CASE("BuildCurrentSearchParams - extension only") {
    GuiState state;
    state.searchCriteria.extension_input.SetValue("txt");
    // filenameInput and pathInput empty

    SearchParams params = state.BuildCurrentSearchParams();

    REQUIRE(params.filenameInput.empty());
    REQUIRE(params.extensionInput == "txt");
    REQUIRE(params.pathInput.empty());
  }

  TEST_CASE("BuildCurrentSearchParams - long strings preserved") {
    GuiState state;
    std::string long_filename(255, 'a');
    state.searchCriteria.filename_input.SetValue(long_filename);

    SearchParams params = state.BuildCurrentSearchParams();

    REQUIRE(params.filenameInput.length() == 255);
    REQUIRE(params.filenameInput == long_filename);
  }

  TEST_CASE("BuildCurrentSearchParams - special characters preserved") {
    GuiState state;
    std::string special = "test/file\\path:name*?";
    state.searchCriteria.filename_input.SetValue(special);

    SearchParams params = state.BuildCurrentSearchParams();

    REQUIRE(params.filenameInput == special);
  }

}

TEST_SUITE("GuiState Input Field Operations") {

  TEST_CASE("MarkInputChanged - sets input_debounce.input_changed flag") {
    GuiState state;
    REQUIRE(state.input_debounce.input_changed == false);

    // Bracket MarkInputChanged with two now() samples. Do not assert "elapsed since
    // input_debounce.last_input_time" using a third now() — on slow/heavily loaded Windows CI the gap
    // between adjacent statements can exceed 1s (first chrono/DLL use, scheduling),
    // which falsely fails tests that use a wall-clock threshold.
    const auto before = std::chrono::steady_clock::now();
    state.MarkInputChanged();
    const auto after = std::chrono::steady_clock::now();

    REQUIRE(state.input_debounce.input_changed == true);
    REQUIRE(state.input_debounce.last_input_time >= before);
    REQUIRE(state.input_debounce.last_input_time <= after);
  }

  TEST_CASE("Input field - IsEmpty check") {
    GuiState state;

    REQUIRE(state.searchCriteria.filename_input.IsEmpty());

    SetInputField(state.searchCriteria.filename_input, "test");
    REQUIRE(!state.searchCriteria.filename_input.IsEmpty());
    REQUIRE(state.searchCriteria.filename_input.AsString().length() == 4);
  }

  TEST_CASE("Input field - comparison with default extensions") {
    GuiState state;
    constexpr const char* kDefaultExtensions = "txt;cpp;h;hpp;c;cc;cxx";

    // Empty should not match
    REQUIRE(state.searchCriteria.extension_input.AsString() != kDefaultExtensions);

    // Set to default
    SetInputField(state.searchCriteria.extension_input, kDefaultExtensions);
    REQUIRE(state.searchCriteria.extension_input.AsString() == kDefaultExtensions);
  }

  TEST_CASE("Input field - buffer size verification") {
    GuiState state;

    // Verify max length is 256
    REQUIRE(SearchInputField::MaxLength() == 256);
    REQUIRE(state.searchCriteria.extension_input.MaxLength() == 256);
    REQUIRE(state.searchCriteria.filename_input.MaxLength() == 256);
    REQUIRE(state.searchCriteria.path_input.MaxLength() == 256);
  }

  TEST_CASE("Input field - null termination after operations") {
    GuiState state;

    // Set value
    SetInputField(state.searchCriteria.filename_input, "test");

    // Verify null termination and length (no strlen - Sonar flags unsafe C string use)
    REQUIRE(state.searchCriteria.filename_input.Data()[4] == '\0');
    REQUIRE(state.searchCriteria.filename_input.AsString().length() == 4);
  }

}

TEST_SUITE("GuiState::ApplyShowAllPreset") {

  TEST_CASE("clears filename, extension and sets path to pp:**") {
    GuiState state;
    state.searchCriteria.filename_input.SetValue("report");
    state.searchCriteria.extension_input.SetValue("pdf");

    state.ApplyShowAllPreset();

    CHECK(state.searchCriteria.filename_input.IsEmpty());
    CHECK(state.searchCriteria.extension_input.IsEmpty());
    CHECK(state.searchCriteria.path_input.AsString() == "pp:**");
  }

  TEST_CASE("resets filters to None") {
    GuiState state;
    state.searchCriteria.time_filter = TimeFilter::Today;
    state.searchCriteria.size_filter = SizeFilter::Large;
    state.searchCriteria.folders_only = true;
    state.searchCriteria.case_sensitive = true;

    state.ApplyShowAllPreset();

    CHECK(state.searchCriteria.time_filter == TimeFilter::None);
    CHECK(state.searchCriteria.size_filter == SizeFilter::None);
    CHECK(!state.searchCriteria.folders_only);
    CHECK(!state.searchCriteria.case_sensitive);
  }

  TEST_CASE("marks input_debounce.input_changed") {
    GuiState state;
    state.input_debounce.input_changed = false;

    state.ApplyShowAllPreset();

    CHECK(state.input_debounce.input_changed);
  }
}

TEST_SUITE("GuiState Result Pool Remapping") {

  TEST_CASE("RemapSelectionAfterDisplayResultsChange - remaps selected indices correctly") {
    GuiState state;

    // Create old results
    std::vector<SearchResult> old_results;
    SearchResult r1; r1.fullPath = "path1"; r1.fileId = 1;
    SearchResult r2; r2.fullPath = "path2"; r2.fileId = 2;
    SearchResult r3; r3.fullPath = "path3"; r3.fileId = 3;
    old_results.push_back(r1);
    old_results.push_back(r2);
    old_results.push_back(r3);

    // Select middle item
    state.selection.SetSelectedRow(1); // "path2"
    REQUIRE(state.selection.GetSelectedRow() == 1);
    REQUIRE(state.selection.IsRowSelected(1));

    // Create new results with items in different order
    std::vector<SearchResult> new_results;
    new_results.push_back(r3);
    new_results.push_back(r2);
    new_results.push_back(r1);

    // Remap (old order passed as path views — no deep copy)
    const std::vector<std::string_view> old_paths = SnapshotDisplayPaths(old_results);
    state.selection.RemapSelectionAfterDisplayResultsChange(old_paths, new_results);

    // "path2" is now at index 1 in new_results too (by chance), but let's check it's correct
    CHECK(state.selection.GetSelectedRow() == 1);
    CHECK(state.selection.IsRowSelected(1));

    // Select multiple items
    state.selection.ClearSelection();
    state.selection.SelectRow(0); // "path1"
    state.selection.SelectRow(2); // "path3"

    // Remap again
    state.selection.RemapSelectionAfterDisplayResultsChange(old_paths, new_results);

    // In new_results: "path1" is at index 2, "path3" is at index 0
    CHECK(state.selection.IsRowSelected(0));
    CHECK(state.selection.IsRowSelected(2));
    CHECK(state.selection.GetSelectedRows().size() == 2);
  }

}

TEST_SUITE("GuiState::ApplySearchConfig") {

  TEST_CASE("applies filename from config") {
    GuiState state;
    gemini_api_utils::SearchConfig config;
    config.filename = "report*.pdf";

    state.ApplySearchConfig(config);

    CHECK(state.searchCriteria.filename_input.AsString() == "report*.pdf");
  }

  TEST_CASE("empty filename in config leaves existing field unchanged") {
    GuiState state;
    state.searchCriteria.filename_input.SetValue("existing");
    gemini_api_utils::SearchConfig config;  // filename empty

    state.ApplySearchConfig(config);

    CHECK(state.searchCriteria.filename_input.AsString() == "existing");
  }

  TEST_CASE("applies multiple extensions as semicolon-separated string") {
    GuiState state;
    gemini_api_utils::SearchConfig config;
    config.extensions = {"pdf", "doc", "docx"};

    state.ApplySearchConfig(config);

    CHECK(state.searchCriteria.extension_input.AsString() == "pdf;doc;docx");
  }

  TEST_CASE("applies single extension without trailing semicolon") {
    GuiState state;
    gemini_api_utils::SearchConfig config;
    config.extensions = {"txt"};

    state.ApplySearchConfig(config);

    CHECK(state.searchCriteria.extension_input.AsString() == "txt");
  }

  TEST_CASE("applies path from config") {
    GuiState state;
    gemini_api_utils::SearchConfig config;
    config.path = "/home/user/docs";

    state.ApplySearchConfig(config);

    CHECK(state.searchCriteria.path_input.AsString() == "/home/user/docs");
  }

  TEST_CASE("applies foldersOnly and caseSensitive flags") {
    GuiState state;
    gemini_api_utils::SearchConfig config;
    config.folders_only = true;
    config.case_sensitive = true;

    state.ApplySearchConfig(config);

    CHECK(state.searchCriteria.folders_only);
    CHECK(state.searchCriteria.case_sensitive);
  }

  TEST_CASE("applies known time filter strings") {
    GuiState state;
    gemini_api_utils::SearchConfig config;

    config.time_filter = "Today";
    state.ApplySearchConfig(config);
    CHECK(state.searchCriteria.time_filter == TimeFilter::Today);

    config.time_filter = "ThisWeek";
    state.ApplySearchConfig(config);
    CHECK(state.searchCriteria.time_filter == TimeFilter::ThisWeek);

    config.time_filter = "ThisMonth";
    state.ApplySearchConfig(config);
    CHECK(state.searchCriteria.time_filter == TimeFilter::ThisMonth);

    config.time_filter = "ThisYear";
    state.ApplySearchConfig(config);
    CHECK(state.searchCriteria.time_filter == TimeFilter::ThisYear);

    config.time_filter = "Older";
    state.ApplySearchConfig(config);
    CHECK(state.searchCriteria.time_filter == TimeFilter::Older);
  }

  TEST_CASE("unknown or empty time filter defaults to None") {
    GuiState state;
    state.searchCriteria.time_filter = TimeFilter::Today;

    gemini_api_utils::SearchConfig config;
    config.time_filter = "InvalidValue";
    state.ApplySearchConfig(config);
    CHECK(state.searchCriteria.time_filter == TimeFilter::None);

    state.searchCriteria.time_filter = TimeFilter::Today;
    config.time_filter = "";
    state.ApplySearchConfig(config);
    CHECK(state.searchCriteria.time_filter == TimeFilter::None);
  }

  TEST_CASE("applies known size filter strings") {
    GuiState state;
    gemini_api_utils::SearchConfig config;

    config.size_filter = "Large";
    state.ApplySearchConfig(config);
    CHECK(state.searchCriteria.size_filter == SizeFilter::Large);

    config.size_filter = "Tiny";
    state.ApplySearchConfig(config);
    CHECK(state.searchCriteria.size_filter == SizeFilter::Tiny);

    config.size_filter = "Massive";
    state.ApplySearchConfig(config);
    CHECK(state.searchCriteria.size_filter == SizeFilter::Massive);
  }

  TEST_CASE("marks input_debounce.input_changed after applying config") {
    GuiState state;
    state.input_debounce.input_changed = false;
    gemini_api_utils::SearchConfig config;
    config.filename = "test";

    state.ApplySearchConfig(config);

    CHECK(state.input_debounce.input_changed);
  }
}

TEST_SUITE("BeginInFlightSearchKeepingVisibleResults") {

  TEST_CASE("preserves result pool and sets in-flight flags") {
    GuiState state;
    state.result_pool_->Pool().assign({'/', 't', 'm', 'p', '/', 'a', '\0'});
    SearchResult stub;
    stub.fileId = 1;
    stub.isDirectory = false;
    stub.fileSize = kFileSizeNotLoaded;
    stub.fullPath = std::string_view(state.result_pool_->Pool().data());
    state.result_pool_->Results().push_back(stub);
    state.input_debounce.input_changed = true;
    state.search_pipeline.search_active = false;
    state.search_pipeline.results_complete = true;
    state.search_pipeline.search_was_manual = true;

    const size_t size_before = state.result_pool_->Results().size();
    search_controller_detail::BeginInFlightSearchKeepingVisibleResults(state);

    CHECK(state.result_pool_->Results().size() == size_before);
    CHECK_FALSE(state.result_pool_->Results().empty());
    CHECK(state.search_pipeline.search_active);
    CHECK_FALSE(state.search_pipeline.results_complete);
    CHECK_FALSE(state.search_pipeline.search_was_manual);
    CHECK_FALSE(state.input_debounce.input_changed);
    CHECK(state.async_sort_.sort_ready_state_ == SortReadyState::Idle);
  }

  TEST_CASE("works with empty pool for first instant search") {
    GuiState state;
    REQUIRE(state.result_pool_->Results().empty());

    search_controller_detail::BeginInFlightSearchKeepingVisibleResults(state);

    CHECK(state.result_pool_->Results().empty());
    CHECK(state.search_pipeline.search_active);
    CHECK_FALSE(state.search_pipeline.results_complete);
  }
}

TEST_SUITE("GuiState Export CSV Popup State") {
  TEST_CASE("Default construction - export popup state is initialized") {
    GuiState state;

    CHECK_FALSE(state.export_workflow.show_popup);
    CHECK_FALSE(state.export_workflow.success);
    CHECK(state.export_workflow.file_path.empty());
    CHECK(state.export_workflow.result_count == 0);
  }

  TEST_CASE("ClearInputs preserves export popup fields and clears status notification") {
    GuiState state;

    state.export_workflow.show_popup = true;
    state.export_workflow.success = true;
    state.export_workflow.file_path = "/path/to/results.csv";
    state.export_workflow.result_count = 123;
    state.export_workflow.notification = "Exported 123 results";
    state.export_workflow.error_message = "should stay for open modal";

    state.ClearInputs();

    CHECK(state.export_workflow.show_popup);
    CHECK(state.export_workflow.success);
    CHECK(state.export_workflow.file_path == "/path/to/results.csv");
    CHECK(state.export_workflow.result_count == 123);
    CHECK(state.export_workflow.notification.empty());
    CHECK(state.export_workflow.error_message == "should stay for open modal");
  }
}

TEST_SUITE("SearchCriteria ItemTypeFilter") {
  TEST_CASE("Default is All items") {
    SearchCriteria criteria;
    CHECK(criteria.item_type_filter == ItemTypeFilter::All);
    CHECK_FALSE(criteria.FoldersOnly());
    CHECK_FALSE(criteria.FilesOnly());
  }

  TEST_CASE("SetItemTypeFilter to FilesOnly") {
    SearchCriteria criteria;
    criteria.SetItemTypeFilter(ItemTypeFilter::FilesOnly);
    CHECK(criteria.item_type_filter == ItemTypeFilter::FilesOnly);
    CHECK(criteria.FilesOnly());
    CHECK_FALSE(criteria.FoldersOnly());
    CHECK_FALSE(criteria.folders_only);

    const SearchParams params = criteria.BuildParams();
    CHECK(params.itemTypeFilter == ItemTypeFilter::FilesOnly);
    CHECK_FALSE(params.foldersOnly);
  }

  TEST_CASE("SetItemTypeFilter to FoldersOnly") {
    SearchCriteria criteria;
    criteria.SetItemTypeFilter(ItemTypeFilter::FoldersOnly);
    CHECK(criteria.item_type_filter == ItemTypeFilter::FoldersOnly);
    CHECK(criteria.FoldersOnly());
    CHECK_FALSE(criteria.FilesOnly());
    CHECK(criteria.folders_only);

    const SearchParams params = criteria.BuildParams();
    CHECK(params.itemTypeFilter == ItemTypeFilter::FoldersOnly);
    CHECK(params.foldersOnly);
  }

  TEST_CASE("ApplySearchConfig with files_only") {
    GuiState state;
    gemini_api_utils::SearchConfig config;
    config.files_only = true;

    state.ApplySearchConfig(config);
    CHECK(state.searchCriteria.FilesOnly());
    CHECK(state.searchCriteria.item_type_filter == ItemTypeFilter::FilesOnly);
    CHECK_FALSE(state.searchCriteria.FoldersOnly());
  }

  TEST_CASE("ApplyHistoryEntryToGuiState restores FilesOnly") {
    SearchHistoryEntry entry;
    entry.params.item_type = 1;

    GuiState state;
    ApplyHistoryEntryToGuiState(entry, state);
    CHECK(state.searchCriteria.FilesOnly());
    CHECK(state.searchCriteria.item_type_filter == ItemTypeFilter::FilesOnly);
  }

  TEST_CASE("ApplyHistoryEntryToGuiState clamps invalid item_type to All") {
    SearchHistoryEntry entry;
    entry.params.item_type = 99; // Corrupted / hand-edited

    GuiState state;
    ApplyHistoryEntryToGuiState(entry, state);
    CHECK_FALSE(state.searchCriteria.FilesOnly());
    CHECK_FALSE(state.searchCriteria.FoldersOnly());
    CHECK(state.searchCriteria.item_type_filter == ItemTypeFilter::All);
  }

  TEST_CASE("EffectiveItemTypeFilter resolves legacy folders_only fallback") {
    SearchCriteria criteria;
    criteria.item_type_filter = ItemTypeFilter::All;
    criteria.folders_only = true; // Stale or legacy state

    CHECK(criteria.FoldersOnly());
    CHECK_FALSE(criteria.FilesOnly());
    CHECK(criteria.GetEffectiveItemTypeFilter() == ItemTypeFilter::FoldersOnly);

    const SearchParams params = criteria.BuildParams();
    CHECK(params.itemTypeFilter == ItemTypeFilter::FoldersOnly);
    CHECK(params.foldersOnly);
  }

  TEST_CASE("Explicit FilesOnly selection overrides stale folders_only flag") {
    SearchCriteria criteria;
    criteria.item_type_filter = ItemTypeFilter::FilesOnly;
    criteria.folders_only = true; // Stale flag

    CHECK(criteria.FilesOnly());
    CHECK_FALSE(criteria.FoldersOnly());
    CHECK(criteria.GetEffectiveItemTypeFilter() == ItemTypeFilter::FilesOnly);

    const SearchParams params = criteria.BuildParams();
    CHECK(params.itemTypeFilter == ItemTypeFilter::FilesOnly);
    CHECK_FALSE(params.foldersOnly);
  }
}


