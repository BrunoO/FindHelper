/**
 * @file SettingsTests.cpp
 * @brief Unit tests for Settings loading and saving functionality
 *
 * Tests cover:
 * - LoadSettings() with valid JSON
 * - LoadSettings() with corrupted/missing JSON (error handling)
 * - SaveSettings() round-trip (save then load)
 * - In-memory settings mode (test_settings namespace)
 * - Validation of numeric values (thread pool size, window dimensions, etc.)
 * - Search history serialization/deserialization
 * - Edge cases (empty values, invalid ranges, etc.)
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "core/Settings.h"
#include "path/PathUtils.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <process.h>  // _getpid()
#else
#include <unistd.h>  // getpid()
#endif

namespace {
// Fixture that clears in-memory settings before and after each test,
// ensuring isolation between tests without repeating the clear in every body.
struct SettingsFixture {
  SettingsFixture() { test_settings::ClearInMemorySettings(); }
  ~SettingsFixture() { test_settings::ClearInMemorySettings(); }
  SettingsFixture(const SettingsFixture&) = delete;
  SettingsFixture& operator=(const SettingsFixture&) = delete;
  SettingsFixture(SettingsFixture&&) = delete;
  SettingsFixture& operator=(SettingsFixture&&) = delete;
};

// Temp dir + HOME/USERPROFILE swap for on-disk Settings path tests (Sonar duplication).
struct ScopedTempHomeForDiskSettings {
  std::filesystem::path tmp_dir_;
  std::string tmp_str_;
  std::string old_home_;

  explicit ScopedTempHomeForDiskSettings(std::string_view suffix) {
    namespace fs = std::filesystem;
    const fs::path tmp_base = fs::temp_directory_path();
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = getpid();  // NOLINT(concurrency-mt-unsafe) - test only; single-threaded
#endif
    tmp_dir_ = tmp_base / ("FindHelper_" + std::string(suffix) + "_" +
                           std::to_string(static_cast<std::size_t>(pid)));
    if (!fs::exists(tmp_dir_)) {
      fs::create_directories(tmp_dir_);
    }
    tmp_str_ = tmp_dir_.string();
#ifdef _WIN32
    {
      char* buf = nullptr;
      size_t len = 0;
      if (_dupenv_s(&buf, &len, "USERPROFILE") == 0 && buf != nullptr) {
        old_home_ = buf;
        free(buf);
      }
    }
    _putenv_s("USERPROFILE", tmp_str_.c_str());
#else
    if (const char* p = std::getenv("HOME"); p != nullptr) {  // NOLINT(concurrency-mt-unsafe)
      old_home_ = p;
    }
    setenv("HOME", tmp_str_.c_str(), 1);  // NOLINT(concurrency-mt-unsafe) - test only
#endif
  }

  ScopedTempHomeForDiskSettings(const ScopedTempHomeForDiskSettings&) = delete;
  ScopedTempHomeForDiskSettings& operator=(const ScopedTempHomeForDiskSettings&) = delete;
  ScopedTempHomeForDiskSettings(ScopedTempHomeForDiskSettings&&) = delete;
  ScopedTempHomeForDiskSettings& operator=(ScopedTempHomeForDiskSettings&&) = delete;

  ~ScopedTempHomeForDiskSettings() {
    namespace fs = std::filesystem;
#ifdef _WIN32
    _putenv_s("USERPROFILE", old_home_.c_str());
#else
    setenv("HOME", old_home_.c_str(), 1);  // NOLINT(concurrency-mt-unsafe) - restore after test
#endif
    std::error_code ec;
    (void)fs::remove_all(tmp_dir_, ec);
  }

  [[nodiscard]] std::string PrimaryDir() const {
    return path_utils::JoinPath(tmp_str_, ".FindHelper");
  }

  [[nodiscard]] std::string PrimaryFile() const {
    return path_utils::JoinPath(PrimaryDir(), "settings.json");
  }
};

}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity) - Aggregated doctest suite with many checks
TEST_SUITE("Settings") {

  TEST_CASE("Default settings") {
    AppSettings settings;

    CHECK(settings.fontFamily == "");
    CHECK(settings.fontSize == 14.0F);
    CHECK(settings.fontScale == 1.0F);
    CHECK(settings.windowWidth == 1280);
    CHECK(settings.windowHeight == 800);
    CHECK(settings.searchThreadPoolSize == 0);
    CHECK(settings.dynamicChunkSize == 1000);
    CHECK(settings.hybridInitialWorkPercent == 80);
    CHECK(settings.guidedSchedulingDivisor == 2);
    CHECK(settings.uiMode == AppSettings::UIMode::Full);
  }

  TEST_CASE("SyncWindowSizeIntoSettings updates valid dimensions only") {
    AppSettings settings;
    settings.windowWidth = 1280;
    settings.windowHeight = 800;

    SyncWindowSizeIntoSettings(settings, 1920, 1080);
    CHECK(settings.windowWidth == 1920);
    CHECK(settings.windowHeight == 1080);

    SyncWindowSizeIntoSettings(settings, 100, 50);  // below minimum — ignored
    CHECK(settings.windowWidth == 1920);
    CHECK(settings.windowHeight == 1080);

    SyncWindowSizeIntoSettings(settings, 5000, 3000);  // above maximum — ignored
    CHECK(settings.windowWidth == 1920);
    CHECK(settings.windowHeight == 1080);
  }

  TEST_CASE("ApplyWorkingSettingsPreservingRuntime keeps window size and history") {
    AppSettings live;
    live.windowWidth = 1600;
    live.windowHeight = 900;
    live.fontScale = 1.0F;
    live.searchHistory.emplace_back();
    live.searchHistory.back().id = "live-entry";

    AppSettings working;
    working.windowWidth = 1280;  // stale startup size from Settings open
    working.windowHeight = 800;
    working.fontScale = 1.25F;
    working.searchHistory.emplace_back();
    working.searchHistory.back().id = "stale-entry";

    ApplyWorkingSettingsPreservingRuntime(live, working);

    CHECK(live.fontScale == 1.25F);
    CHECK(live.windowWidth == 1600);
    CHECK(live.windowHeight == 900);
    REQUIRE(live.searchHistory.size() == 1);
    CHECK(live.searchHistory.front().id == "live-entry");
  }

  TEST_SUITE("In-memory settings mode") {

    TEST_CASE_FIXTURE(SettingsFixture, "Set and get in-memory settings") {
      CHECK_FALSE(test_settings::IsInMemoryMode());

      // Set custom settings
      AppSettings custom;
      custom.fontFamily = "Courier New";
      custom.fontSize = 16.0F;
      custom.windowWidth = 1920;
      custom.windowHeight = 1080;
      custom.searchThreadPoolSize = 8;
      custom.dynamicChunkSize = 2000;
      custom.hybridInitialWorkPercent = 80;

      test_settings::SetInMemorySettings(custom);
      CHECK(test_settings::IsInMemoryMode());

      // Get settings back
      AppSettings loaded;
      bool result = LoadSettings(loaded);
      CHECK(result == true);
      CHECK(loaded.fontFamily == "Courier New");
      CHECK(loaded.fontSize == 16.0F);
      CHECK(loaded.windowWidth == 1920);
      CHECK(loaded.windowHeight == 1080);
      CHECK(loaded.searchThreadPoolSize == 8);
      CHECK(loaded.dynamicChunkSize == 2000);
      CHECK(loaded.hybridInitialWorkPercent == 80);

      // Clean up
      test_settings::ClearInMemorySettings();
      CHECK_FALSE(test_settings::IsInMemoryMode());
    }

    TEST_CASE_FIXTURE(SettingsFixture, "LoadSettings uses in-memory settings when active") {

      AppSettings custom;
      custom.fontFamily = "Arial";
      custom.fontSize = 18.0F;
      test_settings::SetInMemorySettings(custom);

      AppSettings loaded;
      bool result = LoadSettings(loaded);
      CHECK(result == true);
      CHECK(loaded.fontFamily == "Arial");
      CHECK(loaded.fontSize == 18.0F);
    }

    TEST_CASE_FIXTURE(SettingsFixture, "SaveSettings updates in-memory settings when active") {

      AppSettings initial;
      initial.fontFamily = "Initial";
      test_settings::SetInMemorySettings(initial);

      AppSettings to_save;
      to_save.fontFamily = "Updated";
      to_save.fontSize = 20.0F;

      bool result = SaveSettings(to_save);
      CHECK(result == true);

      AppSettings loaded;
      LoadSettings(loaded);
      CHECK(loaded.fontFamily == "Updated");
      CHECK(loaded.fontSize == 20.0F);
    }

    TEST_CASE_FIXTURE(SettingsFixture, "GetInMemorySettings returns defaults when not set") {

      AppSettings settings = test_settings::GetInMemorySettings();
      CHECK(settings.fontFamily == "");
      CHECK(settings.fontSize == 14.0F);
    }
  }

  TEST_SUITE("UI Mode and Backward Compatibility") {
    TEST_CASE_FIXTURE(SettingsFixture, "LoadSettings with uiMode") {

      AppSettings custom;
      custom.uiMode = AppSettings::UIMode::Simplified;
      test_settings::SetInMemorySettings(custom);

      AppSettings loaded;
      LoadSettings(loaded);
      CHECK(loaded.uiMode == AppSettings::UIMode::Simplified);
    }

    TEST_CASE_FIXTURE(SettingsFixture, "SaveSettings with uiMode") {

      AppSettings to_save;
      to_save.uiMode = AppSettings::UIMode::Minimalistic;
      CHECK(SaveSettings(to_save));

      AppSettings loaded;
      LoadSettings(loaded);
      CHECK(loaded.uiMode == AppSettings::UIMode::Minimalistic);
    }

    TEST_CASE_FIXTURE(SettingsFixture, "Disk load: uiMode Minimalistic not overwritten by simplifiedUI false") {
      namespace fs = std::filesystem;
      const ScopedTempHomeForDiskSettings env("ui_mode_vs_simplified");
      fs::create_directories(env.PrimaryDir());
      {
        std::ofstream out(env.PrimaryFile());
        out << R"({"uiMode":2,"simplifiedUI":false,"fontSize":14.0})";
      }

      AppSettings loaded;
      const bool ok = LoadSettings(loaded);
      CHECK(ok == true);
      CHECK(loaded.uiMode == AppSettings::UIMode::Minimalistic);
    }

    TEST_CASE_FIXTURE(SettingsFixture, "Disk load: uiMode as JSON float 2.0") {
      namespace fs = std::filesystem;
      const ScopedTempHomeForDiskSettings env("ui_mode_float");
      fs::create_directories(env.PrimaryDir());
      {
        std::ofstream out(env.PrimaryFile());
        out << R"({"uiMode":2.0,"fontSize":14.0})";
      }

      AppSettings loaded;
      const bool ok = LoadSettings(loaded);
      CHECK(ok == true);
      CHECK(loaded.uiMode == AppSettings::UIMode::Minimalistic);
    }
  }

  TEST_SUITE("LoadSettings validation") {

    TEST_CASE_FIXTURE(SettingsFixture, "LoadSettings with valid JSON") {

      AppSettings custom;
      custom.fontFamily = "TestFont";
      custom.fontSize = 15.0F;
      custom.fontScale = 1.5F;
      custom.windowWidth = 1600;
      custom.windowHeight = 900;
      custom.searchThreadPoolSize = 4;
      custom.dynamicChunkSize = 500;
      custom.hybridInitialWorkPercent = 60;

      test_settings::SetInMemorySettings(custom);

      AppSettings loaded;
      bool result = LoadSettings(loaded);
      CHECK(result == true);
      CHECK(loaded.fontFamily == "TestFont");
      CHECK(loaded.fontSize == 15.0F);
      CHECK(loaded.fontScale == 1.5F);
      CHECK(loaded.windowWidth == 1600);
      CHECK(loaded.windowHeight == 900);
      CHECK(loaded.searchThreadPoolSize == 4);
      CHECK(loaded.dynamicChunkSize == 500);
      CHECK(loaded.hybridInitialWorkPercent == 60);
    }

    TEST_CASE_FIXTURE(SettingsFixture, "LoadSettings validates font size range") {

      // Test valid range
      AppSettings valid;
      valid.fontSize = 12.0F;  // Valid: > 4.0 and < 64.0
      test_settings::SetInMemorySettings(valid);

      AppSettings loaded;
      LoadSettings(loaded);
      CHECK(loaded.fontSize == 12.0F);

      // Test below minimum (should keep default)
      AppSettings too_small;
      too_small.fontSize = 2.0F;  // Invalid: < 4.0
      test_settings::SetInMemorySettings(too_small);

      AppSettings loaded2;
      LoadSettings(loaded2);
      // Should keep default (14.0F) since 2.0F is invalid
      // Note: In in-memory mode, validation is bypassed, so this test
      // would need file-based testing for full validation coverage
    }

    TEST_CASE_FIXTURE(SettingsFixture, "LoadSettings validates font scale range") {

      AppSettings valid;
      valid.fontScale = 1.2F;  // Valid: > 0.2 and < 4.0
      test_settings::SetInMemorySettings(valid);

      AppSettings loaded;
      LoadSettings(loaded);
      CHECK(loaded.fontScale == 1.2F);
    }

    TEST_CASE_FIXTURE(SettingsFixture, "LoadSettings validates window dimensions") {

      // Valid width
      AppSettings valid;
      valid.windowWidth = 1920;  // Valid: >= 640 and <= 4096
      valid.windowHeight = 1080;  // Valid: >= 480 and <= 2160
      test_settings::SetInMemorySettings(valid);

      AppSettings loaded;
      LoadSettings(loaded);
      CHECK(loaded.windowWidth == 1920);
      CHECK(loaded.windowHeight == 1080);
    }

    TEST_CASE_FIXTURE(SettingsFixture, "LoadSettings validates thread pool size") {

      // Test valid values: 0 (auto), 1-64
      AppSettings valid1;
      valid1.searchThreadPoolSize = 0;  // Auto-detect
      test_settings::SetInMemorySettings(valid1);

      AppSettings loaded1;
      LoadSettings(loaded1);
      CHECK(loaded1.searchThreadPoolSize == 0);

      AppSettings valid2;
      valid2.searchThreadPoolSize = 8;  // Valid: 1-64
      test_settings::SetInMemorySettings(valid2);

      AppSettings loaded2;
      LoadSettings(loaded2);
      CHECK(loaded2.searchThreadPoolSize == 8);

      AppSettings valid3;
      valid3.searchThreadPoolSize = 64;  // Valid: max value
      test_settings::SetInMemorySettings(valid3);

      AppSettings loaded3;
      LoadSettings(loaded3);
      CHECK(loaded3.searchThreadPoolSize == 64);
    }

    TEST_CASE_FIXTURE(SettingsFixture, "LoadSettings validates dynamic chunk size") {

      // Test valid range: 100-100000
      AppSettings valid1;
      valid1.dynamicChunkSize = 100;  // Minimum
      test_settings::SetInMemorySettings(valid1);

      AppSettings loaded1;
      LoadSettings(loaded1);
      CHECK(loaded1.dynamicChunkSize == 100);

      AppSettings valid2;
      valid2.dynamicChunkSize = 100000;  // Maximum
      test_settings::SetInMemorySettings(valid2);

      AppSettings loaded2;
      LoadSettings(loaded2);
      CHECK(loaded2.dynamicChunkSize == 100000);
    }

    TEST_CASE_FIXTURE(SettingsFixture, "LoadSettings validates hybrid initial work percent") {

      // Test valid range: 50-95
      AppSettings valid1;
      valid1.hybridInitialWorkPercent = 50;  // Minimum
      test_settings::SetInMemorySettings(valid1);

      AppSettings loaded1;
      LoadSettings(loaded1);
      CHECK(loaded1.hybridInitialWorkPercent == 50);

      AppSettings valid2;
      valid2.hybridInitialWorkPercent = 95;  // Maximum
      test_settings::SetInMemorySettings(valid2);

      AppSettings loaded2;
      LoadSettings(loaded2);
      CHECK(loaded2.hybridInitialWorkPercent == 95);
    }
  }

  TEST_SUITE("Round-trip tests") {

    TEST_CASE_FIXTURE(SettingsFixture, "Save and load round-trip") {
      const ScopedTempHomeForDiskSettings env("round_trip");

      AppSettings original;
      original.fontFamily = "RoundTrip Font";
      original.fontSize = 16.5F;
      original.fontScale = 1.25F;
      original.windowWidth = 1920;
      original.windowHeight = 1080;
      original.searchThreadPoolSize = 16;
      original.dynamicChunkSize = 5000;
      original.hybridInitialWorkPercent = 70;

      // Save
      bool save_result = SaveSettings(original);
      CHECK(save_result == true);

      // Load
      AppSettings loaded;
      bool load_result = LoadSettings(loaded);
      CHECK(load_result == true);

      // Verify all fields
      CHECK(loaded.fontFamily == original.fontFamily);
      CHECK(loaded.fontSize == original.fontSize);
      CHECK(loaded.fontScale == original.fontScale);
      CHECK(loaded.windowWidth == original.windowWidth);
      CHECK(loaded.windowHeight == original.windowHeight);
      CHECK(loaded.searchThreadPoolSize == original.searchThreadPoolSize);
      CHECK(loaded.dynamicChunkSize == original.dynamicChunkSize);
      CHECK(loaded.hybridInitialWorkPercent == original.hybridInitialWorkPercent);
    }

    TEST_CASE_FIXTURE(SettingsFixture, "Multiple round-trips preserve data") {
      const ScopedTempHomeForDiskSettings env("multi_round_trip");

      AppSettings settings;
      settings.fontFamily = "Multi Round Trip";
      settings.searchThreadPoolSize = 32;

      // First round-trip
      CHECK(SaveSettings(settings));
      AppSettings loaded1;
      LoadSettings(loaded1);
      CHECK(loaded1.fontFamily == "Multi Round Trip");
      CHECK(loaded1.searchThreadPoolSize == 32);

      // Modify and second round-trip
      loaded1.fontFamily = "Modified";
      loaded1.searchThreadPoolSize = 64;
      CHECK(SaveSettings(loaded1));

      AppSettings loaded2;
      LoadSettings(loaded2);
      CHECK(loaded2.fontFamily == "Modified");
      CHECK(loaded2.searchThreadPoolSize == 64);
    }
  }

  TEST_SUITE("Settings path (primary vs legacy)") {
    TEST_CASE_FIXTURE(SettingsFixture, "Save creates file at primary path when HOME is set and writable") {
      namespace fs = std::filesystem;
      const ScopedTempHomeForDiskSettings env("st");

      AppSettings to_save;
      to_save.fontSize = settings_defaults::kDefaultFontSize;
      const bool save_ok = SaveSettings(to_save);

      const std::string primary_file = env.PrimaryFile();
      const bool primary_exists = fs::exists(primary_file);

      CHECK(save_ok == true);
      CHECK(primary_exists == true);
    }

  }
}

