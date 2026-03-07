#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <string>
#include <string_view>
#include <vector>
#include "TestHelpers.h"
#include "doctest/doctest.h"
#include "utils/StringUtils.h"

TEST_SUITE("StringUtils") {
  TEST_CASE("ToLower") {
    std::vector<test_helpers::StringUtilsTestCase> test_cases = {
      {"HELLO", "hello"},
      {"WORLD", "world"},
      {"TEST", "test"},
      {"ABC", "abc"},
      {"hello", "hello"},
      {"Hello", "hello"},
      {"WoRlD", "world"},
      {"HeLLo WoRLd", "hello world"},
      {"123", "123"},
      {"!@#", "!@#"},
      {"Hello123", "hello123"},
      {"test.txt", "test.txt"},
      {"", ""},
      {"  ", "  "},
      {"\t\n", "\t\n"},
      {"C:\\Users\\Test", "c:\\users\\test"},
      {"/path/to/file", "/path/to/file"}};

    test_helpers::RunParameterizedStringTests(ToLower, test_cases);
  }

  TEST_CASE("Trim") {
    std::vector<test_helpers::StringUtilsTestCase> test_cases = {
      {"hello", "hello"},
      {"  hello", "hello"},
      {"hello  ", "hello"},
      {"  hello  ", "hello"},
      {"\thello", "hello"},
      {"hello\t", "hello"},
      {"\thello\t", "hello"},
      {" \t hello \t ", "hello"},
      {"   ", ""},
      {"\t\t", ""},
      {"", ""},
      {"  hello world  ", "hello world"},
      {"\ttest string\n", "test string\n"}  // Trim only spaces and tabs
    };

    test_helpers::RunParameterizedStringTests(Trim, test_cases);
  }

  TEST_CASE("GetFilename") {
    std::vector<test_helpers::StringUtilsTestCase> test_cases = {
      {"C:\\Users\\Test\\file.txt", "file.txt"},
      {"C:\\file.txt", "file.txt"},
      {"\\file.txt", "file.txt"},
      {"/home/user/file.txt", "file.txt"},
      {"/file.txt", "file.txt"},
      {"file.txt", "file.txt"},
      {"C:\\Users/Test\\file.txt", "file.txt"},
      {"/path\\to/file.txt", "file.txt"},
      {"C:\\Users\\Test\\", ""},
      {"/home/user/", ""},
      {"C:\\", ""},
      {"/", ""},
      {"", ""},
      {R"(C:\\Users\\Test\\file.txt)", "file.txt"},
      {"//path//to//file.txt", "file.txt"},
      {".gitignore", ".gitignore"},
      {"/home/user/.gitignore", ".gitignore"}};

    test_helpers::RunParameterizedStringTests(GetFilename, test_cases);
  }

  TEST_CASE("GetExtension") {
    std::vector<test_helpers::StringUtilsTestCase> test_cases = {{"file.txt", "txt"},
                                                                 {"document.doc", "doc"},
                                                                 {"image.png", "png"},
                                                                 {"archive.zip", "zip"},
                                                                 {"file.backup.txt", "txt"},
                                                                 {"archive.tar.gz", "gz"},
                                                                 {"test.1.2.3", "3"},
                                                                 {"file", ""},
                                                                 {".gitignore", "gitignore"},
                                                                 {".hidden", "hidden"},
                                                                 {"file.", ""},
                                                                 {"", ""},
                                                                 {"C:\\Users\\file.txt", "txt"},
                                                                 {"file.TXT", "TXT"}};

    test_helpers::RunParameterizedStringTests(GetExtension, test_cases);
  }

  TEST_CASE("ParseExtensions") {
    std::vector<test_helpers::StringUtilsVectorTestCase> test_cases = {
      {"txt", {"txt"}},
      {"txt;doc;pdf", {"txt", "doc", "pdf"}},
      {".txt;.doc;.pdf", {"txt", "doc", "pdf"}},
      {" txt ; doc ; pdf ", {"txt", "doc", "pdf"}},
      {"", {}},
      {"   ;  ;  ", {}},
      {"TXT;DOC;PDF", {"txt", "doc", "pdf"}},
      {"txt;doc;", {"txt", "doc"}},
      {";txt;doc", {"txt", "doc"}},
      {"txt;;doc", {"txt", "doc"}},
      {";", {}},
      {"tar.gz;backup.txt", {"tar.gz", "backup.txt"}}};

    // Helper lambda to adapt ParseExtensions with default delimiter for the test helper
    auto ParseExtensionsDefault = [](std::string_view input) { return ParseExtensions(input); };

    test_helpers::RunParameterizedVectorTests(ParseExtensionsDefault, test_cases);
  }

  TEST_CASE("ParseExtensionsWithDelimiter") {
    // Helper lambda to adapt ParseExtensions with comma delimiter for the test helper
    auto ParseExtensionsComma = [](std::string_view input) { return ParseExtensions(input, ','); };

    std::vector<test_helpers::StringUtilsVectorTestCase> test_cases = {
      {"txt,doc,pdf", {"txt", "doc", "pdf"}},
      {"txt, doc, pdf", {"txt", "doc", "pdf"}},
      {".txt,.doc,.pdf", {"txt", "doc", "pdf"}},
      {"", {}},
      {",", {}},
      {"txt,,doc", {"txt", "doc"}},
      {"cpp, h, hpp", {"cpp", "h", "hpp"}}};

    test_helpers::RunParameterizedVectorTests(ParseExtensionsComma, test_cases);
  }

  TEST_CASE("FormatNumberWithSeparators") {
    struct TestCase {
      size_t input;
      std::string expected;
    };

    std::vector<TestCase> cases = {{0, "0"},
                                   {1, "1"},
                                   {12, "12"},
                                   {123, "123"},
                                   {1234, "1,234"},
                                   {12345, "12,345"},
                                   {123456, "123,456"},
                                   {1234567, "1,234,567"},
                                   {1000000000, "1,000,000,000"}};

    for (const auto& tc : cases) {
      CHECK_EQ(FormatNumberWithSeparators(tc.input), tc.expected);
    }
  }
}
