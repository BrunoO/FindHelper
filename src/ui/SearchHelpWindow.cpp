/**
 * @file ui/SearchHelpWindow.cpp
 * @brief Implementation of search help window rendering component
 */

#include "ui/SearchHelpWindow.h"

#include "imgui.h"

#include "ui/IconsFontAwesome.h"
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

  // Set window position and size - use FirstUseEver so position is only set once, allowing user to move it
  const ImGuiViewport *main_viewport = ImGui::GetMainViewport();
  const auto center = ImVec2(main_viewport->WorkPos.x + (main_viewport->WorkSize.x * 0.5F),
                             main_viewport->WorkPos.y + (main_viewport->WorkSize.y * 0.5F));
  ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2(0.5F, 0.5F));
  constexpr float default_window_width = 600.0F;
  constexpr float default_window_height = 700.0F;
  ImGui::SetNextWindowSize(ImVec2(default_window_width, default_window_height), ImGuiCond_FirstUseEver);
  
  const detail::WindowGuard window_guard(window_title, p_open, ImGuiWindowFlags_None);
  if (window_guard.ShowContent()) {
    ImGui::TextColored(Theme::Colors::Success, "1. Item name (main search bar)"); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("Matches against the item name only (file or folder name; e.g. 'readme' finds readme.txt)."); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)

    ImGui::Spacing();
    ImGui::TextColored(Theme::Colors::Success, "2. Path field"); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("Matches against the full path (directories + item name)."); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("Example: 'pp:**/imgui/**' finds files under any imgui folder."); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)

    ImGui::Spacing();
    ImGui::TextColored(Theme::Colors::Success, "3. Combining filters"); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("Item name, Path, and Extensions are combined with AND."); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("Use Extensions and Path in the Filters section to narrow results."); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)

    ImGui::Spacing();
    ImGui::TextColored(Theme::Colors::Success, "4. Glob Search (Wildcards)"); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("Use * to match any sequence of characters."); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("Use ? to match any single character."); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("Example: '*.cpp' in Extensions matches all C++ files."); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)

    ImGui::Spacing();
    ImGui::TextColored(Theme::Colors::Success, "5. PathPattern (Advanced patterns)"); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("Item name and Path support PathPattern matching (pp: prefix or auto-detect)."); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped( // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
      "Patterns are automatically treated as PathPatterns if they contain: ^ or $ anchors,\n"
      "** (recursive), [abc] (character classes), {n} (quantifiers), or \\d/\\w (shorthands).");
    ImGui::Bullet();
    ImGui::TextWrapped("Prefix with 'pp:' to explicitly force PathPattern (e.g., 'pp:**/logs**')."); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("Use 'pp:' with an empty pattern to match all indexed paths (pp:)."); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("Note: Simple patterns like '*.txt' or '*partage*.pdf' use Glob matching " // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
                      "(substring search).");
    ImGui::Bullet();
    ImGui::TextWrapped( // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
      "Common PathPattern features: ** (cross-directory), [abc] (character classes),\n"
      "[a-z] (ranges), {n} (quantifiers), anchors ^ and $, \\d and \\w (shorthands).");
    ImGui::Bullet();
    ImGui::TextWrapped("Examples: '^C:/Users/John/.*' (paths starting with that folder),\n" // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
                      "'**/*.cpp' (all .cpp files),\n"
                      "'**/logs**' (any path containing a 'logs' folder).");

    ImGui::Spacing();
    ImGui::TextColored(Theme::Colors::Success, "6. Full Regex (ECMAScript)"); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped( // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
      "Prefix with 'rs:' for full ECMAScript regex (for complex patterns with |, groups, etc.).");
    ImGui::Bullet();
    ImGui::TextWrapped( // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
      "ECMAScript regex supports: full alternation (a|b), capturing groups, lookahead, etc.");
    ImGui::Bullet();
    ImGui::TextWrapped("Use Full Regex when you need exclusions on paths or names; PathPattern does not support NOT filters."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped( // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      R"(Example (Path field): 'rs:^(?!.*(build|node_modules)).*\.cpp$' matches .cpp files whose full path does not contain 'build' or 'node_modules'.)");
    ImGui::Bullet();
    ImGui::TextWrapped( // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
      "Tip: You can also ask AI search (\"Help Me Search\") for queries like 'C++ files excluding build and node_modules' to generate such regex for you.");
    ImGui::Bullet();
    ImGui::TextWrapped("Example: '^main' (without prefix) is a PathPattern;\n" // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
                      "use 'rs:^main\\.(cpp|h)$' for alternation on extensions.");

    ImGui::Spacing();
    ImGui::TextColored(Theme::Colors::Success, "7. PathPattern Shorthands and Character Classes"); // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    ImGui::TextDisabled("Shorthands:"); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("\\d  : digits [0-9]"); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("\\w  : word characters [A-Za-z0-9_]"); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped( // NOLINT(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
      "Examples: '**/*\\\\d{3}*.log' (3 digits in a row),\n"  // NOSONAR(cpp:S3628)
      "'**/\\\\w+.txt' (letters/digits/underscore).");  // Raw string literal would be less readable for user-facing examples

    ImGui::Spacing();
    ImGui::TextDisabled("Character classes:"); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("[abc]        : any one of a, b, or c"); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("[a-z]        : range (any lowercase letter)"); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("[A-Za-z0-9_] : letters, digits, underscore"); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("[^a-z]       : any single character except lowercase letters"); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("Examples: 'file[0-9].txt', '**/[A-Za-z]{2}_test.cpp'."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)

    ImGui::Spacing();
    ImGui::TextColored(Theme::Colors::Success, "8. Mark Mode (Selection & Bulk Actions)"); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("Use the 'Mark' column to select files for batch operations."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("Marked files are indicated with an asterisk (*) and a background tint."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("A toolbar appears when items are marked, offering 'Copy Paths' and 'Delete Marked'."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    ImGui::Bullet();
    ImGui::TextWrapped("Mark-mode shortcuts (when table is focused): n/p (nav), m (mark), u (unmark), t (toggle), Shift+M (mark all), Shift+T (invert marks), Shift+U (clear), W (copy paths), Shift+W (copy names), Shift+D (delete)."); // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)

    ImGui::Spacing();
    ImGui::Separator();
    if (const float close_button_width = 120.0F;
        ImGui::Button(ICON_FA_XMARK " Close", ImVec2(close_button_width, 0))) {
      *p_open = false;
    }
  }
}

} // namespace ui
