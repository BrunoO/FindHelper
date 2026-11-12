/**
 * @file ui/SearchHelpWindow.cpp
 * @brief Implementation of search help window rendering component
 */

#include "ui/SearchHelpWindow.h"

#include "imgui.h"

#include "ui/CenteredToolWindow.h"
#include "ui/EmbeddedRxTutorial.h"
#include "ui/IconsFontAwesome.h"
#include "ui/Theme.h"
#include "ui/UiStyleGuards.h"

namespace ui {

namespace {

void BeginHelpSection(const char* title) {
  ImGui::Spacing();
  ImGui::TextColored(Theme::Colors::Success, "%s", title);
}

void HelpBulletParagraph(const char* text) {
  ImGui::Bullet();
  ImGui::TextWrapped("%s", text);
}

void RenderSearchHelpWindowContent(bool *p_open) {
    ImGui::TextColored(Theme::Colors::Success, "1. Item name (main search bar)");
    HelpBulletParagraph("Matches against the item name only (file or folder name; e.g. 'readme' finds readme.txt).");

    BeginHelpSection("2. Path field");
    HelpBulletParagraph("Matches against the full path (directories + item name).");
    HelpBulletParagraph("Example: 'pp:**/imgui/**' finds files under any imgui folder.");

    BeginHelpSection("3. Combining filters");
    HelpBulletParagraph("Item name, Path, and Extensions are combined with AND.");
    HelpBulletParagraph("Use Extensions and Path in the Filters section to narrow results.");

    BeginHelpSection("4. Structured Regex (rx:) — Signature Feature");
    HelpBulletParagraph("Unique to FindHelper: readable, composable S-expression regular expressions inspired by Emacs rx.");
    HelpBulletParagraph("Eliminates backslash escaping entirely! Bare words are treated as literals: (dir src) requires no quotes.");
    HelpBulletParagraph("Path-aware domain operators: (path ...), (dir src), (exclude-dir build node_modules), (ext cpp h), (name ...).");
    HelpBulletParagraph(
      R"(Example: 'rx:(path (exclude-dir build node_modules) (dir src) (ext cpp h))' finds C++ files under 'src' while pruning build and dependency folders.)");
    HelpBulletParagraph(
      R"(Core operators: (seq ...), (or ...), (* ...), (+ ...), (? ...), (= 3 digit), (repeat 3 digit), (any "a-z"), (not digit). Wildcard symbols: any, any-char, anything.)");
    HelpBulletParagraph("Real-time UI feedback: Cyan border = valid (hover to view compiled ECMAScript preview & literal prefilters); Amber = incomplete (typing); Red = syntax error.");

    ImGui::Indent();
    if (ImGui::Button(ICON_FA_BOOK_OPEN " Open rx: Primer Guide")) {
      OpenEmbeddedRxTutorialInBrowser();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Extracts and opens the full embedded HTML primer guide in your default web browser.");
    }
    ImGui::Unindent();

    BeginHelpSection("5. Glob Search (Wildcards)");
    HelpBulletParagraph("Use * to match any sequence of characters within a path segment.");
    HelpBulletParagraph("Use ? to match any single character.");
    HelpBulletParagraph("Example: '*.cpp' in Extensions matches all C++ files.");
    HelpBulletParagraph("Note: Plain text without wildcards (e.g. 'cmake') uses substring search (matches anywhere).");

    BeginHelpSection("6. PathPattern (Fast Whole-Path Globs)");
    HelpBulletParagraph("Item name and Path support PathPattern matching (pp: prefix or auto-detect).");
    HelpBulletParagraph(
      "Patterns are automatically treated as PathPatterns if they contain: ^ or $ anchors,\n"
      "or ** (cross-directory).");
    HelpBulletParagraph("Prefix with 'pp:' to explicitly force PathPattern (e.g., 'pp:**/logs**').");
    HelpBulletParagraph("Use 'pp:' with an empty pattern to match all indexed paths (pp:).");
    HelpBulletParagraph(
      "IMPORTANT - Whole-path matching: PathPattern ALWAYS matches the entire string from start to end (^pattern$).\n"
      "• Leading '^' and trailing '$' are optional whole-string markers that trigger PathPattern auto-detection;\n"
      "  without wildcards, '^word', 'word$', and '^word$' all match ONLY the exact string 'word'.\n"
      "• '**word' matches paths ENDING with 'word' (e.g. '**cmake' matches file.cmake, but NOT CMakeLists.txt).\n"
      "• word** matches paths STARTING with 'word'.\n"
      "• **word** matches paths CONTAINING 'word' anywhere (e.g. '**cmakel**' matches CMakeLists.txt).\n"
      "• To match a filename prefix in subfolders, use '**/word*' (e.g. '**/cmakel*' matches CMakeLists.txt).\n"
      "• A leading '*' cannot reach past the first '/', so '*.cpp' in the Path field matches nothing.\n"
      "Use '**/*.cpp', or put '*.cpp' in the Item name or Extensions field instead.");
    HelpBulletParagraph(
      "Common PathPattern features: ** (cross-directory), * (within single folder segment),\n"
      "? (single non-separator), separators / and \\, optional whole-path anchors ^ and $.\n"
      "Brackets [...], braces {...}, and + are treated as exact literal characters.\n"
      "For regex character classes, repetition, or exclusions, use rx: or re:.");
    HelpBulletParagraph(
      "* and ? are ALWAYS wildcards. Path separators / and \\ are interchangeable.");
    HelpBulletParagraph("Examples:\n"
                        "• '^C:/Users/John/**' (paths starting with that folder)\n"
                        "• '**/*.cpp' (all .cpp files anywhere)\n"
                        "• '**/logs/**' (all files inside any folder named 'logs')\n"
                        "• '**/logs*/**/*.cpp' (files in folders starting with 'logs')\n"
                        "• '**/*logs*/**/*.cpp' (files in folders containing 'logs')");

    BeginHelpSection("7. Full Regex (ECMAScript)");
    HelpBulletParagraph(
      "Prefix with 're:' for full ECMAScript regex (for complex patterns with |, groups, etc.).");
    HelpBulletParagraph(
      "ECMAScript regex supports: full alternation (a|b), capturing groups, lookahead, etc.");
    HelpBulletParagraph("Use Full Regex when you need exclusions on paths or names; PathPattern does not support NOT filters.");
    HelpBulletParagraph(
      R"(Example (Path field): 're:^(?!.*(build|node_modules)).*\.cpp$' matches .cpp files whose full path does not contain 'build' or 'node_modules'.)");
    HelpBulletParagraph(
      "Tip: You can also ask AI search (\"Help Me Search\") for queries like 'C++ files excluding build and node_modules' to generate such regex for you.");
    HelpBulletParagraph("Example: '^main*' (without prefix) is a PathPattern matching names starting with 'main';\n"
                        "use 're:^main\\.(cpp|h)$' for alternation on extensions.");
    HelpBulletParagraph("Use the [R] Regex Generator button next to Path or Item name to build re: patterns from templates.");

    BeginHelpSection("8. Fuzzy Search & Typo Tolerance (fz:)");
    HelpBulletParagraph("Prefix with 'fz:' for relevance-scored fuzzy matching and 1-edit typo tolerance.");
    HelpBulletParagraph("Subsequence matching matches characters in order with gaps allowed (e.g., 'fz:fbr' matches 'foobar' and 'foo/bar').");
    HelpBulletParagraph("Typo tolerance (queries >= 3 chars) matches single substitutions, insertions, deletions, or adjacent character transpositions (e.g., 'fz:finq' or 'fz:fnid' matches 'find').");
    HelpBulletParagraph("Results are automatically scored and ranked. High-scoring exact and contiguous matches rank first, followed by typo matches.");
    HelpBulletParagraph("Use the dedicated 'Relevance' column in the results table to view scores and re-sort by relevance at any time.");

    BeginHelpSection("9. Match highlighting (results table)");
    HelpBulletParagraph(
      "In the results table, matched characters in the Name and Full Path columns are highlighted.");
    HelpBulletParagraph(
      "Name uses the Name field query and also Path-field matches that fall in the filename. "
      "Full Path uses the Path field query against the full path text (directory + filename). "
      "Extension highlights when the Extensions allowlist matches that row (whole cell). "
      "While '/' Filter in results is active, Name and Full Path use that filter "
      "(case-insensitive substring); Extension highlighting still follows the Extensions field.");
    HelpBulletParagraph(
      "Example: Path 'pp:**Monkey*pdf' highlights Monkey and pdf in SuperMonkeyReport.pdf "
      "in the Name column and again in the trailing filename of the Full Path column "
      "(directory literals such as a folder named Monkey are highlighted in Full Path only). "
      "Extensions 'cpp;h' paints the Extension cell amber for matching rows.");
    HelpBulletParagraph(
      "Supported: plain substring, fz: fuzzy, globs (literal parts), re: (first regex match), "
      "pp:/PathPattern (all literal runs such as 'logs' and '.cpp' in '**/logs/**/*.cpp'), "
      "and Extensions allowlist (exact extension, case-insensitive).");
    HelpBulletParagraph(
      "Not highlighted: Name/Path/`/` matches only in the Extension column (those already tint "
      "the Name column); patterns with no literal text (e.g. '**/*' or only wildcards); "
      "Path-field matches that appear only in the directory (not the filename) in the Name column; "
      "parts of a path hidden by leading ellipsis in a narrow Path column.");

    BeginHelpSection("9. Mark Mode (Selection & Bulk Actions)");
    HelpBulletParagraph("Use the 'Mark' column to select files for batch operations.");
    HelpBulletParagraph("Marked files are indicated with an asterisk (*) and a background tint.");
    HelpBulletParagraph("A toolbar appears when items are marked, offering 'Copy Paths' and 'Delete Marked'.");
    HelpBulletParagraph("Mark-mode shortcuts (when table is focused): n/p (nav), m (mark), u (unmark), t (toggle), Shift+M (mark all), Shift+T (invert marks), Shift+U (clear), W (copy paths), Shift+W (copy names), Shift+D (delete).");

    ImGui::Spacing();
    detail::RenderToolWindowCloseButton(p_open);
}

}  // namespace

namespace search_help_window {

void Render(bool *p_open) {
  constexpr float kDefaultWidth = 600.0F;
  constexpr float kDefaultHeight = 700.0F;
  detail::SetupCenteredToolWindow(kDefaultWidth, kDefaultHeight);

  detail::RenderToolWindow("Search Syntax Guide", p_open, Theme::Colors::WindowBg,
                           [p_open] { RenderSearchHelpWindowContent(p_open); });
}

}  // namespace search_help_window

} // namespace ui
