#pragma once

#include <array>
#include <cstring>
#include <string>
#include <string_view>

/**
 * @file SearchInputField.h
 * @brief Encapsulated input field for search queries
 *
 * This class addresses the "Primitive Obsession" code smell by encapsulating
 * raw char[256] arrays with proper validation and safe operations.
 *
 * DESIGN:
 * - Maintains ImGui compatibility via Data() method
 * - Safe string operations with automatic truncation
 * - Clear API for common operations (IsEmpty, Clear, SetValue)
 * - Buffer size is encapsulated (no magic numbers in client code)
 *
 * IMGUI COMPATIBILITY:
 * - Data() returns char* for ImGui::InputText()
 * - MaxLength() returns size for IM_ARRAYSIZE()
 * - Buffer is always null-terminated
 */

class SearchInputField {
public:
  static constexpr size_t kMaxLength = 256;

private:
  std::array<char, kMaxLength> buffer_{};  // NOLINT(readability-identifier-naming) - trailing _ per project; .data() for ImGui::InputText

public:
  /** Default constructor - initializes with empty string */
  SearchInputField() = default;

  /**
   * Get mutable pointer to buffer for ImGui::InputText()
   * This allows direct modification by ImGui while maintaining encapsulation
   */
  char* Data() { return buffer_.data(); }

  /**
   * Get const pointer to buffer for read-only access
   */
  [[nodiscard]] const char* Data() const { return buffer_.data(); }

  /**
   * Get maximum length (for IM_ARRAYSIZE compatibility)
   */
  static constexpr size_t MaxLength() { return kMaxLength; }

  /**
   * Check if field is empty
   */
  [[nodiscard]] bool IsEmpty() const {
    return buffer_[0] == '\0';
  }

  /**
   * Get string view of current value. View refers to internal buffer_ (stable storage); valid for lifetime of this object.
   */
  [[nodiscard]] std::string_view AsView() const {
    return {buffer_.data()};
  }

  /**
   * Get string copy of current value
   */
  [[nodiscard]] std::string AsString() const {
    return {buffer_.data()};
  }

  /**
   * Clear the field (set to empty string)
   */
  void Clear() {
    buffer_[0] = '\0';
  }

  /**
   * Set value with safe truncation
   * If value exceeds max length, it will be truncated to fit
   * 
   * @param value The string to set (will be truncated if too long)
   */
  void SetValue(std::string_view value) {
    const size_t copy_len = (value.length() < kMaxLength - 1) ? value.length() : kMaxLength - 1;
    std::memcpy(buffer_.data(), value.data(), copy_len);
    buffer_.at(copy_len) = '\0';
  }


  /**
   * Set value from const char* (convenience overload)
   */
  void SetValue(const char* value) {
    if (value == nullptr) {
      Clear();
      return;
    }
    SetValue(std::string_view(value));
  }

  /**
   * Implicit conversion to std::string for convenient interop with existing APIs.
   * Used in SearchController::BuildSearchParams() and other call sites that expect std::string.
   */
  operator std::string() const {  // NOLINT(google-explicit-constructor,hicpp-explicit-conversions) NOSONAR(cpp:S1709) - Implicit conversion intentionally used throughout codebase
    return AsString();
  }

  /**
   * Comparison operator
   */
  bool operator==(const SearchInputField& other) const {  // NOSONAR(cpp:S2807) - Member operator is acceptable; could be refactored to hidden friend later if needed
    return std::strcmp(buffer_.data(), other.buffer_.data()) == 0;
  }

  /**
   * Comparison with std::string
   */
  bool operator==(std::string_view other) const {  // NOSONAR(cpp:S2807) - Member operator is acceptable; could be refactored to hidden friend later if needed
    return AsString() == other;
  }

  /**
   * Comparison with const char*
   */
  bool operator==(const char* other) const {  // NOSONAR(cpp:S2807) - Member operator is acceptable; could be refactored to hidden friend later if needed
    if (other == nullptr) {
      return IsEmpty();
    }
    return std::strcmp(buffer_.data(), other) == 0;
  }
};

// Note: IM_ARRAYSIZE does NOT work with SearchInputField::Data()
// because Data() returns a pointer, not an array.
// Use SearchInputField::MaxLength() instead:
//   ImGui::InputText(id, field.Data(), SearchInputField::MaxLength(), flags);

