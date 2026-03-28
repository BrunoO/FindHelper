/**
 * @file ui/SearchHelpWindow.cpp
 * @brief Implementation of search help window rendering component
 */

#include "ui/SearchHelpWindow.h"

#include "imgui.h"

#include "ui/CenteredToolWindow.h"
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

    BeginHelpSection("4. Glob Search (Wildcards)");
    HelpBulletParagraph("Use * to match any sequence of characters.");
    HelpBulletParagraph("Use ? to match any single character.");
    HelpBulletParagraph("Example: '*.cpp' in Extensions matches all C++ files.");

    BeginHelpSection("5. PathPattern (Advanced patterns)");
    HelpBulletParagraph("Item name and Path support PathPattern matching (pp: prefix or auto-detect).");
    HelpBulletParagraph(
      "Patterns are automatically treated as PathPatterns if they contain: ^ or $ anchors,\n"
      "** (recursive), [abc] (character classes), {n} (quantifiers), or \\d/\\w (shorthands).");
    HelpBulletParagraph("Prefix with 'pp:' to explicitly force PathPattern (e.g., 'pp:**/logs**').");
    HelpBulletParagraph("Use 'pp:' with an empty pattern to match all indexed paths (pp:).");
    HelpBulletParagraph("Note: Simple patterns like '*.txt' or '*partage*.pdf' use Glob matching "
                        "(substring search).");
    HelpBulletParagraph(
      "Common PathPattern features: ** (cross-directory), [abc] (character classes),\n"
      "[a-z] (ranges), {n} (quantifiers), anchors ^ and $, \\d and \\w (shorthands).");
    HelpBulletParagraph("Examples: '^C:/Users/John/.*' (paths starting with that folder),\n"
                        "'**/*.cpp' (all .cpp files),\n"
                        "'**/logs**' (any path containing a 'logs' folder).");

    BeginHelpSection("6. Full Regex (ECMAScript)");
    HelpBulletParagraph(
      "Prefix with 'rs:' for full ECMAScript regex (for complex patterns with |, groups, etc.).");
    HelpBulletParagraph(
      "ECMAScript regex supports: full alternation (a|b), capturing groups, lookahead, etc.");
    HelpBulletParagraph("Use Full Regex when you need exclusions on paths or names; PathPattern does not support NOT filters.");
    HelpBulletParagraph(
      R"(Example (Path field): 'rs:^(?!.*(build|node_modules)).*\.cpp$' matches .cpp files whose full path does not contain 'build' or 'node_modules'.)");
    HelpBulletParagraph(
      "Tip: You can also ask AI search (\"Help Me Search\") for queries like 'C++ files excluding build and node_modules' to generate such regex for you.");
    HelpBulletParagraph("Example: '^main' (without prefix) is a PathPattern;\n"
                        "use 'rs:^main\\.(cpp|h)$' for alternation on extensions.");
    HelpBulletParagraph("Use the [R] Regex Generator button next to Path or Item name to build rs: patterns from templates.");

    BeginHelpSection("7. Fuzzy Search (Subsequence)");
    HelpBulletParagraph("Prefix with 'fz:' for fuzzy matching (subsequence matching).");
    HelpBulletParagraph("Fuzzy search matches characters in the order they appear, allowing gaps.");
    HelpBulletParagraph("Example: 'fz:fbr' matches 'foobar', 'fiber', and 'foo/bar'.");
    HelpBulletParagraph("This is useful for quickly finding paths when you only remember some letters.");

    BeginHelpSection("8. PathPattern Shorthands and Character Classes");
    ImGui::TextDisabled("Shorthands:");
    HelpBulletParagraph("\\d  : digits [0-9]");
    HelpBulletParagraph("\\w  : word characters [A-Za-z0-9_]");
    HelpBulletParagraph(
      "Examples: '**/*\\\\d{3}*.log' (3 digits in a row),\n"  // NOSONAR(cpp:S3628)
      "'**/\\\\w+.txt' (letters/digits/underscore).");  // Raw string literal would be less readable for user-facing examples

    ImGui::Spacing();
    ImGui::TextDisabled("Character classes:");
    HelpBulletParagraph("[abc]        : any one of a, b, or c");
    HelpBulletParagraph("[a-z]        : range (any lowercase letter)");
    HelpBulletParagraph("[A-Za-z0-9_] : letters, digits, underscore");
    HelpBulletParagraph("[^a-z]       : any single character except lowercase letters");
    HelpBulletParagraph("Examples: 'file[0-9].txt', '**/[A-Za-z]{2}_test.cpp'.");

    BeginHelpSection("9. Mark Mode (Selection & Bulk Actions)");
    HelpBulletParagraph("Use the 'Mark' column to select files for batch operations.");
    HelpBulletParagraph("Marked files are indicated with an asterisk (*) and a background tint.");
    HelpBulletParagraph("A toolbar appears when items are marked, offering 'Copy Paths' and 'Delete Marked'.");
    HelpBulletParagraph("Mark-mode shortcuts (when table is focused): n/p (nav), m (mark), u (unmark), t (toggle), Shift+M (mark all), Shift+T (invert marks), Shift+U (clear), W (copy paths), Shift+W (copy names), Shift+D (delete).");

    ImGui::Spacing();
    detail::RenderToolWindowCloseButton(p_open);
}

}  // namespace

void SearchHelpWindow::Render(bool *p_open) {
  constexpr float kDefaultWidth = 600.0F;
  constexpr float kDefaultHeight = 700.0F;
  detail::SetupCenteredToolWindow(kDefaultWidth, kDefaultHeight);

  detail::RenderToolWindow("Search Syntax Guide", p_open, Theme::Colors::WindowBg,
                           [p_open]() { RenderSearchHelpWindowContent(p_open); });
}

} // namespace ui
