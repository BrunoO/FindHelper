#include <string>
#include <string_view>

#include "doctest/doctest.h"
#include "gui/PatternDiagnostics.h"
#include "gui/SearchPipelineState.h"
#include "search/SearchPatternUtils.h"
#include "search/SearchPrefixes.h"
#include "search/rx/RxCompiler.h"
#include "search/rx/RxFormatter.h"
#include "ui/EmbeddedRxTutorial.h"

namespace file_operations {
void OpenFileDefault(std::string_view /*full_path*/) {}
}  // namespace file_operations

TEST_SUITE("RxCompiler - S-Expression Parser & Lexer") {

  TEST_CASE("wildcard aliases: any-char, any, anything") {
    const rx::RxCompileResult res_any = rx::CompileRx("(seq (+ any) (repeat 3 digit) (+ any))");
    CHECK(res_any.Ok());
    CHECK(res_any.regex == ".+[0-9]{3}.+");

    const rx::RxCompileResult res_anything = rx::CompileRx("(seq (* anything) (repeat 3 digit) (* anything))");
    CHECK(res_anything.Ok());
    CHECK(res_anything.regex == ".*[0-9]{3}.*");

    // In (name ...) wildcard must not cross path separator
    const rx::RxCompileResult res_name = rx::CompileRx("(name (seq (+ any) (repeat 3 digit) (+ any)))");
    CHECK(res_name.Ok());
    CHECK(res_name.regex == "(?:^|[/\\\\])[^/\\\\]+[0-9]{3}[^/\\\\]+$");

    const auto matcher = search_pattern_utils::CreateFilenameMatcher("rx:(name (seq (+ any) (repeat 3 digit) (+ any)))", false);
    CHECK(matcher("report123.pdf"));
    CHECK(matcher("my_file_456_final.txt"));
    CHECK_FALSE(matcher("file123"));      // no chars after 123
    CHECK_FALSE(matcher("123.txt"));      // no chars before 123

    const auto star_matcher = search_pattern_utils::CreateFilenameMatcher("rx:(name (seq (* any) (repeat 3 digit) (* any)))", false);
    CHECK(star_matcher("report123.pdf"));
    CHECK(star_matcher("file123"));
    CHECK(star_matcher("123.txt"));
    CHECK(star_matcher("123"));
  }

  TEST_CASE("empty and whitespace-only queries") {
    const rx::RxCompileResult empty_res = rx::CompileRx("");
    CHECK(empty_res.Ok());
    CHECK(empty_res.IsEmptyQuery());
    CHECK(empty_res.regex.empty());

    const rx::RxCompileResult ws_res = rx::CompileRx("   \t\n  ");
    CHECK(ws_res.Ok());
    CHECK(ws_res.IsEmptyQuery());
  }

  TEST_CASE("bare words and quoted strings") {
    const rx::RxCompileResult bare = rx::CompileRx("(seq hello world)");
    CHECK(bare.Ok());
    CHECK(bare.regex == "helloworld");

    const rx::RxCompileResult quoted = rx::CompileRx("(seq \"hello world\")");
    CHECK(quoted.Ok());
    CHECK(quoted.regex == "hello world");
  }

  TEST_CASE("parenthesis matching and error diagnostics") {
    const rx::RxCompileResult unclosed = rx::CompileRx("(seq a b");
    CHECK_FALSE(unclosed.Ok());
    CHECK(unclosed.IsIncomplete());
    CHECK(unclosed.error == rx::RxError::kUnclosedParen);
    CHECK(unclosed.open_paren_count == 1);

    const rx::RxCompileResult deep_unclosed = rx::CompileRx("(seq (or (path (dir a");
    CHECK_FALSE(deep_unclosed.Ok());
    CHECK(deep_unclosed.IsIncomplete());
    CHECK(deep_unclosed.open_paren_count == 4);

    const rx::RxCompileResult unexpected_close = rx::CompileRx("(seq a b))");
    CHECK_FALSE(unexpected_close.Ok());
    CHECK(unexpected_close.error == rx::RxError::kUnexpectedCloseParen);

    const rx::RxCompileResult unterminated_str = rx::CompileRx("(seq \"unterminated)");
    CHECK_FALSE(unterminated_str.Ok());
    CHECK(unterminated_str.error == rx::RxError::kUnterminatedString);

    const rx::RxCompileResult empty_list = rx::CompileRx("()");
    CHECK_FALSE(empty_list.Ok());
    CHECK(empty_list.error == rx::RxError::kEmptyList);
  }

  TEST_CASE("unknown operator suggestions") {
    const rx::RxCompileResult unknown = rx::CompileRx("(dire src)");
    CHECK_FALSE(unknown.Ok());
    CHECK(unknown.error == rx::RxError::kUnknownOperator);
    CHECK(unknown.message.find("did you mean 'dir'?") != std::string::npos);

    const rx::RxCompileResult unknown_seq = rx::CompileRx("(sequ a b)");
    CHECK_FALSE(unknown_seq.Ok());
    CHECK(unknown_seq.error == rx::RxError::kUnknownOperator);
    CHECK(unknown_seq.message.find("did you mean 'seq'?") != std::string::npos);
  }
}

TEST_SUITE("RxCompiler - Regex Lowering & Escaping") {

  TEST_CASE("metacharacter escaping") {
    // Dots, brackets, parens, asterisks in bare words must be escaped
    const rx::RxCompileResult res = rx::CompileRx("(seq foo.bar [baz] *test*)");
    CHECK(res.Ok());
    CHECK(res.regex == "foo\\.bar\\[baz\\]\\*test\\*");
  }

  TEST_CASE("sequence aliases") {
    const rx::RxCompileResult seq_res = rx::CompileRx("(seq a b c)");
    const rx::RxCompileResult colon_res = rx::CompileRx("(: a b c)");
    const rx::RxCompileResult and_res = rx::CompileRx("(and a b c)");

    CHECK(seq_res.Ok());
    CHECK(colon_res.Ok());
    CHECK(and_res.Ok());
    CHECK(seq_res.regex == "abc");
    CHECK(colon_res.regex == "abc");
    CHECK(and_res.regex == "abc");
  }

  TEST_CASE("alternatives (or, |)") {
    const rx::RxCompileResult or_res = rx::CompileRx("(or cpp h hpp)");
    const rx::RxCompileResult pipe_res = rx::CompileRx("(| cpp h hpp)");

    CHECK(or_res.Ok());
    CHECK(pipe_res.Ok());
    CHECK(or_res.regex == "(?:cpp|h|hpp)");
    CHECK(pipe_res.regex == "(?:cpp|h|hpp)");
  }

  TEST_CASE("quantifiers (*, +, ?, repeat)") {
    const rx::RxCompileResult star = rx::CompileRx("(* a)");
    CHECK(star.Ok());
    CHECK(star.regex == "a*");

    const rx::RxCompileResult plus = rx::CompileRx("(+ (or a b))");
    CHECK(plus.Ok());
    CHECK(plus.regex == "(?:a|b)+");

    const rx::RxCompileResult opt = rx::CompileRx("(? \"word\")");
    CHECK(opt.Ok());
    CHECK(opt.regex == "(?:word)?");

    const rx::RxCompileResult repeat_exact = rx::CompileRx("(repeat 3 digit)");
    CHECK(repeat_exact.Ok());
    CHECK(repeat_exact.regex == "[0-9]{3}");

    const rx::RxCompileResult repeat_range = rx::CompileRx("(repeat 2 4 alpha)");
    CHECK(repeat_range.Ok());
    CHECK(repeat_range.regex == "[a-zA-Z]{2,4}");
  }

  TEST_CASE("named character classes and anchors") {
    const rx::RxCompileResult classes = rx::CompileRx("(seq digit alpha alnum space blank xdigit any-char)");
    CHECK(classes.Ok());
    CHECK(classes.regex == "[0-9][a-zA-Z][a-zA-Z0-9][ \\t\\r\\n\\v\\f][ \\t][0-9a-fA-F].");

    const rx::RxCompileResult anchors = rx::CompileRx("(seq bos \"foo\" word-boundary eos)");
    CHECK(anchors.Ok());
    CHECK(anchors.regex == "^foo\\b$");
  }

  TEST_CASE("any character class and character negation") {
    const rx::RxCompileResult any_res = rx::CompileRx("(any \"a-z\" \"0-9\" \"_\")");
    CHECK(any_res.Ok());
    CHECK(any_res.regex == "[a-z0-9_]");

    const rx::RxCompileResult not_digit = rx::CompileRx("(not digit)");
    CHECK(not_digit.Ok());
    CHECK(not_digit.regex == "[^0-9]");

    const rx::RxCompileResult not_any = rx::CompileRx("(not (any \"0-9\"))");
    CHECK(not_any.Ok());
    CHECK(not_any.regex == "[^0-9]");
  }
}

TEST_SUITE("RxCompiler - Path Extensions") {

  TEST_CASE("cross-platform directory matching") {
    const rx::RxCompileResult dir = rx::CompileRx("(dir src)");
    CHECK(dir.Ok());
    // Must match either forward or backslash
    CHECK(dir.regex.find("[/\\\\]") != std::string::npos);
  }

  TEST_CASE("extensions clause (ext)") {
    const rx::RxCompileResult ext_single = rx::CompileRx("(ext cpp)");
    CHECK(ext_single.Ok());
    CHECK(ext_single.regex == "\\.cpp$");

    const rx::RxCompileResult ext_multi = rx::CompileRx("(ext cpp h hpp)");
    CHECK(ext_multi.Ok());
    CHECK(ext_multi.regex == "\\.(cpp|h|hpp)$");

    // Leading dots are cleaned automatically
    const rx::RxCompileResult ext_dots = rx::CompileRx("(ext \".cpp\" \".h\")");
    CHECK(ext_dots.Ok());
    CHECK(ext_dots.regex == "\\.(cpp|h)$");
  }

  TEST_CASE("full path composition pipeline") {
    const rx::RxCompileResult path_res = rx::CompileRx(
        "(path (exclude-dir build node_modules) (dir src) (name test_run) (ext cpp))");
    CHECK(path_res.Ok());
    // Exclusions lookahead must be at the very front
    CHECK(path_res.regex.rfind("^(?!.*", 0) == 0);
    // Contains exclude dirs
    CHECK(path_res.regex.find("build|node_modules") != std::string::npos);
    // Contains dir src
    CHECK(path_res.regex.find("src") != std::string::npos);
    // Ends with test_run\.cpp$
    CHECK(path_res.regex.find("test_run\\.cpp$") != std::string::npos);
  }
}

TEST_SUITE("RxCompiler - Prefilter Harvesting") {

  TEST_CASE("longest required literal extraction") {
    const rx::RxCompileResult res1 = rx::CompileRx("(seq \"pre_\" (+ digit) \"_middlelongliteral_\" (? \"post\"))");
    CHECK(res1.Ok());
    CHECK(res1.required_literal == "_middlelongliteral_");

    // Inside (dir ...) single folder is mandatory
    const rx::RxCompileResult res2 = rx::CompileRx("(path (dir \"project_alpha\") (ext cpp))");
    CHECK(res2.Ok());
    CHECK(res2.required_literal == "project_alpha");

    // Shorter than 3 chars discarded
    const rx::RxCompileResult res3 = rx::CompileRx("(seq \"ab\" digit)");
    CHECK(res3.Ok());
    CHECK(res3.required_literal.empty());
  }

  TEST_CASE("pruning branches with no guaranteed literal") {
    // In (or a b), neither branch is guaranteed
    const rx::RxCompileResult or_res = rx::CompileRx("(or \"alpha_very_long\" \"beta_very_long\")");
    CHECK(or_res.Ok());
    CHECK(or_res.required_literal.empty());

    // In (* foo), foo is not guaranteed (0 occurrences)
    const rx::RxCompileResult star_res = rx::CompileRx("(* \"alpha_very_long\")");
    CHECK(star_res.Ok());
    CHECK(star_res.required_literal.empty());

    // In (exclude-dir foo), foo must NOT appear
    const rx::RxCompileResult excl_res = rx::CompileRx("(exclude-dir \"excluded_folder\")");
    CHECK(excl_res.Ok());
    CHECK(excl_res.required_literal.empty());
  }
}

TEST_SUITE("RxCompiler - Safety & ReDoS Guardrails") {

  TEST_CASE("nested quantifiers rejection") {
    const rx::RxCompileResult star_plus = rx::CompileRx("(* (+ a))");
    CHECK_FALSE(star_plus.Ok());
    CHECK(star_plus.error == rx::RxError::kNestedQuantifier);

    const rx::RxCompileResult plus_star = rx::CompileRx("(+ (* a))");
    CHECK_FALSE(plus_star.Ok());
    CHECK(plus_star.error == rx::RxError::kNestedQuantifier);

    const rx::RxCompileResult opt_star = rx::CompileRx("(? (* a))");
    CHECK_FALSE(opt_star.Ok());
    CHECK(opt_star.error == rx::RxError::kNestedQuantifier);
  }

  TEST_CASE("nesting depth limit (32)") {
    std::string deep = "(seq ";
    for (int i = 0; i < 35; ++i) {
      deep += "(: ";
    }
    deep += 'a';
    for (int i = 0; i < 36; ++i) {
      deep += ')';
    }

    const rx::RxCompileResult deep_res = rx::CompileRx(deep);
    CHECK_FALSE(deep_res.Ok());
    CHECK(deep_res.error == rx::RxError::kNestingTooDeep);
  }

  TEST_CASE("source length limit (1000)") {
    std::string long_src = "(seq \"";
    long_src.append(1010, 'a');
    long_src += "\")";

    const rx::RxCompileResult long_res = rx::CompileRx(long_src);
    CHECK_FALSE(long_res.Ok());
    CHECK(long_res.error == rx::RxError::kPatternTooLong);
  }

  TEST_CASE("general lookahead negation rejected in V1") {
    const rx::RxCompileResult not_seq = rx::CompileRx("(not (seq a b))");
    CHECK_FALSE(not_seq.Ok());
    CHECK(not_seq.error == rx::RxError::kGeneralNegationNotSupported);
  }

  TEST_CASE("repeat range validation") {
    const rx::RxCompileResult inverted = rx::CompileRx("(repeat 5 2 a)");
    CHECK_FALSE(inverted.Ok());
    CHECK(inverted.error == rx::RxError::kInvalidRange);

    const rx::RxCompileResult overflow = rx::CompileRx("(repeat 20000 a)");
    CHECK_FALSE(overflow.Ok());
    CHECK(overflow.error == rx::RxError::kInvalidRange);
  }
}

TEST_SUITE("RxCompiler - Integration with PatternMatcher") {

  TEST_CASE("matching paths end-to-end") {
    const std::string query = "rx:(path (exclude-dir build node_modules) (dir src) (ext cpp h))";
    const auto matcher = search_pattern_utils::CreatePathMatcher(query, false);
    CHECK(static_cast<bool>(matcher));

    // Matching cases
    CHECK(matcher("C:\\dev\\my_project\\src\\main.cpp"));
    CHECK(matcher("/home/user/project/src/core/utils.h"));

    // Excluded directory
    CHECK_FALSE(matcher("C:\\dev\\build\\src\\main.cpp"));
    CHECK_FALSE(matcher("/home/user/node_modules/pkg/src/index.h"));

    // Missing required dir
    CHECK_FALSE(matcher("C:\\dev\\my_project\\lib\\main.cpp"));

    // Wrong extension
    CHECK_FALSE(matcher("C:\\dev\\my_project\\src\\main.py"));
  }

  TEST_CASE("case sensitivity toggle") {
    const std::string query = "rx:(seq \"CMakeLists.txt\")";
    const auto case_sensitive_matcher = search_pattern_utils::CreateFilenameMatcher(query, true);
    const auto case_insensitive_matcher = search_pattern_utils::CreateFilenameMatcher(query, false);

    CHECK(case_sensitive_matcher("CMakeLists.txt"));
    CHECK_FALSE(case_sensitive_matcher("cmakelists.txt"));

    CHECK(case_insensitive_matcher("CMakeLists.txt"));
    CHECK(case_insensitive_matcher("cmakelists.txt"));
  }

  TEST_CASE("bare prefix and empty rx") {
    const auto matcher_bare = search_pattern_utils::CreatePathMatcher("rx:", false);
    CHECK(matcher_bare("any/path/passes"));

    const auto matcher_empty = search_pattern_utils::CreatePathMatcher("rx:   ", false);
    CHECK(matcher_empty("any/path/passes"));
  }

  TEST_CASE("PatternDiagnostics reporting") {
    SearchPipelineState pipeline;

    // Valid query clears error
    UpdatePatternDiagnostics("rx:(seq abc)", "", false, pipeline);
    CHECK(pipeline.pattern_error.empty());

    // Broken query surfaces diagnostic
    UpdatePatternDiagnostics("rx:(dire invalid)", "", false, pipeline);
    CHECK_FALSE(pipeline.pattern_error.empty());
    CHECK(pipeline.pattern_error.find("Name pattern: unknown operator 'dire'") != std::string::npos);

    // Unclosed paren surfaces diagnostic
    UpdatePatternDiagnostics("", "rx:(seq unclosed", false, pipeline);
    CHECK_FALSE(pipeline.pattern_error.empty());
    CHECK(pipeline.pattern_error.find("Path pattern: missing ')'") != std::string::npos);
  }

  TEST_CASE("EmbeddedRxTutorial verification and export") {
    const std::string_view html = ui::GetEmbeddedRxTutorialHtml();
    CHECK_FALSE(html.empty());
    CHECK(html.find("<!DOCTYPE html>") != std::string_view::npos);
    CHECK(html.find("Tutorial: Structured Regex (rx:) in FindHelper") != std::string_view::npos);
    CHECK(html.find("(exclude-dir build node_modules)") != std::string_view::npos);
    CHECK(html.find("Structural Anatomy &amp; Grammar") != std::string_view::npos);
    CHECK(html.find("Key Structural Rules") != std::string_view::npos);
    CHECK(html.find("Clause Head Disambiguation") != std::string_view::npos);
    CHECK(html.find("flowchart TD") != std::string_view::npos);

    const std::string temp_file = ui::WriteEmbeddedRxTutorialToTempFile();
    CHECK_FALSE(temp_file.empty());
    CHECK(temp_file.find("rx_pattern_tutorial.html") != std::string::npos);

    // Verify opener callback hook
    static bool s_opened = false;
    s_opened = false;
    ui::OpenEmbeddedRxTutorialInBrowser([](std::string_view path) {
      s_opened = true;
      CHECK(path.find("rx_pattern_tutorial.html") != std::string_view::npos);
    });
    CHECK(s_opened);

    // Validate that every single example and recipe in the tutorial compiles cleanly
    const std::vector<std::string> valid_tutorial_snippets = {
        "(path (exclude-dir build node_modules) (dir src) (name (seq test_ (+ alnum))) (ext cpp h))",
        "(dir \"Program Files\")",
        "(seq CMakeLists.txt)",
        "(seq hello world)",
        "(or cpp h hpp)",
        "(* digit)",
        "(+ alpha)",
        "(? \"_backup\")",
        "(repeat 4 digit)",
        "(repeat 2 4 xdigit)",
        "(any \"a-z\" \"0-9\" \"_\")",
        "(not digit)",
        "(not (any \"aeiou\"))",
        "(dir src)",
        "(dir src include)",
        "(exclude-dir build .git)",
        "(name (seq test_ (* alnum)))",
        "(ext cpp h)",
        "(path (dir include) (ext h hpp))",
        "(path (exclude-dir build target node_modules .git) (name (seq config (* any))))",
        "(seq \"server_\" (repeat 4 digit) \".log\")",
        "(path (exclude-dir node_modules vendor .terraform) (name (or \".env\" (seq \".env.\" (or \"local\" \"development\" \"staging\" \"prod\")))))",
        "(path (exclude-dir .git) (name (or (seq (+ any) \".orig\") (seq (+ any) \".rej\") (seq (+ any) (or \"_BACKUP_\" \"_BASE_\") (+ digit)))))",
        "(name (seq (or \"IMG_\" \"PHOTO_\" \"VID_\") (repeat 4 digit) (? \"-\") (repeat 2 digit) (? \"-\") (repeat 2 digit) (+ any)))",
        "(name (seq (? \"v\") (repeat 1 3 digit) \".\" (repeat 1 3 digit) \".\" (repeat 1 3 digit) (or \".tar.gz\" \".zip\" \".tgz\" (seq \"-\" alnum (* alnum) (or \".tar.gz\" \".zip\" \".tgz\")))))",
        "(name (seq word-boundary \"Engine\" word-boundary))",
    };

    for (const auto& snippet : valid_tutorial_snippets) {
      const rx::RxCompileResult res = rx::CompileRx(snippet);
      INFO("Checking tutorial snippet: " << snippet);
      CHECK(res.Ok());
      CHECK_FALSE(res.regex.empty());
    }

    // Recipe 9 typo verification
    const rx::RxCompileResult typo_res = rx::CompileRx("(dire src)");
    CHECK_FALSE(typo_res.Ok());
    CHECK(typo_res.message.find("did you mean 'dir'?") != std::string::npos);
  }

  TEST_CASE("FormatRx S-expression pretty-printing") {
    // Empty / whitespace
    CHECK(rx::FormatRx("").empty());
    CHECK(rx::FormatRx("   ") == "   ");

    // Single-line expression stays compact
    const std::string single = "rx:(seq (+ any) (repeat 3 digit))";
    CHECK(rx::FormatRx(single) == single);

    // Multi-clause path expression indents clauses nicely
    const std::string compound = "rx:(path (exclude-dir \"node_modules\" \".git\") (dir \"src\") (name (seq (+ any) (repeat 3 digit))))";
    const std::string formatted = rx::FormatRx(compound);
    CHECK(formatted.find("rx:(path\n  (exclude-dir \"node_modules\" \".git\")\n  (dir \"src\")\n  (name") != std::string::npos);

    // Preserves strings with escapes and spaces
    const std::string with_quotes = "rx:(path (dir \"my folder/with space\"))";
    const std::string formatted_quotes = rx::FormatRx(with_quotes);
    CHECK(formatted_quotes.find("\"my folder/with space\"") != std::string::npos);
  }
}

TEST_SUITE("RxCompiler - Edge Cases (added)") {

  TEST_CASE("bare atom inside (path ...) reports expected-operator error") {
    const rx::RxCompileResult res = rx::CompileRx("(path foo)");
    CHECK_FALSE(res.Ok());
    CHECK(res.error == rx::RxError::kInvalidArgument);
    CHECK(res.message.find("expected a clause operator") != std::string::npos);
  }

  TEST_CASE("empty extension is rejected") {
    const rx::RxCompileResult res = rx::CompileRx("(ext \"\")");
    CHECK_FALSE(res.Ok());
    CHECK(res.error == rx::RxError::kInvalidArgument);
    CHECK(res.message.find("empty name") != std::string::npos);
  }

  TEST_CASE("exclude-only path filters correctly") {
    const auto matcher = search_pattern_utils::CreatePathMatcher("rx:(exclude-dir build)", false);
    CHECK(static_cast<bool>(matcher));
    CHECK_FALSE(matcher("C:\\build\\x.cpp"));
    CHECK(matcher("C:\\src\\x.cpp"));
  }

  TEST_CASE("unknown operator inside (path ...) suggests") {
    const rx::RxCompileResult res = rx::CompileRx("(path (frob src))");
    CHECK_FALSE(res.Ok());
    CHECK(res.error == rx::RxError::kUnknownOperator);
  }
}
