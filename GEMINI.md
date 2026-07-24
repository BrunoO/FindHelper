# GEMINI.md — FindHelper Project Context

This file serves as the foundational instructional context for Gemini CLI interactions within the **FindHelper** (formerly USN_WINDOWS) repository.

---

## 1. Project Overview
**FindHelper** is a high-performance, cross-platform file search application built with **C++17** and **Dear ImGui**.

### Core Purpose
- Fast file discovery using the **Windows USN Journal** (NTFS) or recursive filesystem crawling (**macOS/Linux**).
- Modern, immediate-mode GUI for filtering by filename, path, extension, size, and modification date.
- **AI-Assisted Search**: Optional integration with Google's Gemini API to translate natural language queries into search parameters.

### Technical Stack
- **Languages**: C++17 (Strict requirement).
- **Build System**: CMake 3.16+.
- **UI Framework**: Dear ImGui (managed via GLFW + DX11/Metal/OpenGL).
- **Testing**: doctest (unit tests) and ImGui Test Engine (UI automation).
- **Key Libraries**: nlohmann_json, FreeType, GLFW.

---

## 2. Architecture & Design
The project follows a **Component-Based Architecture** with clear separation of concerns.

- **`src/core/`**: Application lifecycle (`Application`), bootstrap, settings, and command-line parsing.
- **`src/gui/`**: `GuiState` (the single source of truth for UI data) and `UIActions` (interface for logic-to-UI boundaries).
- **`src/ui/`**: Rendering components (`UIRenderer`, `ResultsTable`, `StatusBar`, etc.) using ImGui.
- **`src/search/`**: Search engine logic, thread-pool management, and `SearchHistory` management.
- **`src/index/`**: The `FileIndex` storage and maintenance logic.
- **`src/usn/`**: Windows-specific NTFS monitoring.
- **`src/platform/`**: Platform-specific abstractions (Linux, macOS, Windows).

### Key Architectural Invariants
- **UI Thread Confinement**: All ImGui calls and `GuiState` mutations must happen on the main thread. Workers must use staging buffers.
- **Dangling Pointer Prevention**: Search results use `std::string_view` into a central path pool. The pool must not be cleared while results are active. Use the `ResultPoolOwner` / `ClearResultPool()` pattern.
- **Dependency Injection**: Major components are owned by `Application` and passed via references/pointers to avoid global state.

---

## 3. Building, Running & Testing

### First-Time Setup
```bash
git submodule update --init --recursive
```

### Build Commands
- **Windows (Developer Command Prompt)**:
  ```powershell
  cmake -S . -B build -A x64
  cmake --build build --config Release
  ```
- **macOS/Linux**:
  ```bash
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --config Release
  ```

### Running the App
- **Windows**: `build\Release\FindHelper.exe`
- **macOS**: `open build/FindHelper.app`
- **Linux**: `./build/FindHelper`

### Running Unit Tests
- **macOS/Linux**: `./scripts/build_tests_macos.sh` (preferred) or `cmake --build build --target run_tests`.
- **Windows**: `ctest --test-dir build -C Release --output-on-failure`.

---

## 4. Development Conventions (Mandatory)

### Naming (See `docs/standards/CXX17_NAMING_CONVENTIONS.md`)
- **Types**: `PascalCase`
- **Functions/Methods**: `PascalCase`
- **Local variables/Parameters**: `snake_case`
- **Member variables**: `snake_case_` (trailing underscore)
- **Constants**: `kPascalCase`
- **Exceptions**: `GuiState` public members use `camelCase` (for JSON compatibility).

### C++17 Style
- **Always** use `std::scoped_lock` (CTAD).
- **Always** use `[[nodiscard]]` for status/error returns.
- **Prefer** `std::string_view` for read-only parameters.
- **Explicit** lambda captures are **mandatory** for MSVC compatibility in template contexts.
- **Parentheses** around `(std::min)` and `(std::max)` are required to avoid `<windows.h>` macro conflicts.

### Platform Guards
- **Never** modify code inside platform blocks (`#ifdef _WIN32`) to "unify" logic. Create a platform-agnostic abstraction instead.
- All `#endif` must have comments: `#endif // _WIN32`.

### AI-Assisted Workflow
- Implementation starts with a **Spec** in `specs/` (use `specs/SPECIFICATION_DRIVEN_DEVELOPMENT_PROMPT.md`).
- Use the **"Boy Scout Rule"**: Leave the code slightly better (const correctness, naming).
- Commit messages must include `Co-Authored-By: Gemini <gemini@google.com>`.

---

## 5. Documentation Map
- **`docs/`**: Public contributor documentation (Architecture, Standards).
- **`internal-docs/`**: Maintainer-specific files (Analysis, Task Plans, AI Prompts).
- **`specs/`**: Feature-specific implementation plans.
- **`scripts/`**: CI/CD and automation tools.

---

## 6. Safety & Integrity
- **Credential Protection**: Never commit or log the `GEMINI_API_KEY`.
- **Drive Integrity**: FindHelper is search-only by default. Explicit user action is required for deletion or pinning.
- **Crash Triage**: Commit messages for fixes should describe the **Fault Model** and the **Safety Mechanism** introduced.
