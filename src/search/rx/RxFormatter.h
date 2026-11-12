#pragma once

/**
 * @file search/rx/RxFormatter.h
 * @brief Pretty-printer / auto-formatter for structured regex (`rx:`) S-expressions.
 */

#include <string>
#include <string_view>

namespace rx {

/**
 * @brief Formats an `rx:` expression with clean 2-space indentation and balanced nesting.
 *
 * Preserves strings, comments, and atomic expressions. Wraps nested compound
 * clauses (such as `(path ...)`, `(exclude-dir ...)`, `(dir ...)`, `(name ...)`)
 * onto new lines with appropriate indentation.
 *
 * @param source S-expression source, optionally with leading "rx:".
 * @return Formatted S-expression string.
 */
[[nodiscard]] std::string FormatRx(std::string_view source);

}  // namespace rx
