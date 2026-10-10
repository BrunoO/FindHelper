---
description: Prevent cross-thread lifetime and ownership bugs in async code
globs: "**/*.cpp,**/*.h,**/*.hpp,**/*.cc,**/*.cxx"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
  - "**/*.cc"
  - "**/*.cxx"
---

# Async Ownership and Lifetime

- Never return a raw pointer/reference to an object that can be reset on another thread.
- For cross-thread handoff, return a `std::shared_ptr<T>` snapshot (captured under lock), then use only that snapshot.
- Do not lock, fetch `.get()`, unlock, and use the raw pointer later.
- When exposing async collectors/services from worker state, ownership must be explicit in the API.

## Pattern

```cpp
// ❌ Unsafe: pointer may dangle after lock is released
T* Get() { std::scoped_lock l(m_); return ptr_.get(); }

// ✅ Safe: shared lifetime across caller usage window
std::shared_ptr<T> Get() { std::scoped_lock l(m_); return ptr_; }
```
