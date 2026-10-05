#pragma once

// Normalized bare file name (V5 slice): UTF-16 → UTF-8 converted and owned.
// Marks values intended as bare names (no parent chain), distinct from a
// full path (FilePath, future work) and from the raw journal name view
// (UsnRecord::name, valid only while the source buffer lives). Marking is
// by construction (explicit) — nothing validates bareness, so a full path
// still compiles if wrapped; the value is grepability and a landing site
// for future validation. Embedded-null truncation is applied at insert
// (IndexOperations::Insert), not here. Glossary term: FileName
// (docs/design/USN_MFT_UBIQUITOUS_LANGUAGE.md).
//
// Cross-platform (no Windows types); usable from tests.

#include <string>
#include <string_view>

namespace file_name {

class FileName {
 public:
  FileName() = default;
  explicit FileName(std::string_view name) : name_(name) {}

  // View into the owned name. Invalidated by any non-const operation on
  // this FileName (move, assign); do not hold across such operations.
  [[nodiscard]] std::string_view View() const noexcept { return {name_}; }
  [[nodiscard]] bool Empty() const noexcept { return name_.empty(); }

  [[nodiscard]] friend bool operator==(const FileName& a,
                                       const FileName& b) noexcept {
    return a.name_ == b.name_;
  }

  [[nodiscard]] friend bool operator!=(const FileName& a,
                                       const FileName& b) noexcept {
    return !(a == b);
  }

 private:
  std::string name_;
};

}  // namespace file_name
