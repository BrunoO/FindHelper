# Contributing to FindHelper

Thanks for your interest. This is a personal learning project (see [About This Project](README.md#about-this-project)), but contributions are welcome.

---

## Getting started

1. **Fork** the repository and clone your fork.
2. Initialize submodules:
   ```bash
   git submodule update --init --recursive
   ```
3. Follow the build instructions for your platform:
   - [macOS](docs/guides/building/MACOS_BUILD_INSTRUCTIONS.md)
   - [Linux](docs/guides/building/BUILDING_ON_LINUX.md)
   - Windows: see [README.md](README.md#build)

---

## Running tests

```bash
# macOS / Linux
scripts/build_tests_macos.sh

# All platforms (after building)
ctest --test-dir build --output-on-failure
```

---

## Code standards

Read [AGENTS.md](AGENTS.md) before writing any code — it contains the project's naming conventions, C++17 patterns, platform guard rules, and include order requirements. The pre-commit hook runs clang-tidy on staged files automatically; your commit will be rejected if issues are found.

Key rules at a glance:
- C++17, no exceptions to naming conventions (`PascalCase` types/functions, `snake_case_` members)
- Use `(std::min)` / `(std::max)` on Windows-targeted code (macro conflicts)
- Explicit lambda captures everywhere (`[&x]` not `[&]`)
- `const` on all non-mutating locals

---

## Submitting changes

1. Keep changes focused — one logical change per PR.
2. Make sure all tests pass and clang-tidy is clean.
3. Write a clear commit message (what and why, not just what).
4. Open a pull request against `main` with a short description of the change and how you tested it.

---

## Questions

Open an issue if something is unclear or broken.
