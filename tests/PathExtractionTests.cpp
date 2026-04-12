#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "platform/FileOperations.h"

TEST_CASE("FileOperations::IsPathSafe") {
    // UNC paths (\\server\share) are rejected — could access network resources
    CHECK_FALSE(file_operations::internal::IsPathSafe("\\\\server\\share"));
    CHECK_FALSE(file_operations::internal::IsPathSafe("\\\\server\\share\\file.txt"));
    CHECK_FALSE(file_operations::internal::IsPathSafe("\\\\"));

    // Path traversal sequences are rejected
    CHECK_FALSE(file_operations::internal::IsPathSafe("C:\\dir\\..\\..\\file"));
    CHECK_FALSE(file_operations::internal::IsPathSafe("../etc/passwd"));

    // Embedded nulls are rejected
    const std::string path_with_null = std::string("C:\\valid") + '\0' + "\\file.txt";
    CHECK_FALSE(file_operations::internal::IsPathSafe(path_with_null));

    // Normal paths are accepted
    CHECK(file_operations::internal::IsPathSafe("C:\\Users\\file.txt"));
    CHECK(file_operations::internal::IsPathSafe("/usr/local/bin/tool"));

    // Dots inside names (not traversal) are accepted
    CHECK(file_operations::internal::IsPathSafe("C:\\dir.name\\file.txt"));
    CHECK(file_operations::internal::IsPathSafe("C:\\v1.0\\release.zip"));
}

TEST_CASE("FileOperations::ValidatePath Boundary Conditions") {
    SUBCASE("Empty paths are rejected") {
        CHECK_FALSE(file_operations::internal::ValidatePath("", "TestOperation"));
    }

    SUBCASE("Paths containing embedded null characters are rejected") {
        const std::string path_with_null = std::string("C:\\folder\\file") + '\0' + ".txt";
        CHECK_FALSE(file_operations::internal::ValidatePath(path_with_null, "TestOperation"));
    }

    SUBCASE("Standard paths are accepted") {
        CHECK(file_operations::internal::ValidatePath("C:\\valid\\path.txt", "TestOperation"));
    }
}
