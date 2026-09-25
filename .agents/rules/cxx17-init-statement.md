---
description: C++17 init-statement pattern (if (init; condition)) and Sonar cpp:S6004
globs: "**/*.cpp,**/*.h,**/*.hpp"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
---

# C++17 init-statement pattern

Use init-statements in `if` when the variable is **only** used inside that `if` (or its `else`/`else if`). Reduces scope and satisfies Sonar cpp:S6004.

## Do

```cpp
// Variable only used inside the if
if (int width = std::atoi(value); width >= 640 && width <= 4096) {
  args.window_width_override = width;
}

if (auto it = map.find(key); it != map.end()) {
  use(*it);
}
```

## Don't

- Variable used **after** the `if`: keep declaration outside.
- Variable used in **multiple unrelated** `if`s: keep outside.
- **ImGui pattern:** `ImGui::SliderFloat(..., &var, ...)` then use `var` after the `if`—keep `var` outside (Sonar S6004 false positive).

## More examples

See `docs/standards/CXX17_INIT_STATEMENT_EXAMPLES.md` for extended examples, “don’t apply” cases, and real-world usage. Before applying, verify the variable is not used after the `if` block.
