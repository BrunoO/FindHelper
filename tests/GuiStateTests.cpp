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
 *
 * These tests ensure safe refactoring from char[256] arrays to SearchInputField class.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "gui/GuiState.h"
#include "search/SearchWorker.h"
#include "utils/StringUtils.h"
#include <string>

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

    REQUIRE(IsInputFieldEmpty(state.extensionInput));
    REQUIRE(IsInputFieldEmpty(state.filenameInput));
    REQUIRE(IsInputFieldEmpty(state.pathInput));
    REQUIRE(state.extensionInput.AsString().empty());
    REQUIRE(state.filenameInput.AsString().empty());
    REQUIRE(state.pathInput.AsString().empty());
  }

  TEST_CASE("ClearInputs - clears all input fields") {
    GuiState state;

    // Set some values
    SetInputField(state.extensionInput, "txt;cpp");
    SetInputField(state.filenameInput, "test");
    SetInputField(state.pathInput, "/path/to/file");
    state.foldersOnly = true;
    state.caseSensitive = true;

    // Verify they're set
    REQUIRE(!IsInputFieldEmpty(state.extensionInput));
    REQUIRE(!IsInputFieldEmpty(state.filenameInput));
    REQUIRE(!IsInputFieldEmpty(state.pathInput));

    // Clear
    state.ClearInputs();

    // Verify they're cleared
    REQUIRE(IsInputFieldEmpty(state.extensionInput));
    REQUIRE(IsInputFieldEmpty(state.filenameInput));
    REQUIRE(IsInputFieldEmpty(state.pathInput));
    REQUIRE(state.foldersOnly == false);
    REQUIRE(state.caseSensitive == false);
  }

  TEST_CASE("Input field assignment - normal strings") {
    GuiState state;

    SetInputField(state.extensionInput, "txt");
    SetInputField(state.filenameInput, "test");
    SetInputField(state.pathInput, "/path");

    REQUIRE(state.extensionInput.AsString() == "txt");
    REQUIRE(state.filenameInput.AsString() == "test");
    REQUIRE(state.pathInput.AsString() == "/path");
  }

  TEST_CASE("Input field assignment - long strings (truncation)") {
    GuiState state;

    constexpr std::size_t kBufferLength = 256;
    constexpr std::size_t kMaxChars = kBufferLength - 1U;
    constexpr std::size_t kOverlongChars = kMaxChars + 45U;

    // Create a string longer than 256 characters
    const std::string long_string(kOverlongChars, 'a');
    SetInputField(state.filenameInput, long_string);

    // Should be truncated to 255 characters (256 - 1 for null terminator)
    REQUIRE(state.filenameInput.AsString().length() == kMaxChars);
    REQUIRE(state.filenameInput.AsString()[kMaxChars - 1U] == 'a');
    REQUIRE(state.filenameInput.Data()[kMaxChars] == '\0');
  }

  TEST_CASE("Input field assignment - maximum length string") {
    GuiState state;

    constexpr std::size_t kBufferLength = 256;
    constexpr std::size_t kMaxChars = kBufferLength - 1U;

    // Create a string exactly at max length (255 chars + null terminator = 256)
    const std::string max_string(kMaxChars, 'a');
    SetInputField(state.filenameInput, max_string);

    REQUIRE(state.filenameInput.AsString().length() == kMaxChars);
    REQUIRE(!IsInputFieldEmpty(state.filenameInput));
  }

  TEST_CASE("Input field assignment - empty string") {
    GuiState state;

    SetInputField(state.filenameInput, "test");
    REQUIRE(!IsInputFieldEmpty(state.filenameInput));

    SetInputField(state.filenameInput, "");
    REQUIRE(IsInputFieldEmpty(state.filenameInput));
  }

  TEST_CASE("Input field direct assignment (clear with Clear())") {
    GuiState state;

    SetInputField(state.filenameInput, "test");
    REQUIRE(!IsInputFieldEmpty(state.filenameInput));

    // Use Clear() method
    state.filenameInput.Clear();
    REQUIRE(IsInputFieldEmpty(state.filenameInput));
  }

  TEST_CASE("Input field - special characters") {
    GuiState state;

    std::string special = "test/file\\path:name*?";
    SetInputField(state.pathInput, special);

    REQUIRE(state.pathInput.AsString() == special);
  }

  TEST_CASE("Input field - conversion to std::string") {
    GuiState state;

    SetInputField(state.filenameInput, "test");

    // Implicit conversion (as used in BuildSearchParams)
    std::string str = state.filenameInput;
    REQUIRE(str == "test");
  }

}

TEST_SUITE("GuiState::BuildCurrentSearchParams") {

  TEST_CASE("BuildCurrentSearchParams - all fields populated") {
    GuiState state;
    state.filenameInput.SetValue("test");
    state.extensionInput.SetValue("txt");
    state.pathInput.SetValue("/path");
    state.foldersOnly = true;
    state.caseSensitive = true;

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
    state.pathInput.SetValue("/path/to/search");
    // filenameInput and extensionInput empty

    SearchParams params = state.BuildCurrentSearchParams();

    REQUIRE(params.filenameInput.empty());
    REQUIRE(params.pathInput == "/path/to/search");
    REQUIRE(params.extensionInput.empty());
  }

  TEST_CASE("BuildCurrentSearchParams - filename only") {
    GuiState state;
    state.filenameInput.SetValue("test");
    // extensionInput and pathInput empty

    SearchParams params = state.BuildCurrentSearchParams();

    REQUIRE(params.filenameInput == "test");
    REQUIRE(params.extensionInput.empty());
    REQUIRE(params.pathInput.empty());
  }

  TEST_CASE("BuildCurrentSearchParams - extension only") {
    GuiState state;
    state.extensionInput.SetValue("txt");
    // filenameInput and pathInput empty

    SearchParams params = state.BuildCurrentSearchParams();

    REQUIRE(params.filenameInput.empty());
    REQUIRE(params.extensionInput == "txt");
    REQUIRE(params.pathInput.empty());
  }

  TEST_CASE("BuildCurrentSearchParams - long strings preserved") {
    GuiState state;
    std::string long_filename(255, 'a');
    state.filenameInput.SetValue(long_filename);

    SearchParams params = state.BuildCurrentSearchParams();

    REQUIRE(params.filenameInput.length() == 255);
    REQUIRE(params.filenameInput == long_filename);
  }

  TEST_CASE("BuildCurrentSearchParams - special characters preserved") {
    GuiState state;
    std::string special = "test/file\\path:name*?";
    state.filenameInput.SetValue(special);

    SearchParams params = state.BuildCurrentSearchParams();

    REQUIRE(params.filenameInput == special);
  }

}

TEST_SUITE("GuiState Input Field Operations") {

  TEST_CASE("MarkInputChanged - sets inputChanged flag") {
    GuiState state;
    REQUIRE(state.inputChanged == false);

    state.MarkInputChanged();

    REQUIRE(state.inputChanged == true);
    // lastInputTime should be updated (we can't test exact time, but should be recent)
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - state.lastInputTime).count();
    constexpr long long kMaxElapsedMs = 1000LL;
    REQUIRE(elapsed < kMaxElapsedMs); // Should be less than 1 second
  }

  TEST_CASE("Input field - IsEmpty check") {
    GuiState state;

    REQUIRE(state.filenameInput.IsEmpty());

    SetInputField(state.filenameInput, "test");
    REQUIRE(!state.filenameInput.IsEmpty());
    REQUIRE(state.filenameInput.AsString().length() == 4);
  }

  TEST_CASE("Input field - comparison with default extensions") {
    GuiState state;
    const char* kDefaultExtensions = "txt;cpp;h;hpp;c;cc;cxx";

    // Empty should not match
    REQUIRE(state.extensionInput.AsString() != kDefaultExtensions);

    // Set to default
    SetInputField(state.extensionInput, kDefaultExtensions);
    REQUIRE(state.extensionInput.AsString() == kDefaultExtensions);
  }

  TEST_CASE("Input field - buffer size verification") {
    GuiState state;

    // Verify max length is 256
    REQUIRE(SearchInputField::MaxLength() == 256);
    REQUIRE(state.extensionInput.MaxLength() == 256);
    REQUIRE(state.filenameInput.MaxLength() == 256);
    REQUIRE(state.pathInput.MaxLength() == 256);
  }

  TEST_CASE("Input field - null termination after operations") {
    GuiState state;

    // Set value
    SetInputField(state.filenameInput, "test");

    // Verify null termination and length (no strlen - Sonar flags unsafe C string use)
    REQUIRE(state.filenameInput.Data()[4] == '\0');
    REQUIRE(state.filenameInput.AsString().length() == 4);
  }

  TEST_CASE("ClearInputs - invalidates folder stats cache") {
    GuiState state;

    // Simulate a populated folder-stats cache
    state.folderStatsByPath["some/path"] = GuiState::FolderStats{3, 1024};
    state.folderStatsValid = true;
    state.folderStatsResultsBatchNumber = 5;

    state.ClearInputs();

    // Folder stats must be cleared so the next render does not display stale data
    REQUIRE(state.folderStatsValid == false);
    REQUIRE(state.folderStatsByPath.empty());
  }

}

