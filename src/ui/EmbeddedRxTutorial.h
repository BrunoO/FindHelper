#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace ui {

/**
 * @brief Callback signature for opening a file in its default viewer/application.
 */
using OpenFileCallback = std::function<void(std::string_view full_path)>;

/**
 * @brief Returns the embedded HTML content of the rx: tutorial guide.
 *
 * This provides offline, single-binary access to the complete tutorial guide
 * without requiring external file distribution.
 */
[[nodiscard]] std::string_view GetEmbeddedRxTutorialHtml();

/**
 * @brief Writes the embedded tutorial HTML to a temporary file.
 *
 * @return Absolute path to the written file, or empty string on failure.
 */
[[nodiscard]] std::string WriteEmbeddedRxTutorialToTempFile();

/**
 * @brief Exports the embedded tutorial HTML to a temporary file and opens it in the default browser.
 *
 * @param opener Custom file opener function (defaults to file_operations::OpenFileDefault if empty).
 * Safe to call from the UI thread.
 */
void OpenEmbeddedRxTutorialInBrowser(const OpenFileCallback& opener = {});

}  // namespace ui
