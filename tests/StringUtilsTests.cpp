#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "TestHelpers.h"
#include "doctest/doctest.h"
#include "utils/StringUtils.h"

TEST_SUITE("StringUtils") {
  TEST_CASE("ToLower") {
    const std::vector<test_helpers::StringUtilsTestCase> test_cases = {
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
    const std::vector<test_helpers::StringUtilsTestCase> test_cases = {
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
    const std::vector<test_helpers::StringUtilsTestCase> test_cases = {
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
    const std::vector<test_helpers::StringUtilsTestCase> test_cases = {{"file.txt", "txt"},
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
    const std::vector<test_helpers::StringUtilsVectorTestCase> test_cases = {
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

    const std::vector<test_helpers::StringUtilsVectorTestCase> test_cases = {
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

    const std::vector<TestCase> cases = {{0, "0"},
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

  TEST_CASE("FormatMemory") {
    // Zero
    CHECK(FormatMemory(0) == "0 B");

    // Bytes range (< 1024)
    CHECK(FormatMemory(1) == "1 B");
    CHECK(FormatMemory(1023) == "1023 B");

    // KB range — rounds to nearest integer
    CHECK(FormatMemory(1024) == "1 KB");
    CHECK(FormatMemory(1535) == "1 KB");   // 1535+512=2047 → truncates to 1
    CHECK(FormatMemory(1536) == "2 KB");   // 1536+512=2048 → rounds up to 2
    CHECK(FormatMemory(1024 * 1024 - 1) == "1024 KB");

    // MB range — rounds to nearest integer
    CHECK(FormatMemory(1024 * 1024) == "1 MB");
    CHECK(FormatMemory(1024 * 1024 * 1024 - 1) == "1024 MB");

    // GB range — rounds to nearest integer
    CHECK(FormatMemory(1024ULL * 1024 * 1024) == "1 GB");
    CHECK(FormatMemory(2ULL * 1024 * 1024 * 1024) == "2 GB");
  }

  TEST_CASE("FormatMemoryOrNa") {
    // Zero maps to N/A
    CHECK(FormatMemoryOrNa(0) == "N/A");

    // Non-zero delegates to FormatMemory
    CHECK(FormatMemoryOrNa(1) == "1 B");
    CHECK(FormatMemoryOrNa(1024) == "1 KB");
    CHECK(FormatMemoryOrNa(1024 * 1024) == "1 MB");
  }

  TEST_CASE("TruncateAtEmbeddedNull strips suffix after first NUL") {
    std::string corrupted = "name";
    corrupted.push_back('\0');
    corrupted.append("tail");
    const std::string_view truncated = TruncateAtEmbeddedNull(corrupted);
    CHECK_EQ(truncated, "name");
    CHECK_EQ(truncated.size(), 4U);
  }

#ifndef _WIN32
  // POSIX stub coverage (StringUtils_posix.cpp): WideToUtf8 / Utf8ToWide return empty
  // on macOS/Linux; production code uses Windows implementations.
  TEST_CASE("WideToUtf8_PosixStub_ReturnsEmpty") {
    CHECK_EQ(WideToUtf8(std::wstring()), "");
    CHECK_EQ(WideToUtf8(L""), "");
    CHECK_EQ(WideToUtf8(L"hello"), "");
    CHECK_EQ(WideToUtf8(L"c:\\path\\file.txt"), "");
  }

  TEST_CASE("WideToUtf8Assign_PosixStub_ClearsOutput") {
    std::string out = "previous";
    WideToUtf8Assign(L"hello", 5, out);
    CHECK_EQ(out, "");
  }

  TEST_CASE("Utf8ToWide_PosixStub_ReturnsEmpty") {
    CHECK_EQ(Utf8ToWide(std::string_view()), std::wstring());
    CHECK_EQ(Utf8ToWide(""), std::wstring());
    CHECK_EQ(Utf8ToWide("hello"), std::wstring());
    CHECK_EQ(Utf8ToWide("/path/to/file"), std::wstring());
  }
#endif  // _WIN32

#ifdef _WIN32
  TEST_CASE("WideToUtf8Assign_ExcludesEmbeddedNullTerminator") {
    std::string out;
    WideToUtf8Assign(L"Documents", 9, out);
    CHECK_EQ(out, "Documents");
    CHECK_EQ(out.size(), 9U);
    CHECK(out.find('\0') == std::string::npos);
  }

  TEST_CASE("Utf8ToWide_ExcludesEmbeddedNullTerminator") {
    const std::wstring wide = Utf8ToWide("Documents");
    CHECK_EQ(wide, L"Documents");
    CHECK_EQ(wide.size(), 9U);
    CHECK(wide.find(L'\0') == std::wstring::npos);
  }
#endif  // _WIN32

#ifndef _WIN32
  // strcat_safe is an inline function on non-Windows; strcat_s macro on Windows.
  TEST_SUITE("strcat_safe") {

    TEST_CASE("appends to empty destination") {
      std::array<char, 16> buf{};
      buf[0] = '\0';
      strcat_safe(buf.data(), buf.size(), "hello");
      CHECK(std::string_view(buf.data()) == "hello");
    }

    TEST_CASE("appends to non-empty destination") {
      std::array<char, 16> buf{};
      strcpy_safe(buf.data(), buf.size(), "foo");
      strcat_safe(buf.data(), buf.size(), "bar");
      CHECK(std::string_view(buf.data()) == "foobar");
    }

    TEST_CASE("truncates when combined result would overflow") {
      std::array<char, 8> buf{};
      strcpy_safe(buf.data(), buf.size(), "abc");  // 3 chars + null
      strcat_safe(buf.data(), buf.size(), "defghij");  // would overflow
      // buf holds 8 bytes total; 3 already used, so 4 chars of src fit
      CHECK(std::strlen(buf.data()) == 7U);
      CHECK(buf[7] == '\0');  // always null-terminated
    }

    TEST_CASE("no-op when destination is already full") {
      std::array<char, 4> buf{};
      strcpy_safe(buf.data(), buf.size(), "abc");  // fills buffer (3 chars + null)
      strcat_safe(buf.data(), buf.size(), "xyz");  // no room
      CHECK(std::string_view(buf.data()) == "abc");
    }

    TEST_CASE("zero-size buffer is handled without crash") {
      std::array<char, 2> buf = {'x', '\0'};
      strcat_safe(buf.data(), 0, "y");  // must not touch buf
      CHECK(buf[0] == 'x');
    }

    TEST_CASE("appending empty string leaves destination unchanged") {
      std::array<char, 16> buf{};
      strcpy_safe(buf.data(), buf.size(), "hello");
      strcat_safe(buf.data(), buf.size(), "");
      CHECK(std::string_view(buf.data()) == "hello");
    }
  }
#endif  // _WIN32
}
