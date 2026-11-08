---
description: Exception handling and error logging for production (try-catch, LOG_*_BUILD, (void)e)
globs: "**/*.cpp,**/*.h,**/*.hpp"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
---

# Exception handling and logging (production)

When editing C++ that can throw or catch, follow production checklist rules so Release builds stay clean and errors are diagnosable.

## Try-catch and logging

- Wrap critical code (file I/O, allocation, parsing) in try-catch.
- Log with `LOG_ERROR_BUILD` or `LOG_WARNING_BUILD` and include context (e.g. what failed, key values).
- Return safe defaults on error; avoid swallowing exceptions without logging.

## Unused exception variable in Release (void)e

In Release, `LOG_*_BUILD` macros are no-ops. If a catch block only uses `e` inside such a macro, the compiler warns that `e` is unused.

```cpp
// ❌ BAD — Release warning: unused 'e'
} catch (const std::exception& e) {
  LOG_ERROR_BUILD("Error: " << e.what());
}

// ✅ GOOD
} catch (const std::exception& e) {
  (void)e;
  LOG_ERROR_BUILD("Error: " << e.what());
}
```

Apply `(void)e;` (or `(void)ex;` etc.) in catch blocks whenever the exception is only used inside `LOG_*_BUILD`. Not needed for `LOG_ERROR`/`LOG_WARNING` (always active) or when the variable is used outside the macro.

**Reference:** `docs/plans/production/PRODUCTION_READINESS_CHECKLIST.md` (§ Quick Checklist, Phase 5, Common Issues).
