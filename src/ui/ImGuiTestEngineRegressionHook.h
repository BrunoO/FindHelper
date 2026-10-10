#pragma once

/**
 * @file ImGuiTestEngineRegressionHook.h
 * @brief Interface for ImGui Test Engine regression tests to drive search and read results.
 *
 * Only used when ENABLE_IMGUI_TEST_ENGINE is defined. Application implements this
 * so regression tests can set search params, trigger search, and assert result count
 * against golden data without depending on UI layout.
 *
 * Guard: When ENABLE_IMGUI_TEST_ENGINE is not defined, this header declares nothing.
 * Include only from TUs built with ENABLE_IMGUI_TEST_ENGINE to avoid ODR/linkage issues
 * in production builds (flag off).
 */

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#ifdef ENABLE_IMGUI_TEST_ENGINE

/**
 * Hook for regression tests: set params, trigger search, read result count.
 * Implemented by Application when ENABLE_IMGUI_TEST_ENGINE is ON.
 */
struct IRegressionTestHook {
  virtual ~IRegressionTestHook() = default;

  /** True when index is ready for search (not building, has entries or loaded from file). */
  [[nodiscard]] virtual bool IsIndexReady() const = 0;

  /** Index size (file_index_->Size()). Use to verify correct dataset is loaded (e.g. 789531 for std-linux-filesystem). */
  [[nodiscard]] virtual size_t GetIndexSize() const = 0;

  /**
   * Set search parameters (filename, path, extensions semicolon-separated, folders_only).
   * Callers must pass empty string for "unset" (do not pass nullptr; constructing string_view from nullptr is UB).
   * Implementations must accept empty strings as unset and must not construct string_view from nullptr.
   */
  virtual void SetSearchParams(std::string_view filename,
                               std::string_view path,
                               std::string_view extensions_semicolon,
                               bool folders_only) = 0;

  /**
   * Current search params (after set). Returned string_views reference Application-owned storage; valid only for the lifetime of the Application. Do not store beyond the call site—use for immediate comparison only. No-throw: only reads internal state.
   */
  [[nodiscard]] virtual std::string_view GetSearchParamFilename() const noexcept = 0;
  [[nodiscard]] virtual std::string_view GetSearchParamPath() const noexcept = 0;
  [[nodiscard]] virtual std::string_view GetSearchParamExtensions() const noexcept = 0;
  [[nodiscard]] virtual bool GetSearchParamFoldersOnly() const noexcept = 0;

  /** Trigger a manual search using current state_. */
  virtual void TriggerManualSearch() = 0;

  /** True when the last triggered search has completed (!searchActive). */
  [[nodiscard]] virtual bool IsSearchComplete() const = 0;

  /** Number of results from the last search (state_.result_pool_->Results().size()). */
  [[nodiscard]] virtual size_t GetSearchResultCount() const = 0;

  /**
   * Get the recursive non-directory file count for a search result by its index.
   * Returns kFolderFileCountNotLoaded if not yet computed, or 0/sentinel for files.
   */
  [[nodiscard]] virtual uint64_t GetSearchResultFolderFileCount(size_t index) const = 0;

  /**
   * Get the full path of a search result by its index.
   * Used in tests to identify which result row we are asserting against.
   */
  [[nodiscard]] virtual std::string_view GetSearchResultPath(size_t index) const = 0;

  /**
   * Request that the app set selection to the first row and mark it (for shortcut tests).
   * Consumed once on next ProcessFrame when display results are available.
   */
  virtual void RequestSetSelectionAndMarkFirstResult() = 0;

  /**
   * Request that the app copy marked paths to clipboard (for shortcut tests).
   * Consumed once on next ProcessFrame after selection/mark may have been applied.
   */
  virtual void RequestCopyMarkedPathsToClipboard() = 0;

  /**
   * Request that the app copy marked filenames to clipboard (Shift+W path).
   * Consumed once on next ProcessFrame after selection/mark may have been applied.
   */
  virtual void RequestCopyMarkedFilenamesToClipboard() = 0;

  /**
   * Request that the app copy the selected row's path to clipboard (Ctrl/Cmd+Shift+C path).
   * Consumed once on next ProcessFrame; selection should be set first (e.g. RequestSetSelectionAndMarkFirstResult).
   */
  virtual void RequestCopySelectedPathToClipboard() = 0;

  /** One-shot: true if RequestSetSelectionAndMarkFirstResult was called, then cleared. */
  [[nodiscard]] virtual bool GetAndClearRequestSetSelectionAndMarkFirstResult() = 0;

  /** One-shot: true if RequestCopyMarkedPathsToClipboard was called, then cleared. */
  [[nodiscard]] virtual bool GetAndClearRequestCopyMarkedPathsToClipboard() = 0;

  /** One-shot: true if RequestCopyMarkedFilenamesToClipboard was called, then cleared. */
  [[nodiscard]] virtual bool GetAndClearRequestCopyMarkedFilenamesToClipboard() = 0;

  /** One-shot: true if RequestCopySelectedPathToClipboard was called, then cleared. */
  [[nodiscard]] virtual bool GetAndClearRequestCopySelectedPathToClipboard() = 0;

  /**
   * Return current clipboard text from the app's GLFW window.
   * Use this in tests to read the same clipboard the app wrote to (avoids ImGui/backend mismatch).
   */
  [[nodiscard]] virtual std::string GetClipboardText() const = 0;
};

/**
 * Adapter with default no-op implementations for all IRegressionTestHook methods.
 * Alternative Application-like implementations can inherit from this and override only
 * the methods they support, avoiding breaking when new interface methods (e.g. GetIndexSize) are added.
 */
struct RegressionTestHookAdapter : IRegressionTestHook {
  [[nodiscard]] bool IsIndexReady() const override { return false; }
  [[nodiscard]] size_t GetIndexSize() const override { return 0U; }
  void SetSearchParams(std::string_view /*filename*/,
                      std::string_view /*path*/,
                      std::string_view /*extensions_semicolon*/,
                      bool /*folders_only*/) override { /* stub: no-op for regression test adapter */ }
  [[nodiscard]] std::string_view GetSearchParamFilename() const noexcept override { return {}; }
  [[nodiscard]] std::string_view GetSearchParamPath() const noexcept override { return {}; }
  [[nodiscard]] std::string_view GetSearchParamExtensions() const noexcept override { return {}; }
  [[nodiscard]] bool GetSearchParamFoldersOnly() const noexcept override { return false; }
  void TriggerManualSearch() override { /* stub: no-op for regression test adapter */ }
  [[nodiscard]] bool IsSearchComplete() const override { return true; }
  [[nodiscard]] size_t GetSearchResultCount() const override { return 0U; }
  [[nodiscard]] uint64_t GetSearchResultFolderFileCount(size_t /*index*/) const override { return 0U; }
  [[nodiscard]] std::string_view GetSearchResultPath(size_t /*index*/) const override { return {}; }
  void RequestSetSelectionAndMarkFirstResult() override { /* stub: no-op for regression test adapter */ }
  void RequestCopyMarkedPathsToClipboard() override { /* stub: no-op for regression test adapter */ }
  void RequestCopyMarkedFilenamesToClipboard() override { /* stub: no-op for regression test adapter */ }
  void RequestCopySelectedPathToClipboard() override { /* stub: no-op for regression test adapter */ }
  [[nodiscard]] bool GetAndClearRequestSetSelectionAndMarkFirstResult() override { return false; }
  [[nodiscard]] bool GetAndClearRequestCopyMarkedPathsToClipboard() override { return false; }
  [[nodiscard]] bool GetAndClearRequestCopyMarkedFilenamesToClipboard() override { return false; }
  [[nodiscard]] bool GetAndClearRequestCopySelectedPathToClipboard() override { return false; }
  [[nodiscard]] std::string GetClipboardText() const override { return {}; }
};

#endif  // ENABLE_IMGUI_TEST_ENGINE
