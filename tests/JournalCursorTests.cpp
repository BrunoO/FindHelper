#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdint>

#include "doctest/doctest.h"
#include "usn/JournalCursor.h"

using usn_journal::JournalCursor;

// Compile-time proof the VO is usable in constant expressions.
static_assert(JournalCursor{7U, 100, 50}.Advance(120).next_usn == 120,
              "Advance must be constexpr");
static_assert(!JournalCursor{7U, 100, 50}.IsWrappedBy(100),
              "equal position is still readable, not wrapped");
static_assert(JournalCursor{7U, 100, 50}.HasIdChanged(8U),
              "different journal id must report changed");

TEST_SUITE("JournalCursor - value semantics") {

  TEST_CASE("default cursor is the null position") {
    constexpr JournalCursor cursor{};
    CHECK(cursor.journal_id == 0U);
    CHECK(cursor.next_usn == 0);
    CHECK(cursor.lowest_valid_usn == 0);
  }

  TEST_CASE("equality is by value across all three fields") {
    CHECK(JournalCursor{1U, 100, 50} == JournalCursor{1U, 100, 50});
    CHECK(JournalCursor{1U, 100, 50} != JournalCursor{2U, 100, 50});
    CHECK(JournalCursor{1U, 100, 50} != JournalCursor{1U, 101, 50});
    CHECK(JournalCursor{1U, 100, 50} != JournalCursor{1U, 100, 51});
  }

  TEST_CASE("Advance replaces only the read position") {
    constexpr JournalCursor start{7U, 100, 50};
    constexpr JournalCursor advanced = start.Advance(120);
    CHECK(advanced.journal_id == 7U);
    CHECK(advanced.next_usn == 120);
    CHECK(advanced.lowest_valid_usn == 50);
    // Original is untouched (replace-not-mutate).
    CHECK(start.next_usn == 100);
  }

  TEST_CASE("Advance chains across buffers") {
    JournalCursor cursor{7U, 100, 50};
    cursor = cursor.Advance(120);
    cursor = cursor.Advance(140);
    CHECK(cursor == JournalCursor{7U, 140, 50});
  }
}

TEST_SUITE("JournalCursor - journal integrity predicates") {

  TEST_CASE("HasIdChanged detects journal delete/recreate") {
    constexpr JournalCursor cursor{7U, 100, 50};
    CHECK_FALSE(cursor.HasIdChanged(7U));
    CHECK(cursor.HasIdChanged(8U));
    CHECK(cursor.HasIdChanged(0U));
  }

  TEST_CASE("IsWrappedBy detects lost events") {
    constexpr JournalCursor cursor{7U, 200, 50};
    CHECK(cursor.IsWrappedBy(201));  // position predates oldest readable
    CHECK_FALSE(cursor.IsWrappedBy(200));  // equal: next_usn itself readable
    CHECK_FALSE(cursor.IsWrappedBy(199));
    CHECK_FALSE(cursor.IsWrappedBy(0));
  }

  TEST_CASE("fresh journal at zero is never wrapped") {
    constexpr JournalCursor cursor{};
    CHECK_FALSE(cursor.IsWrappedBy(0));
    CHECK_FALSE(cursor.HasIdChanged(0U));
  }
}
