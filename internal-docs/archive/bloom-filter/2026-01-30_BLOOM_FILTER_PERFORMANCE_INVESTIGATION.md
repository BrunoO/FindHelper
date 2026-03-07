# Bloom Filter Extension Filtering: Why It Was Slower (10–20%)

**Date:** 2026-01-30  
**Conclusion:** Per-file Bloom cost (2 hashes + 6 checks) is comparable to or higher than binary search over typical small extension sets. Bloom is only beneficial when the extension set is large enough that binary search is more expensive than the Bloom check.

**Update:** Bloom filter was removed; extension matching uses only Layer 1 (sorted binary search) + Layer 2 (hash set). See `docs/plans/2026-01-30_BLOOM_FILTER_REMOVAL_PLAN.md`.

---

## 1. Hot-path cost comparison

**With Bloom (case_sensitive, Layer 0 enabled):**

- **Every file:** `MayContain(ext_view)` = 2 × `Hash(ext)` + 1 modulo (for `d`) + 6 × (word load + AND + shift).
- **~30% of files (Bloom “maybe”):** then Layer 1 (binary search over `sorted_extensions`).
- **Cost:** `C_bloom × 100%` + `C_binary × ~30%`.

**Without Bloom (only Layer 1):**

- **Every file:** binary search over `sorted_extensions` (log₂(n) string comparisons).
- **Cost:** `C_binary × 100%`.

**Bloom wins only if:**  
`C_bloom + 0.3 × C_binary < C_binary`  
⇒ `C_bloom < 0.7 × C_binary`.

---

## 2. Why C_bloom ≥ C_binary for typical n

- **C_bloom:** 2 hashes (each: loop over extension length 3–10 chars: XOR, shift, rotate) + 1 integer modulo + 6 word loads + 6 ANDs. Dozens of instructions per call.
- **C_binary (n = 5–20):** log₂(n) ≈ 3–5 string comparisons; each comparison is a short string (3–10 chars). Roughly similar or fewer instructions than one Bloom check.

So for **small extension sets (n ≈ 5–30)**, the Bloom layer does **more work per file** than the binary search it is trying to avoid. Net effect: **10–20% slower** when Bloom is always used.

---

## 3. When would Bloom help?

Bloom helps when the **alternative** is expensive:

- **Large n (e.g. n ≥ 24–32):** binary search does ~5–6 comparisons; C_binary grows. Then `C_bloom < 0.7 × C_binary` can hold and Bloom can win.
- **Very cheap Bloom:** e.g. 1 hash + 3 bits (higher FP rate, lower C_bloom) could win for smaller n, at the cost of more Layer 1 work.

---

## 4. Fix applied

- **Only enable the Bloom filter when `extension_set.size() >= kMinExtensionCountForBloom`** (e.g. 24).
- For **small extension sets** we do **not** build or use the Bloom filter; we keep **Layer 1 (sorted binary search)** + Layer 2 (hash set). No regression.
- For **large extension sets** we build and use the Bloom filter so that C_bloom is below 0.7 × C_binary and we get a net speedup.

This removes the 10–20% regression for the common case (small n) and keeps a potential win for large n.
