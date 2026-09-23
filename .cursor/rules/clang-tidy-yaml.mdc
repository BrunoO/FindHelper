---
description: .clang-tidy YAML — no inline # in Checks or other values
globs: ".clang-tidy"
alwaysApply: false
paths:
  - ".clang-tidy"
---

# Modifying .clang-tidy safely

In YAML, `#` starts a comment; anything after it on that line is stripped.

## Do not

- Put `#` on the same line as a check name under `Checks: >-`. The check name would be removed and the check would stay enabled.
- Put inline `#` inside any YAML value (e.g. `CheckOptions:`).

## Do

- Put each check pattern on its own line under `Checks: >-` with no comment on that line.
- Document why a check is disabled in a **separate** comment block (e.g. "--- Disabled Checks Rationale ---") below the list.

After editing, run `clang-tidy --config-file=.clang-tidy --dump-config` and confirm `Checks:` contains the intended names.

Full rules: AGENTS.md § Modifying .clang-tidy Safely.
