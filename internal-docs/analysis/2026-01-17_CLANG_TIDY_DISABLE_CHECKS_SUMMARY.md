# Quick Summary: Proposed clang-tidy Check Disables

**Date:** 2026-01-17

## Quick Reference

### Checks to Disable (Add to `.clang-tidy` Checks section)

```yaml
-hicpp-uppercase-literal-suffix,              # 350 warnings - lowercase 'f' is project convention
-readability-uppercase-literal-suffix,         # Same as above
-llvm-prefer-static-over-anonymous-namespace,  # 114 warnings - anonymous namespaces are valid
-misc-use-internal-linkage,                    # 89 warnings - functions may be intentionally non-static
-readability-convert-member-functions-to-static, # 109 warnings - design choice to keep as members
-google-readability-namespace-comments,         # 13 warnings - style preference
-llvm-namespace-comment,                       # Same as above
-readability-redundant-inline-specifier,       # 12 warnings - minor issue
```

**Note:** `-fuchsia-*` is already disabled but still generating 262 warnings - needs investigation.

### Expected Impact

- **Warnings eliminated:** ~1,000 (31% reduction)
- **Remaining warnings:** ~2,184 (focused on real issues)
- **Biggest win:** Floating point suffix (350 warnings, zero functional impact)

### Checks to KEEP (Important)

- `cppcoreguidelines-init-variables` (uninitialized variables - 92 warnings)
- `misc-const-correctness` (const correctness - 134 warnings)
- `cppcoreguidelines-pro-type-member-init` (member initialization - 46 warnings)
- `cppcoreguidelines-avoid-c-arrays` (C-style arrays - 19 warnings)
- `bugprone-empty-catch` (empty catch blocks - 14 warnings)
- `readability-function-cognitive-complexity` (complexity - 18 warnings)
- `llvm-include-order` (include order - 70 warnings)
- `readability-identifier-naming` (naming - 525 warnings, but legitimate issues)

### Magic Numbers - Consider Tuning

Add more UI constants to ignored values:
```yaml
readability-magic-numbers.IgnoredFloatingPointValues: '0.0;0.5;1.0;-1.0;2.0;100.0;0.2;0.25;0.3;0.35;0.4;0.5;0.7;0.8'
```

See full proposal: `2026-01-17_CLANG_TIDY_DISABLE_CHECKS_PROPOSAL.md`
