#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/IndexBuildState.h"

TEST_CASE("FormatInProgressStatus prefers USN whenever monitor is populating") {
  CHECK(index_build::FormatInProgressStatus("Folder: C:\\Users\\me", true) ==
        "Indexing USN Journal");
  CHECK(index_build::FormatInProgressStatus("", true) == "Indexing USN Journal");
  CHECK(index_build::FormatInProgressStatus("USN Journal", true) == "Indexing USN Journal");
  CHECK(index_build::FormatInProgressStatus("Folder: /home/user", true) ==
        "Indexing USN Journal");
}

TEST_CASE("FormatInProgressStatus uses source when USN is not populating") {
  CHECK(index_build::FormatInProgressStatus("Folder: /home/user", false) ==
        "Indexing Folder: /home/user");
  CHECK(index_build::FormatInProgressStatus("USN Journal", false) == "Indexing USN Journal");
  CHECK(index_build::FormatInProgressStatus("", false) == "Indexing index");
}
