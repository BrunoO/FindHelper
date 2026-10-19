#pragma once

#include <cassert>
#include <utility>

#include "utils/ThreadUtils.h"

// A container that asserts (in debug builds) it is only mutated on the UI thread.
// Reads (const access) are allowed from any thread — render functions that take
// const GuiState& automatically use the const overload which does not assert.
//
// In release builds the assert is compiled away: zero overhead.
//
// Usage:
//   UIThreadOwned<std::vector<Foo>> vec;
//   *vec             // mutable reference — asserts IsUIThread()
//   vec->method()    // mutable call     — asserts IsUIThread()
//   *std::as_const(vec)  // const reference — no assert
//   (const context)  // const overloads called automatically
template <typename T>
class UIThreadOwned {
 public:
  [[nodiscard]] T& operator*() {
    assert(IsUIThread());
    return value_;
  }
  [[nodiscard]] const T& operator*() const { return value_; }
  [[nodiscard]] T* operator->() {
    assert(IsUIThread());
    return &value_;
  }
  [[nodiscard]] const T* operator->() const { return &value_; }

 private:
  T value_{};  // NOLINT(readability-identifier-naming)
};
