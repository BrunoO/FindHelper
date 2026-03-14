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

void SearchHelpWindow::Render(bool *p_open) {
  if (p_open == nullptr || !*p_open) {
    return;
  }

  // Use theme tool-window background
  const detail::StyleColorGuard window_bg_guard(ImGuiCol_WindowBg, Theme::Colors::WindowBg);

  const char *window_title = "Search Syntax Guide";

  constexpr float kDefaultWidth = 600.0F;
  constexpr float kDefaultHeight = 700.0F;
  detail::SetupCenteredToolWindow(kDefaultWidth, kDefaultHeight);

  const detail::WindowGuard window_guard(window_title, p_open, ImGuiWindowFlags_None);
  if (window_guard.ShowContent()) {
    ImGui::TextColored(Theme::Colors::Success, "1. Item name (main search bar)");
    ImGui::Bullet();
    ImGui::TextWrapped("Matches against the item name only (file or folder name; e.g. 'readme' finds readme.txt).");

    ImGui::Spacing();
    ImGui::TextColored(Theme::Colors::Success, "2. Path field");
    ImGui::Bullet();
    ImGui::TextWrapped("Matches against the full path (directories + item name).");
    ImGui::Bullet();
    ImGui::TextWrapped("Example: 'pp:**/imgui/**' finds files under any imgui folder.");

    ImGui::Spacing();
    ImGui::TextColored(Theme::Colors::Success, "3. Combining filters");
    ImGui::Bullet();
    ImGui::TextWrapped("Item name, Path, and Extensions are combined with AND.");
    ImGui::Bullet();
    ImGui::TextWrapped("Use Extensions and Path in the Filters section to narrow results.");

    ImGui::Spacing();
    ImGui::TextColored(Theme::Colors::Success, "4. Glob Search (Wildcards)");
    ImGui::Bullet();
    ImGui::TextWrapped("Use * to match any sequence of characters.");
    ImGui::Bullet();
    ImGui::TextWrapped("Use ? to match any single character.");
    ImGui::Bullet();
    ImGui::TextWrapped("Example: '*.cpp' in Extensions matches all C++ files.");

    ImGui::Spacing();
    ImGui::TextColored(Theme::Colors::Success, "5. PathPattern (Advanced patterns)");
    ImGui::Bullet();
    ImGui::TextWrapped("Item name and Path support PathPattern matching (pp: prefix or auto-detect).");
    ImGui::Bullet();
    ImGui::TextWrapped(
      "Patterns are automatically treated as PathPatterns if they contain: ^ or $ anchors,\n"
      "** (recursive), [abc] (character classes), {n} (quantifiers), or \\d/\\w (shorthands).");
    ImGui::Bullet();
    ImGui::TextWrapped("Prefix with 'pp:' to explicitly force PathPattern (e.g., 'pp:**/logs**').");
    ImGui::Bullet();
    ImGui::TextWrapped("Use 'pp:' with an empty pattern to match all indexed paths (pp:).");
    ImGui::Bullet();
    ImGui::TextWrapped("Note: Simple patterns like '*.txt' or '*partage*.pdf' use Glob matching "
                      "(substring search).");
    ImGui::Bullet();
    ImGui::TextWrapped(
      "Common PathPattern features: ** (cross-directory), [abc] (character classes),\n"
      "[a-z] (ranges), {n} (quantifiers), anchors ^ and $, \\d and \\w (shorthands).");
    ImGui::Bullet();
    ImGui::TextWrapped("Examples: '^C:/Users/John/.*' (paths starting with that folder),\n"
                      "'**/*.cpp' (all .cpp files),\n"
                      "'**/logs**' (any path containing a 'logs' folder).");

    ImGui::Spacing();
    ImGui::TextColored(Theme::Colors::Success, "6. Full Regex (ECMAScript)");
    ImGui::Bullet();
    ImGui::TextWrapped(
      "Prefix with 'rs:' for full ECMAScript regex (for complex patterns with |, groups, etc.).");
    ImGui::Bullet();
    ImGui::TextWrapped(
      "ECMAScript regex supports: full alternation (a|b), capturing groups, lookahead, etc.");
    ImGui::Bullet();
    ImGui::TextWrapped("Use Full Regex when you need exclusions on paths or names; PathPattern does not support NOT filters.");
    ImGui::Bullet();
    ImGui::TextWrapped(
      R"(Example (Path field): 'rs:^(?!.*(build|node_modules)).*\.cpp$' matches .cpp files whose full path does not contain 'build' or 'node_modules'.)");
    ImGui::Bullet();
    ImGui::TextWrapped(
      "Tip: You can also ask AI search (\"Help Me Search\") for queries like 'C++ files excluding build and node_modules' to generate such regex for you.");
    ImGui::Bullet();
    ImGui::TextWrapped("Example: '^main' (without prefix) is a PathPattern;\n"
                      "use 'rs:^main\\.(cpp|h)$' for alternation on extensions.");
    ImGui::Bullet();
    ImGui::TextWrapped("Use the [R] Regex Generator button next to Path or Item name to build rs: patterns from templates.");

    ImGui::Spacing();
    ImGui::TextColored(Theme::Colors::Success, "7. Fuzzy Search (Subsequence)");
    ImGui::Bullet();
    ImGui::TextWrapped("Prefix with 'fz:' for fuzzy matching (subsequence matching).");
    ImGui::Bullet();
    ImGui::TextWrapped("Fuzzy search matches characters in the order they appear, allowing gaps.");
    ImGui::Bullet();
    ImGui::TextWrapped("Example: 'fz:fbr' matches 'foobar', 'fiber', and 'foo/bar'.");
    ImGui::Bullet();
    ImGui::TextWrapped("This is useful for quickly finding paths when you only remember some letters.");

    ImGui::Spacing();
    ImGui::TextColored(Theme::Colors::Success, "8. PathPattern Shorthands and Character Classes");
    ImGui::TextDisabled("Shorthands:");
    ImGui::Bullet();
    ImGui::TextWrapped("\\d  : digits [0-9]");
    ImGui::Bullet();
    ImGui::TextWrapped("\\w  : word characters [A-Za-z0-9_]");
    ImGui::Bullet();
    ImGui::TextWrapped(
      "Examples: '**/*\\\\d{3}*.log' (3 digits in a row),\n"  // NOSONAR(cpp:S3628)
      "'**/\\\\w+.txt' (letters/digits/underscore).");  // Raw string literal would be less readable for user-facing examples

    ImGui::Spacing();
    ImGui::TextDisabled("Character classes:");
    ImGui::Bullet();
    ImGui::TextWrapped("[abc]        : any one of a, b, or c");
    ImGui::Bullet();
    ImGui::TextWrapped("[a-z]        : range (any lowercase letter)");
    ImGui::Bullet();
    ImGui::TextWrapped("[A-Za-z0-9_] : letters, digits, underscore");
    ImGui::Bullet();
    ImGui::TextWrapped("[^a-z]       : any single character except lowercase letters");
    ImGui::Bullet();
    ImGui::TextWrapped("Examples: 'file[0-9].txt', '**/[A-Za-z]{2}_test.cpp'.");

    ImGui::Spacing();
    ImGui::TextColored(Theme::Colors::Success, "9. Mark Mode (Selection & Bulk Actions)");
    ImGui::Bullet();
    ImGui::TextWrapped("Use the 'Mark' column to select files for batch operations.");
    ImGui::Bullet();
    ImGui::TextWrapped("Marked files are indicated with an asterisk (*) and a background tint.");
    ImGui::Bullet();
    ImGui::TextWrapped("A toolbar appears when items are marked, offering 'Copy Paths' and 'Delete Marked'.");
    ImGui::Bullet();
    ImGui::TextWrapped("Mark-mode shortcuts (when table is focused): n/p (nav), m (mark), u (unmark), t (toggle), Shift+M (mark all), Shift+T (invert marks), Shift+U (clear), W (copy paths), Shift+W (copy names), Shift+D (delete).");

    ImGui::Spacing();
    detail::RenderToolWindowCloseButton(p_open);
  }
}

} // namespace ui
