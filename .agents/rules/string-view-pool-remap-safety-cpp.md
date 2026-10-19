---
description: Guard string_view/pool remap and lock-escaping view APIs
globs: "**/*.cpp,**/*.h,**/*.hpp,**/*.cc,**/*.cxx"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
  - "**/*.cc"
  - "**/*.cxx"
---

# string_view and Pool Remap Safety

- Do not return `std::string_view` into mutable/locked internal storage unless lifetime is guaranteed externally.
- Prefer copy-return APIs (`std::string`) for cross-thread error/status access.
- During pool migration/remap, validate pointer range membership before offset math.
- Never assume `view.data()` belongs to a specific buffer without checks.
- On failed remap validation, use a safe fallback (clear view, skip item, log warning).

## Pattern

```cpp
// ✅ Validate before remap
if (view_data < old_data || view_data >= old_data + old_size) return false;
const size_t offset = static_cast<size_t>(view_data - old_data);
if (offset + view.size() > new_size) return false;
```
