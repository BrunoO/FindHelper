# Bloom Filter: Local Optimizations Relevance & Word-Sized Storage

**Date:** 2026-01-30  
**Scope:** `src/utils/BloomFilter.h` — review of local optimizations and the "replace bits_ with std::vector<uint64_t>" recommendation.

---

## 1. Word-sized storage (uint64_t)

**Recommendation (from design):** Replace `bits_` with `std::vector<uint64_t>` to:
- Reduce memory loads per query
- Set multiple bits with bit masks
- Enable atomic 64-bit OR if concurrent building is required

**Status: Already implemented.**

The current implementation uses `std::vector<uint64_t> bits_` (see `BloomFilter.h` line 76: "Word-sized bitset for better cache and atomic ops"). No change required.

**Benefits we already get:**
- **Fewer loads per query:** Each MayContain() touches at most `num_hash_functions_` words (typically 6); with byte storage we could touch up to 6 bytes in different cache lines; with 64-bit words we have fewer, naturally aligned loads.
- **Bit masks:** Add/MayContain use `word_idx = bit_pos / kBitsPerWord`, `bit_idx = bit_pos % kBitsPerWord`, and `bits_[word_idx] |= (1ULL << bit_idx)` (or the AND for MayContain). This is the intended pattern.
- **Atomic building:** If we ever build the filter concurrently (e.g. parallel Add() from multiple threads), we can replace `bits_[word_idx] |= ...` with `std::atomic_ref(bits_[word_idx]).fetch_or(...)` (C++20) or a mutex per word / stripe. No refactor of storage needed.

**Recommendation:** Keep the current `std::vector<uint64_t> bits_` design. Update optimization docs (e.g. IMPLEMENTATION_PLAN.md, ARCHITECTURE_INTEGRATION_ANALYSIS.md) so they no longer show `std::vector<uint8_t> bits_` — the code has already moved to word-sized storage.

---

## 2. Local optimizations — relevance and keep/remove

| Optimization | Where | Relevance | Keep? | Notes |
|-------------|--------|-----------|--------|--------|
| **Double hashing** | GetBitPosition / ComputePositionFromHashes | High | **Yes** | Compute h1, h2 once per Add/MayContain; derive k positions with h_i = (h1 + i*d) % m. Cuts cost from k hash calls to 2. Essential for hot path. |
| **Hash rotation** | Hash() | Medium | **Yes** | kHashRotateLeftBits (13) / kHashRotateRightBits (51) improve mixing. Simple and effective. |
| **kDoubleHashSeed (golden ratio)** | GetBitPosition / ComputePositionFromHashes | High | **Yes** | Makes h2 independent of h1; lowers effective false positive rate. |
| **Minimum bit size (64)** | Constructor | High | **Yes** | Avoids tiny filters (e.g. n=1,2) with very high FP rate. |
| **Optimal k** | num_hash_functions_ = round((m/n)*ln 2) | High | **Yes** | Minimizes FP rate for given m, n. |
| **Word alignment of bit_size_** | Constructor | Medium | **Yes** | Align to kBitsPerWord so we use full words; avoids partial word access. |
| **GetBitPosition(data, i)** | Private helper | Low | **Optional remove** | Only used for “compatibility”; Add/MayContain use ComputePositionFromHashes(h1, h2, i). Not used in tests. Safe to remove to reduce surface; or keep for clarity/documentation. |

**Summary:** All current local optimizations are relevant. Keep them. The only optional cleanup is removing `GetBitPosition(std::string_view data, uint32_t i)` if we want to avoid dead code; it is not called anywhere.

---

## 3. Recommendations summary

1. **Word-sized storage:** Already in place. Keep it. Update docs that still show `uint8_t` bits to reflect `uint64_t` words.
2. **Local optimizations:** Keep all (double hashing, rotation, golden-ratio seed, min bit size, optimal k, word alignment). Optionally remove the unused `GetBitPosition(data, i)` overload.
3. **Concurrent building:** Not required today (filter is built single-threaded in CreateSearchContext). If we add it later, use atomic OR on the existing `uint64_t` words (e.g. per-word or striped locking / atomic_ref); no storage change needed.

---

## 4. Doc updates suggested

- **docs/optimization/IMPLEMENTATION_PLAN.md** — Section 1.1 still shows `std::vector<uint8_t> bits_`, byte index, and k hash calls per Add/MayContain. Update to:
  - `std::vector<uint64_t> bits_`
  - Word index: `bit_pos / kBitsPerWord`, `bit_pos % kBitsPerWord`
  - Double hashing: 2 hash calls per Add/MayContain, k positions from ComputePositionFromHashes.
- **docs/optimization/ARCHITECTURE_INTEGRATION_ANALYSIS.md** — Same: replace uint8_t bits with uint64_t and align pseudocode with current implementation.

---

## 5. Speed/efficiency double-check (2026-01-30)

**Hot path (MayContain):**

- **Double hashing:** 2 hash calls per query (h1, h2); k positions derived via `(h1 + i*d) & mask` — kept.
- **Compute `d` once:** Previously `ComputePositionFromHashes(h1, h2, i)` recomputed `d = (h2 % (m-1)) + 1` every iteration (6× per MayContain). **Fixed:** compute `d` once before the loop; use `(h1 + i*d) & mask` in the loop.
- **Avoid modulo in loop:** Integer `% bit_size_` is expensive. **Fixed:** round `bit_size_` up to the next power of two in the constructor; use `pos = (h1 + i*d) & (bit_size_-1)` (single AND instead of division).
- **Word/bit index:** Use `word_idx = bit_pos >> kLog2BitsPerWord` and `bit_idx = bit_pos & (kBitsPerWord - 1)` (shift and AND instead of `/` and `%`).
- **Hash inner loop:** Use `(i & 7)` instead of `(i % 8)` for the shift amount in `Hash()`.

**Result:** MayContain does 2 hash calls, 1 modulo (for `d`), then k iterations with AND + shift + AND + load + AND + compare (no division in loop). All tests pass.

---

## 6. Windows fixes — no performance penalty (2026-01-30)

**Change** | **Runtime cost** | **Reason**
----------|------------------|--------
**(std::max)(1.0, k_opt)** | **None** | Compile-time only: parentheses prevent Windows.h macro expansion; generated code is identical to `std::max`.
**next_pow2 overflow guard** (`next_pow2 <= SIZE_MAX / 2`) | **Negligible** | One extra comparison per loop iteration in the **constructor** (runs once per filter). Loop is O(log₂(bit_size_)); e.g. ~17 iterations for 8M bits.
**use_and_mask_ branch** in Add/MayContain | **None in practice** | One branch per call: `use_and_mask_ ? (sum & mask) : (sum % bit_size_)`. On **64-bit** (main Windows target), extension-set bit_size_ is always reachable as a power of two (e.g. up to ~2²³ for millions of extensions), so `use_and_mask_` is **always true**. Branch is 100% predictable; no misprediction penalty. On 32-bit overflow fallback, we use modulo (same cost as before the AND optimization).
**Hash: (i & (kBitsPerByte - 1))** | **None** | `kBitsPerByte` is constexpr 8; compiler constant-folds to 7. Same as `(i & 7)`.
**Parentheses in sum** `h1 + (i * d)` | **None** | Same semantics; no extra instructions.

**Conclusion:** Windows-safe changes do not add measurable hot-path cost. The only hot-path addition is a single, highly predictable branch that is always taken on 64-bit and on typical 32-bit extension filters.
