## What does this change?

<!-- Brief description of what this PR does and why. -->

## How was it tested?

<!-- Describe how you verified the change works and doesn't break anything. -->
<!-- e.g. "Built and ran on macOS, ran tests with scripts/build_tests_macos.sh" -->

## Checklist

- [ ] Builds without errors on the target platform(s)
- [ ] All tests pass (`ctest --test-dir build --output-on-failure`)
- [ ] clang-tidy is clean (pre-commit hook passes)
- [ ] Follows naming conventions in [AGENTS.md](AGENTS.md)
- [ ] No new `#ifdef` blocks that could be unified across platforms
