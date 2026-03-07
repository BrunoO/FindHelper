# macOS Home Folder Index – Benefit Assessment

**Date:** 2026-02-02  
**Scope:** Indexing a typical macOS home folder; relevance of getattrlistbulk and other optimisations.

---

## 1. What we do today (macOS)

- **opendir** / **readdir** per directory (glibc-style; kernel uses getdents-style batching).
- For each entry: if **d_type != DT_UNKNOWN**, use it for `is_directory` / `is_symlink` (no syscall). Else **lstat(full_path)** once per such entry.
- Path building, batching (e.g. 2500 paths), **InsertPaths** under one lock per batch, then **RecomputeAllPaths** at the end.

So per directory we pay: one **opendir**, a series of **readdir** (amortised as few getdents-style reads), and **lstat** only for entries where the FS returns `DT_UNKNOWN`.

---

## 2. Typical “index my home folder” profile

Rough order of magnitude for a developer-style Mac home:

| Metric | Order of magnitude |
|--------|--------------------|
| Directories | 10k–100k+ (e.g. with node_modules, .cache, Xcode, Mail, Caches) |
| Total entries (files + dirs) | 100k–2M+ |
| Large directories (e.g. &gt;1k entries) | A few to tens (node_modules, Library/Caches, .npm, etc.) |
| Filesystem | Usually APFS (local SSD); sometimes home on SMB/NFS |

APFS (and HFS+) on macOS typically **fill d_type** for local directories. So for a **local** home:

- **lstat rate**: Low — only for the rare entries where `d_type == DT_UNKNOWN` (often 0 on APFS).
- **Cost per directory**: Dominated by **opendir + readdir** (I/O + syscall count), plus path building and in-memory work; **not** by lstat.

So for “index home on **local APFS**”, the bottleneck is:

1. **Number of directories** (one opendir + readdir sequence per directory).
2. **Total entries** (path construction, batching, InsertPaths, RecomputeAllPaths).
3. **I/O** to read directory contents (kernel already batches via getdents-style reads).

---

## 3. getattrlistbulk for this use case

**What we need:** Only **name + type** (is_directory, is_symlink). We do **not** need mtime, size, or other attributes at index time (we have lazy loading).

**What getattrlistbulk is good at:** Returning **many attributes** for many entries in one go, without creating a vnode per file in the kernel. The win is when you would otherwise do **many** stat/lstat per directory.

**For a local APFS home:**

- We already **avoid** lstat for almost all entries (d_type from readdir).
- So we are **not** in the “many lstats per directory” regime where getattrlistbulk shines.
- Public benchmarks (e.g. mjtsai, “Performance Considerations When Reading Directories on macOS”) indicate that for **name-only or name+type** on APFS, **opendir/readdir can be faster than getattrlistbulk**.
- **Conclusion:** For “index my home folder” on **local APFS**, switching to getattrlistbulk for the same “name + type” workload would likely bring **no benefit or a small regression**, not a gain.

**When getattrlistbulk could help:**

- Home (or a large subtree) on **network storage** (SMB/NFS) where the client often returns **d_type == DT_UNKNOWN**, so we currently do **one lstat per entry** in those directories.
- Then “large directory” = directory with hundreds/thousands of entries and many DT_UNKNOWN. getattrlistbulk could replace **N** lstat calls with **one or a few** bulk attribute calls.
- Even then, the win is only for those **specific** (e.g. network) directories; the rest of the tree is unchanged.

**Summary:** For **your** case (home on macOS, almost certainly local APFS), getattrlistbulk is **not** a high-value optimisation. The benefit would only appear if you were indexing a lot from a volume where d_type is usually DT_UNKNOWN (e.g. network home).

---

## 4. Other levers that matter more for “index home”

These have a larger impact than getattrlistbulk for a typical Mac home:

| Lever | Effect | Effort |
|-------|--------|--------|
| **Exclude patterns** | Skip `node_modules`, `.git`, `Library/Caches`, `.npm`, `*.xcuserstate`, etc. Cuts directory and entry count sharply (often 30–60%+ for dev homes). | Medium (config + filter in crawler). |
| **Batch insert (InsertPaths)** | Already done: one lock per batch instead of per path; big reduction in contention. | Done. |
| **Explicit is_directory** | Already done: correct directory semantics, no duplicate/misclassified dirs. | Done. |
| **readdir + d_type** | Already done: no lstat on APFS for almost all entries. | Done. |
| **Batch size (e.g. 2500)** | Already tuned: fewer FlushBatch/InsertPaths calls. | Done. |
| **fts() for tree walk** | Single API for full tree; some benchmarks show fts faster than manual opendir/readdir queue. Would require a different crawl structure (one fts handle per tree vs our queue of dirs). | High. |

So for “index my home folder on macOS”, the **highest-impact** next step is usually **exclude patterns** (and optionally a “max depth” or “only these top-level dirs”) rather than getattrlistbulk.

---

## 5. Bottom line

- **getattrlistbulk:** For indexing a **local** macOS home (APFS), **low or no benefit**; we already avoid lstat via d_type. Only consider it for **network** or other volumes where d_type is often DT_UNKNOWN and directories are large.
- **“Large directory” for getattrlistbulk** in our context: directories with **many entries and many DT_UNKNOWN** (e.g. hundreds/thousands of lstats per dir). Not typical for local APFS home.
- **Better wins for “index my home”:** Exclude patterns (and possibly depth/scope), then everything we already did (batch insert, d_type, explicit is_directory, batch size). getattrlistbulk is optional and only for special (e.g. network) cases.
