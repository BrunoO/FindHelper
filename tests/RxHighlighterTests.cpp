#include "ui/RxHighlighter.h"

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "doctest/doctest.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "search/rx/RxCompiler.h"

TEST_SUITE("RxHighlighter - Tokenizer and Classifier") {

  TEST_CASE("ClassifyRxAtom recognizes core operators, path clauses, and named classes") {
    // Core operators (only in clause head position)
    CHECK(rx::ClassifyRxAtom("seq", true) == rx::RxTokenCategory::kOperator);
    CHECK(rx::ClassifyRxAtom("or", true) == rx::RxTokenCategory::kOperator);
    CHECK(rx::ClassifyRxAtom("*", true) == rx::RxTokenCategory::kOperator);
    CHECK(rx::ClassifyRxAtom("+", true) == rx::RxTokenCategory::kOperator);
    CHECK(rx::ClassifyRxAtom("?", true) == rx::RxTokenCategory::kOperator);
    CHECK(rx::ClassifyRxAtom("repeat", true) == rx::RxTokenCategory::kOperator);
    CHECK(rx::ClassifyRxAtom("not", true) == rx::RxTokenCategory::kOperator);

    // Operator atoms in argument position are treated as bare literals
    CHECK(rx::ClassifyRxAtom("seq", false) == rx::RxTokenCategory::kBareLiteral);
    CHECK(rx::ClassifyRxAtom("or", false) == rx::RxTokenCategory::kBareLiteral);
    CHECK(rx::ClassifyRxAtom("repeat", false) == rx::RxTokenCategory::kBareLiteral);

    // Path clauses (only in clause head position)
    CHECK(rx::ClassifyRxAtom("path", true) == rx::RxTokenCategory::kPathClause);
    CHECK(rx::ClassifyRxAtom("dir", true) == rx::RxTokenCategory::kPathClause);
    CHECK(rx::ClassifyRxAtom("exclude-dir", true) == rx::RxTokenCategory::kPathClause);
    CHECK(rx::ClassifyRxAtom("not-dir", true) == rx::RxTokenCategory::kPathClause);
    CHECK(rx::ClassifyRxAtom("name", true) == rx::RxTokenCategory::kPathClause);
    CHECK(rx::ClassifyRxAtom("ext", true) == rx::RxTokenCategory::kPathClause);

    // Path clause names in argument position are treated as bare literals
    CHECK(rx::ClassifyRxAtom("dir", false) == rx::RxTokenCategory::kBareLiteral);
    CHECK(rx::ClassifyRxAtom("name", false) == rx::RxTokenCategory::kBareLiteral);
    CHECK(rx::ClassifyRxAtom("ext", false) == rx::RxTokenCategory::kBareLiteral);

    // Named classes & anchors (valid in argument position)
    CHECK(rx::ClassifyRxAtom("digit", false) == rx::RxTokenCategory::kNamedClass);
    CHECK(rx::ClassifyRxAtom("alpha", false) == rx::RxTokenCategory::kNamedClass);
    CHECK(rx::ClassifyRxAtom("alnum", false) == rx::RxTokenCategory::kNamedClass);
    CHECK(rx::ClassifyRxAtom("word", false) == rx::RxTokenCategory::kNamedClass);
    CHECK(rx::ClassifyRxAtom("bos", false) == rx::RxTokenCategory::kNamedClass);
    CHECK(rx::ClassifyRxAtom("eos", false) == rx::RxTokenCategory::kNamedClass);
    CHECK(rx::ClassifyRxAtom("any-char", false) == rx::RxTokenCategory::kNamedClass);

    // Named class in clause head position is invalid as an operator, falling back to bare literal
    CHECK(rx::ClassifyRxAtom("digit", true) == rx::RxTokenCategory::kBareLiteral);

    // Bare literals
    CHECK(rx::ClassifyRxAtom("my_file", false) == rx::RxTokenCategory::kBareLiteral);
    CHECK(rx::ClassifyRxAtom("cpp", false) == rx::RxTokenCategory::kBareLiteral);
    CHECK(rx::ClassifyRxAtom("src", false) == rx::RxTokenCategory::kBareLiteral);
    CHECK(rx::ClassifyRxAtom("123", false) == rx::RxTokenCategory::kBareLiteral);
    CHECK(rx::ClassifyRxAtom("", false) == rx::RxTokenCategory::kBareLiteral);
  }

  TEST_CASE("TokenizeRx produces contiguous tokens covering the entire source") {
    const std::string_view source = "rx:(path (dir src) (ext cpp h))";
    std::vector<ui::RxToken> tokens;
    ui::TokenizeRx(source, tokens);

    REQUIRE_FALSE(tokens.empty());

    // Verify contiguity: token[0].offset == 0, and each token starts where the previous ended
    CHECK(tokens.front().offset == 0);
    std::size_t expected_offset = 0;
    for (const auto& tok : tokens) {
      CHECK(tok.offset == expected_offset);
      CHECK(tok.length > 0);
      expected_offset += tok.length;
    }
    CHECK(expected_offset == source.size());

    // Verify token categories
    CHECK(tokens.at(0).category == rx::RxTokenCategory::kPrefix);        // "rx:"
    CHECK(tokens.at(1).category == rx::RxTokenCategory::kParenthesis);   // "("
    CHECK(tokens.at(2).category == rx::RxTokenCategory::kPathClause);    // "path"
    CHECK(tokens.at(3).category == rx::RxTokenCategory::kWhitespace);    // " "
    CHECK(tokens.at(4).category == rx::RxTokenCategory::kParenthesis);   // "("
    CHECK(tokens.at(5).category == rx::RxTokenCategory::kPathClause);    // "dir"
    CHECK(tokens.at(6).category == rx::RxTokenCategory::kWhitespace);    // " "
    CHECK(tokens.at(7).category == rx::RxTokenCategory::kBareLiteral);   // "src"
    CHECK(tokens.at(8).category == rx::RxTokenCategory::kParenthesis);   // ")"
  }

  TEST_CASE("TokenizeRx applies position-aware classification to clause heads vs arguments") {
    const std::string_view source = "rx:(seq \"foo\" dir seq digit)";
    std::vector<ui::RxToken> tokens;
    ui::TokenizeRx(source, tokens);

    REQUIRE_FALSE(tokens.empty());
    // "(" -> Paren, "seq" -> Operator, " " -> Ws, "\"foo\"" -> QuotedString, " " -> Ws,
    // "dir" -> BareLiteral (not clause head!), " " -> Ws, "seq" -> BareLiteral, " " -> Ws,
    // "digit" -> NamedClass, ")" -> Paren
    bool checked_dir = false;
    bool checked_arg_seq = false;
    bool checked_digit = false;
    for (const auto& tok : tokens) {
      if (const std::string_view slice = source.substr(tok.offset, tok.length); slice == "dir") {
        CHECK(tok.category == rx::RxTokenCategory::kBareLiteral);
        checked_dir = true;
      } else if (slice == "seq" && tok.offset > 8) {
        CHECK(tok.category == rx::RxTokenCategory::kBareLiteral);
        checked_arg_seq = true;
      } else if (slice == "digit") {
        CHECK(tok.category == rx::RxTokenCategory::kNamedClass);
        checked_digit = true;
      }
    }
    CHECK(checked_dir);
    CHECK(checked_arg_seq);
    CHECK(checked_digit);
  }

  TEST_CASE("TokenizeRx handles verbatim quoted strings matching RxCompiler") {
    // Verbatim string with backslash (no escape processing, matching RxCompiler::ParseQuoted)
    const std::string_view source = R"(rx:(name "folder\file.txt" "another file"))";
    std::vector<ui::RxToken> tokens;
    ui::TokenizeRx(source, tokens);

    std::size_t total_len = 0;
    std::size_t string_count = 0;
    for (const auto& tok : tokens) {
      total_len += tok.length;
      if (tok.category == rx::RxTokenCategory::kQuotedString) {
        ++string_count;
        if (const std::string_view slice = source.substr(tok.offset, tok.length); string_count == 1) {
          CHECK(slice == R"("folder\file.txt")");
        } else if (string_count == 2) {
          CHECK(slice == R"("another file")");
        }
      }
    }
    CHECK(total_len == source.size());
    CHECK(string_count == 2);
  }

  TEST_CASE("TokenizeRx handles complex nested expressions") {
    const std::string_view source = "rx:(name (seq \"test_\" (+ digit) (? (seq \"-\" (+ alnum)))))";
    std::vector<ui::RxToken> tokens;
    ui::TokenizeRx(source, tokens);

    std::size_t total_len = 0;
    std::size_t op_count = 0;
    std::size_t class_count = 0;
    std::size_t paren_count = 0;

    for (const auto& tok : tokens) {
      total_len += tok.length;
      if (tok.category == rx::RxTokenCategory::kOperator) {
        ++op_count;
      } else if (tok.category == rx::RxTokenCategory::kNamedClass) {
        ++class_count;
      } else if (tok.category == rx::RxTokenCategory::kParenthesis) {
        ++paren_count;
      }
    }

    CHECK(total_len == source.size());
    CHECK(op_count >= 3);     // seq, +, ?, seq, +
    CHECK(class_count >= 2);  // digit, alnum
    CHECK(paren_count == 12); // 6 opening, 6 closing
  }

  TEST_CASE("TokenizeRx gracefully handles malformed/unterminated input") {
    // Unclosed string
    {
      const std::string_view unclosed_str = "rx:(name \"unterminated";
      std::vector<ui::RxToken> tokens;
      ui::TokenizeRx(unclosed_str, tokens);
      std::size_t total_len = 0;
      for (const auto& tok : tokens) {
        total_len += tok.length;
      }
      CHECK(total_len == unclosed_str.size());
    }

    // Unbalanced parens
    {
      const std::string_view unbalanced = "rx:(((";
      std::vector<ui::RxToken> tokens;
      ui::TokenizeRx(unbalanced, tokens);
      CHECK(tokens.size() == 4); // "rx:" + 3 '('
    }

    // Empty input
    {
      std::vector<ui::RxToken> tokens;
      ui::TokenizeRx("", tokens);
      CHECK(tokens.empty());
    }
  }

  TEST_CASE("RenderRxSyntaxOverlay successfully resolves child window and draws tokens in multiline mode") {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.0F, 600.0F);
    if (io.Fonts->Fonts.empty()) {
      io.Fonts->AddFontDefault();
    }
    unsigned char* font_pixels = nullptr;
    int font_width = 0;
    int font_height = 0;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
    static_cast<void>(font_pixels);

    ImGui::NewFrame();
    std::array<char, 256> buffer{};
    constexpr std::string_view kSource = "rx:(name (seq \"foo\" digit))";
    std::memcpy(buffer.data(), kSource.data(), kSource.size());
    buffer.at(kSource.size()) = '\0';
    ImGui::InputTextMultiline("##test_rx_multiline", buffer.data(), buffer.size(), ImVec2(400.0F, 90.0F));

    const ImGuiID widget_id = ImGui::GetID("##test_rx_multiline");
    const ImGuiContext* const ctx = ImGui::GetCurrentContext();
    REQUIRE(ctx != nullptr);

    const ImGuiWindow* child_window = nullptr;
    for (int i = 0; i < ctx->Windows.Size; ++i) {
      if (const ImGuiWindow* const w = ctx->Windows.Data[i];
          w != nullptr && w->ChildId == widget_id) {
        child_window = w;
        break;
      }
    }
    REQUIRE(child_window != nullptr);

    const int vtx_before = child_window->DrawList->VtxBuffer.Size;
    ui::RenderRxSyntaxOverlay("##test_rx_multiline", buffer.data(), true);
    const int vtx_after = child_window->DrawList->VtxBuffer.Size;

    // Syntax overlay must have added text vertices to the multiline child window's DrawList
    CHECK(vtx_after > vtx_before);

    ImGui::Render();
    ImGui::DestroyContext();
  }

}
