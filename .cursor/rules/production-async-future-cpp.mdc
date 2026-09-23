---
description: std::future and async resource cleanup to avoid leaks and stuck threads
globs: "**/*.cpp,**/*.h,**/*.hpp"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
---

# std::future / async resource cleanup (production)

When editing code that uses `std::future` or starts async work, ensure futures are cleaned up so they don’t leak or leave threads running.

## Rules

1. **Before starting a new async operation**  
   If an existing future is valid, wait for it and consume the result before replacing:
   - `if (future.valid()) { future.wait(); future.get(); }` (or `wait_for` then `get()`).
   - Then assign the new future.

2. **After getting the result**  
   Reset the future so resources are released:  
   `future = std::future<ResultType>();`

3. **In cleanup / destructors**  
   In `ClearInputs()`, destructors, or state reset: wait for any valid future and reset it so it doesn’t outlive the object.

## Pattern

```cpp
// Before starting new operation
if (state.apiFuture.valid()) {
  if (state.apiFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    state.apiFuture.wait();
  state.apiFuture.get();
}
state.apiFuture = StartAsyncOperation();

// After getting result
auto result = state.apiFuture.get();
state.apiFuture = std::future<ResultType>();
```

Never replace a valid future without waiting and calling `get()` first; abandoned futures leak memory and can keep threads running.

**Reference:** `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` (Quick “Async resource cleanup”, Phase 6).
