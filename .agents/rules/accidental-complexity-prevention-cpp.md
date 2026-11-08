---
description: Prevent accidental complexity, premature abstraction, and architectural anti-patterns in C++ code
globs: "**/*.cpp,**/*.h,**/*.hpp"
alwaysApply: false
paths:
  - "**/*.cpp"
  - "**/*.h"
  - "**/*.hpp"
---

# Accidental Complexity & Premature Abstraction Prevention

Enforce simplicity, strict architectural layering, and algorithmic invariants across the codebase.

## 1. Container & Algorithmic Invariants Across Flags
- **Never silently change algorithmic complexity** across configuration branches (e.g., `#ifndef FAST_LIBS_BOOST`).
- If an optimized path uses an $O(1)$ hash map, the fallback path must also be an $O(1)$ hash map (e.g. wrap `std::unordered_map` with `std::string_view` overloads). Never swap paradigms to an $O(\log N)$ tree (`std::map`).
- Container aliases must support identical core capabilities across all configurations (e.g., `.reserve()`, `bucket_count()`).

## 2. Interface Segregation for Seams (No Leaky Hosts)
- Keep unit test seams strictly aligned to consumer subsystems.
- Do not dump background orchestration or monitoring queries into UI shortcut hosts (e.g. `ShortcutHost` handles only UI shortcuts; `CrawlHost` handles crawler completion and recrawl coordination).
- Composition roots (`Application`) may implement multiple narrow interfaces rather than one monolithic bloated interface.

## 3. Strict Dependency Hierarchy & Layering
- High-level index coordination and storage belong in `src/index/`.
- Lower-level utility modules (`src/path/`, `src/crawler/`) must remain leaf-oriented and must **never** include index storage headers or depend on `FileIndexStorage`.
- Avoid circular couplings between sibling directories.

## 4. Zero Test Machinery in Production Trees
- All mocks, fakes, stubs, and test double implementations (e.g., `FakeChunkReader`, `FakeExtentProvider`) must live exclusively in `tests/`.
- Production directories (`src/`) must contain zero test double classes or test-only seam fixtures.

## 5. Hot-Path Purity (No Test Hooks in Inner Loops)
- Never poll `thread_local` test hook flags, atomic configuration variables, or dynamic environment checks inside candidate evaluation loops over search results.
- Check flags **once** during matcher/pipeline construction and select or configure the algorithm ahead of time.

## 6. Namespaces Over Degenerate Static Classes
- In C++, never write Java/C#-style static utility classes (classes with deleted constructors and only static methods).
- Use standard `namespace` instead. Namespaces provide cleaner syntax, allow Argument-Dependent Lookup (ADL), and avoid artificial class wrappers.

## 7. YAGNI on Dynamic Polymorphism
- Do not introduce abstract base classes (`ISomething`) or virtual method hierarchies when only one concrete implementation exists in the codebase.
- Inline strategy methods directly until runtime polymorphism is genuinely required by multiple production implementations.

## 8. Value Encapsulation Over Pointer Plumbing
- Avoid passing loose pairs of mutable pointers (e.g., `int* window_width, int* window_height`) across multiple UI layers.
- Aggregate related properties into a cohesive value struct (e.g., `WindowDimensions`) owned directly by the state container.
