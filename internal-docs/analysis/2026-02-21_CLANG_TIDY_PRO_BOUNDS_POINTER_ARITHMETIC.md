# 2026-02-21 — cppcoreguidelines-pro-bounds-pointer-arithmetic

## What this check flags

**`cppcoreguidelines-pro-bounds-pointer-arithmetic`** warns on any use of pointer arithmetic (e.g. `p + i`, `p - q`, `*(p + i)`, or subscript `p[i]` when `p` is a raw pointer). The C++ Core Guidelines (Bounds.1) prefer:

- Standard library containers and iterators
- `std::span` (C++20) or index-based access with explicit bounds checks
- Avoiding raw pointer arithmetic to reduce out-of-bounds and lifetime bugs

## Why we still use it here

In this codebase, pointer arithmetic is used in a few constrained places:

1. **MFT parsing** (`MftMetadataReader`) — Windows MFT structures are byte buffers; we compute `base + offset` and sector-based pointers with validated offsets. Refactoring to `std::span` would require wider API changes and the current code is already validated.
2. **Path pattern matcher** — Hot path over contiguous `SimpleToken*` and fixed storage; indexing is bounds-checked by loop range. Replacing with `std::span<SimpleToken>` would be a larger refactor for a hot path.
3. **FileIndex** — Checking `detail[0]` was replaced with `*detail` to avoid subscript (one instance fixed without NOLINT).

## How we addressed the 9 warnings

- **FileIndex.cpp** — Replaced `detail[0]` with `*detail` so the “empty detail” check no longer uses pointer arithmetic.
- **MftMetadataReader.h / MftMetadataReader.cpp** — Added `NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)` with a short comment at the MFT offset/sector pointer sites.
- **PathPatternMatcher.cpp** — Added `NOLINT` (or `NOLINTNEXTLINE`) at each flagged site with a comment that the pointer is a contiguous array in a hot path and indexing is loop-bounded.

All changes keep current behavior and semantics; no functional refactors.
