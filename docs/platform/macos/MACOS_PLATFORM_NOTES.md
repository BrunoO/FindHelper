# macOS Platform Notes (public)

**Date:** 2026-02-02  
**Scope:** High-level macOS platform notes and pointers. Detailed build instructions remain in the main build guides.

---

## 1. Where to start

- **Build instructions:** See [`guides/building/MACOS_BUILD_INSTRUCTIONS.md`](../../guides/building/MACOS_BUILD_INSTRUCTIONS.md) for:
  - Required tools and SDK versions
  - CMake configuration and build commands
  - Running the macOS test suite (`scripts/build_tests_macos.sh`)

- **UI and rendering model:** See [`design/IMGUI_IMMEDIATE_MODE_PARADIGM.md`](../../design/IMGUI_IMMEDIATE_MODE_PARADIGM.md) for:
  - How the ImGui immediate-mode UI is structured
  - Threading constraints (all ImGui calls on the main thread)
  - How the macOS renderer (Metal) integrates with the shared UI code

---

## 2. Platform-specific implementation overview

macOS-specific code lives primarily under:

- `src/platform/macos/`
  - `AppBootstrap_mac.*` – Application bootstrap (window, Metal, ImGui setup)
  - `FileOperations_mac.*` – macOS implementation of the shared `platform/FileOperations.h` API
  - `FontUtils_mac.*`, `ThreadUtils_mac.*`, `StringUtils_mac.*`, `MetalManager.*`

Shared cross-platform contracts are declared in:

- `src/platform/FileOperations.h` – File operations API used by all platforms
  - Implementations: `FileOperations_win.cpp`, `FileOperations_mac.mm`, `FileOperations_linux.cpp`

When adding new macOS-specific functionality:

- Prefer adding declarations to a shared header under `src/platform/` when the API is shared across platforms, and keep per-OS implementations in `src/platform/<os>/`.
- Keep macOS-only helpers under `src/platform/macos/` when they are not intended for other platforms.

---

## 3. Future macOS-specific documentation

This file is the entry point for macOS platform notes. Future topics that may be added here:

- Windowing and high-DPI/Retina behavior (GLFW + Metal)
- File system behavior differences vs Windows/Linux
- Any macOS-only debugging or profiling tips

