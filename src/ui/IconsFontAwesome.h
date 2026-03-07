#pragma once

/**
 * @file ui/IconsFontAwesome.h
 * @brief FontAwesome 6 icon constants for use in UI
 *
 * This file defines icon constants for FontAwesome 6 Solid icons.
 * Icons are used as Unicode characters that map to FontAwesome glyphs.
 *
 * Usage:
 *   ImGui::Text(ICON_FA_SAVE " Save");
 *   ImGui::Button(ICON_FA_TRASH " Delete");
 *
 * License: Font Awesome Free License (SIL OFL 1.1)
 * Source: https://fontawesome.com/
 */

// FontAwesome 6 Solid icons (commonly used in this application)
// Macros required for string literal concatenation in ImGui (e.g., ICON_FA_SAVE " Save")
// C++ string literal concatenation only works with macros, not constexpr variables
#define ICON_FA_QUESTION_CIRCLE "\xef\x81\x99"  // U+F059 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_CIRCLE_QUESTION "\xef\x8a\x99"  // U+F29A (FA6) // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_CODE "\xef\x84\xa1"              // U+F121 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_CIRCLE "\xef\x84\x91"           // U+F111 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_LIGHTBULB "\xef\x83\xab"        // U+F0EB // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_SAVE "\xef\x83\x87"             // U+F0C7 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_FLOPPY_DISK "\xef\x83\x87"      // U+F0C7 (same as save) // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_FILTER "\xef\x83\xb0"           // U+F0B0 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_XMARK "\xef\x80\x8d"            // U+F00D (FA6) // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_TIMES "\xef\x80\x8d"            // U+F00D (alias) // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_TRASH "\xef\x87\xb8"            // U+F1F8 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_TRASH_CAN "\xef\x8b\xad"        // U+F2ED (FA6) // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_ERASER "\xef\x84\xad"           // U+F12D // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_GEAR "\xef\x80\x93"             // U+F013 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_COG "\xef\x80\x93"              // U+F013 (alias) // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_CHART_BAR "\xef\x82\x80"        // U+F080 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_CHART_LINE "\xef\x88\x81"      // U+F201 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_BOOKMARK "\xef\x86\xae"         // U+F02E // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_SEARCH "\xef\x80\x82"           // U+F002 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_MAGNIFYING_GLASS "\xef\x80\x82" // U+F002 (FA6 alias) // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_SEARCH_PLUS "\xef\x80\x8e"      // U+F00E // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_BAN "\xef\x81\x9e"              // U+F05E // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_XMARK_CIRCLE "\xef\x8a\x96"     // U+F29A (FA6) // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_CHECK "\xef\x80\x8c"            // U+F00C // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_CHECK_DOUBLE "\xef\x95\xa0"     // U+F560 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)

#define ICON_FA_FOLDER_OPEN "\xef\x81\xbc"      // U+F07C // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_STOP "\xef\x81\x8d"             // U+F04D // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_PLAY "\xef\x81\x8b"             // U+F04B // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_ARROW_UP "\xef\x81\xa2"         // U+F062 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_ARROW_DOWN "\xef\x81\xa3"       // U+F063 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_REFRESH "\xef\x80\xa1"          // U+F021 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_HOME "\xef\x80\x95"             // U+F015 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_COPY ""             // U+F0C5 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_FILE_EXPORT "\xef\x95\xae"      // U+F56E // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_EXPAND "\xef\x81\xa5"           // U+F065 // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
#define ICON_FA_LIST "\xef\x80\xba"             // U+F03A // NOSONAR(cpp:S5028) // NOLINT(cppcoreguidelines-macro-usage)
