#!/usr/bin/env python3
"""
parse_stage4_benchmark_log.py

Parses FindHelper benchmark logs for Stage 4 (RecomputeAllPaths) sub-phase telemetry,
calculates statistical aggregations (mean, min, max, % breakdown), evaluates Gate 0,
and optionally updates internal-docs/benchmarks/2026-09-24_STAGE4_RECOMPUTE_BASELINE.md.

Usage:
    python3 scripts/parse_stage4_benchmark_log.py [LOG_FILES...] [--update] [--debug-mode] [--runs N]

Examples:
    # Parse default Windows log file and display metrics:
    python3 scripts/parse_stage4_benchmark_log.py

    # Parse specific log file and update baseline markdown:
    python3 scripts/parse_stage4_benchmark_log.py C:\\path\\to\\debug.log --update

    # Parse 5 most recent runs from a log:
    python3 scripts/parse_stage4_benchmark_log.py debug.log --runs 5 --update
"""

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional


@dataclass
class Stage4Run:
    run_index: int = 0
    entries: Optional[int] = None
    phases_1_4_ms: Optional[float] = None
    phase_5_sort_ms: Optional[float] = None
    phase_6_build_insert_ms: Optional[float] = None
    prune_orphans_ms: Optional[float] = None
    rebuild_path_to_id_ms: Optional[float] = None
    total_recompute_ms: Optional[float] = None

    def is_complete(self) -> bool:
        return (
            self.phases_1_4_ms is not None
            and self.phase_5_sort_ms is not None
            and self.phase_6_build_insert_ms is not None
            and self.prune_orphans_ms is not None
            and self.total_recompute_ms is not None
        )


def parse_time_to_ms(val_str: str, unit_str: str) -> float:
    val = float(val_str)
    if "sec" in unit_str.lower():
        return val * 1000.0
    return val


def find_default_log_files(limit: int = 5) -> List[Path]:
    search_dirs: List[Path] = []

    # 1. Windows %TEMP% and %TMP% (primary location used by Logger::GetDefaultLogDirectory)
    for env_var in ("TEMP", "TMP"):
        val = os.environ.get(env_var)
        if val:
            p = Path(val)
            if p.is_dir():
                search_dirs.append(p)

    # 2. Windows %LOCALAPPDATA%\FindHelper\logs
    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        p = Path(local_app_data) / "FindHelper" / "logs"
        if p.is_dir():
            search_dirs.append(p)

    # 3. macOS / Linux cache directories
    for env_var in ("XDG_CACHE_HOME", "HOME"):
        val = os.environ.get(env_var)
        if val:
            p = Path(val) / ".cache" if env_var == "HOME" else Path(val)
            if p.is_dir():
                search_dirs.append(p)

    # 4. Current dir and build directories
    search_dirs.extend([Path("."), Path("build"), Path("build/Release"), Path("build_tests")])

    candidate_files: List[Path] = []
    seen: set = set()
    for d in search_dirs:
        if not d.is_dir():
            continue
        try:
            for pattern in ("FindHelper*.log", "*FindHelper*.log", "debug.log", "*.log"):
                for f in d.glob(pattern):
                    resolved = f.resolve()
                    if resolved not in seen and f.is_file() and f.stat().st_size > 0:
                        seen.add(resolved)
                        candidate_files.append(f)
        except OSError:
            pass

    if candidate_files:
        # Sort by mtime descending to get the most recent ones
        candidate_files.sort(key=lambda p: p.stat().st_mtime, reverse=True)
        selected = candidate_files[:limit]
        # Return in chronological order (oldest to newest among the selected)
        selected.sort(key=lambda p: p.stat().st_mtime)
        return selected

    return []


def parse_log_file(file_path: Path) -> List[Stage4Run]:
    runs: List[Stage4Run] = []
    current_run = Stage4Run(run_index=1)

    re_entries = re.compile(
        r"(?:PathRecomputer|FileIndex)::RecomputeAllPaths:\s+(?:indexing|indexed)\s+(\d+)\s+entries"
    )
    re_p1_4 = re.compile(
        r"PathRecomputer phases 1-4 \(collect/adjacency/BFS\) completed in ([\d\.]+)\s+(ms|seconds)"
    )
    re_p5 = re.compile(
        r"PathRecomputer phase 5 \(depth sort\) completed in ([\d\.]+)\s+(ms|seconds)"
    )
    re_p6 = re.compile(
        r"PathRecomputer phase 6 \(path construction and insertion\) completed in ([\d\.]+)\s+(ms|seconds)"
    )
    re_prune = re.compile(
        r"FileIndex prune orphans completed in ([\d\.]+)\s+(ms|seconds)"
    )
    re_rebuild = re.compile(
        r"FileIndex rebuild path-to-id map completed in ([\d\.]+)\s+(ms|seconds)"
    )
    re_total = re.compile(
        r"FileIndex::RecomputeAllPaths completed in ([\d\.]+)\s+(ms|seconds)"
    )

    with open(file_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re_entries.search(line)
            if m:
                count = int(m.group(1))
                # If current run already completed, also back-fill the completed run
                if runs and runs[-1].entries is None:
                    runs[-1].entries = count
                current_run.entries = count
                continue

            m = re_p1_4.search(line)
            if m:
                current_run.phases_1_4_ms = parse_time_to_ms(
                    m.group(1), m.group(2)
                )
                continue

            m = re_p5.search(line)
            if m:
                current_run.phase_5_sort_ms = parse_time_to_ms(
                    m.group(1), m.group(2)
                )
                continue

            m = re_p6.search(line)
            if m:
                current_run.phase_6_build_insert_ms = parse_time_to_ms(
                    m.group(1), m.group(2)
                )
                continue

            m = re_prune.search(line)
            if m:
                current_run.prune_orphans_ms = parse_time_to_ms(
                    m.group(1), m.group(2)
                )
                continue

            m = re_rebuild.search(line)
            if m:
                current_run.rebuild_path_to_id_ms = parse_time_to_ms(
                    m.group(1), m.group(2)
                )
                continue

            m = re_total.search(line)
            if m:
                current_run.total_recompute_ms = parse_time_to_ms(
                    m.group(1), m.group(2)
                )
                # NOTE: rebuild_path_to_id_ms stays None when the timer line is
                # absent (Phase 1B eliminates it). None means "not observed" and
                # must not be coerced to 0.0 — the eliminated/mixed-log branches
                # below distinguish on observed-ness, not on the mean value.
                runs.append(current_run)
                current_run = Stage4Run(run_index=len(runs) + 1)

    # In case the file ended right after metrics without re_total
    if current_run.phases_1_4_ms is not None and current_run not in runs:
        if current_run.total_recompute_ms is None and current_run.is_complete():
            current_run.total_recompute_ms = sum(
                [
                    current_run.phases_1_4_ms or 0.0,
                    current_run.phase_5_sort_ms or 0.0,
                    current_run.phase_6_build_insert_ms or 0.0,
                    current_run.prune_orphans_ms or 0.0,
                    current_run.rebuild_path_to_id_ms or 0.0,
                ]
            )
        runs.append(current_run)

    return runs


def format_ms(val: Optional[float], precision: int = 1) -> str:
    if val is None:
        return "N/A"
    return f"{val:.{precision}f} ms"


def format_sec_or_ms(val: Optional[float]) -> str:
    if val is None:
        return "N/A"
    if val >= 1000.0:
        return f"{val / 1000.0:.3f} s ({val:.1f} ms)"
    return f"{val:.1f} ms"


def update_baseline_markdown(
    baseline_path: Path,
    runs: List[Stage4Run],
    debug_mode: bool = False,
) -> bool:
    if not baseline_path.is_file():
        print(f"Error: Baseline markdown file not found: {baseline_path}")
        return False

    with open(baseline_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Calculate means
    mean_p1_4 = sum(r.phases_1_4_ms or 0.0 for r in runs) / len(runs)
    mean_p5 = sum(r.phase_5_sort_ms or 0.0 for r in runs) / len(runs)
    mean_p6 = sum(r.phase_6_build_insert_ms or 0.0 for r in runs) / len(runs)
    mean_prune = sum(r.prune_orphans_ms or 0.0 for r in runs) / len(runs)
    mean_rebuild = sum(r.rebuild_path_to_id_ms or 0.0 for r in runs) / len(runs)
    mean_total = sum(r.total_recompute_ms or 0.0 for r in runs) / len(runs)

    pct_p1_4 = (mean_p1_4 / mean_total * 100.0) if mean_total > 0 else 0.0
    pct_p5 = (mean_p5 / mean_total * 100.0) if mean_total > 0 else 0.0
    pct_p6 = (mean_p6 / mean_total * 100.0) if mean_total > 0 else 0.0
    pct_prune = (mean_prune / mean_total * 100.0) if mean_total > 0 else 0.0
    pct_rebuild = (mean_rebuild / mean_total * 100.0) if mean_total > 0 else 0.0

    # 1. Update Section 3 table
    table_pattern = re.compile(
        r"(\| Sub-Phase / Component \| Timer Identifier \| Spec Estimate \(\S+\) \| Measured Release Time \(Mean\) \| Measured Debug Time \(Reference\) \| % of Stage 4 Total \| Notes / Observations \|\s*\n"
        r"\|(?:---|\|)+\s*\n)"
        r"((?:\|[^\n]+\n)+)",
        re.MULTILINE,
    )

    m = table_pattern.search(content)
    if m:
        old_rows_block = m.group(2)
        existing_cells = {}
        for line in old_rows_block.strip().splitlines():
            parts = [p.strip() for p in line.split("|")]
            if len(parts) >= 8:
                existing_cells[parts[1]] = (parts[4], parts[5], parts[6])

        def get_existing(name: str, idx: int, fallback: str) -> str:
            for k, v in existing_cells.items():
                if name in k:
                    return v[idx]
            return fallback

        if debug_mode:
            rel_p1_4 = get_existing("Phases 1–4", 0, "_TBD ms_")
            rel_p5 = get_existing("Phase 5", 0, "_TBD ms_")
            rel_p6 = get_existing("Phase 6", 0, "_TBD ms_")
            rel_prune = get_existing("Prune", 0, "_TBD ms_")
            rel_rebuild = get_existing("Rebuild", 0, "_TBD ms_")
            rel_total = get_existing("Total Stage 4", 0, "**_TBD ms_**")

            dbg_p1_4 = f"{mean_p1_4:.1f} ms"
            dbg_p5 = f"{mean_p5:.1f} ms"
            dbg_p6 = f"{mean_p6:.1f} ms"
            dbg_prune = f"{mean_prune:.1f} ms"
            dbg_rebuild = f"{mean_rebuild:.1f} ms"
            dbg_total = f"**{mean_total:.1f} ms** ({mean_total / 1000.0:.2f} s)"

            pct_suffix = " (Debug)"
        else:
            rel_p1_4 = f"{mean_p1_4:.1f} ms"
            rel_p5 = f"{mean_p5:.1f} ms"
            rel_p6 = f"{mean_p6:.1f} ms"
            rel_prune = f"{mean_prune:.1f} ms"
            rel_rebuild = f"{mean_rebuild:.1f} ms"
            rel_total = f"**{mean_total:.1f} ms** ({mean_total / 1000.0:.2f} s)"

            dbg_p1_4 = get_existing("Phases 1–4", 1, "_TBD ms_")
            dbg_p5 = get_existing("Phase 5", 1, "_TBD ms_")
            dbg_p6 = get_existing("Phase 6", 1, "_TBD ms_")
            dbg_prune = get_existing("Prune", 1, "_TBD ms_")
            dbg_rebuild = get_existing("Rebuild", 1, "_TBD ms_")
            dbg_total = get_existing("Total Stage 4", 1, "**_TBD ms_**")

            pct_suffix = ""

        new_rows = (
            f"| **Phases 1–4: Collect, Adjacency & BFS** | `PathRecomputer phases 1-4 (collect/adjacency/BFS)` | ~120 ms | {rel_p1_4} | {dbg_p1_4} | {pct_p1_4:.1f} %{pct_suffix} | Entry collection, parent-child multimap, BFS queue depth assignment |\n"
            f"| **Phase 5: Depth Sort** | `PathRecomputer phase 5 (depth sort)` | ~150 ms | {rel_p5} | {dbg_p5} | {pct_p5:.1f} %{pct_suffix} | `std::sort` by `(depth, id)` |\n"
            f"| **Phase 6: Single-Pass Path Construction & Insert** | `PathRecomputer phase 6 (path construction and insertion)` | ~700–900 ms | {rel_p6} | {dbg_p6} | {pct_p6:.1f} %{pct_suffix} | Topological path assembly + `PathStorage::InsertPath` |\n"
            f"| **Prune Orphan Subtrees** | `FileIndex prune orphans` | ~50 ms | {rel_prune} | {dbg_prune} | {pct_prune:.1f} %{pct_suffix} | `FileIndex::PruneOrphanSubtreesLocked()` |\n"
            f"| **Rebuild Path-to-Id Map** | `FileIndex rebuild path-to-id map` | ~1,000–1,200 ms | {rel_rebuild} | {dbg_rebuild} | {pct_rebuild:.1f} %{pct_suffix} | Second hash pass over all indexed entries |\n"
            f"| **Total Stage 4 (`RecomputeAllPaths`)** | `FileIndex::RecomputeAllPaths` | **~2,000–2,420 ms** | {rel_total} | {dbg_total} | **100 %** | Overall lock duration for path recomputation |\n"
        )
        content = content[: m.start(2)] + new_rows + content[m.end(2) :]

    # 2. Update Section 4 Gate 0 decision (only on Release runs where the
    # rebuild timer was actually observed). When Phase 1B eliminates the timer,
    # rewriting the decision from an unobserved mean would wrongly flip it to
    # REVISE SCOPE, contradicting the console evaluation.
    rebuild_seen = any(r.rebuild_path_to_id_ms is not None for r in runs)
    if not debug_mode and rebuild_seen:
        gate0_passed = mean_rebuild >= 1000.0
        status_text = (
            "**Status:** COMPLETED (Gate 0 Evaluated)"
        )
        content = re.sub(
            r"\*\*Status:\*\* Pending Execution", status_text, content
        )

        if gate0_passed:
            content = re.sub(
                r"- \[ \] \*\*CONFIRMED \(Gate Passed\):\*\*",
                "- [x] **CONFIRMED (Gate Passed):**",
                content,
            )
            content = re.sub(
                r"- \[x\] \*\*REVISE SCOPE:\*\*",
                "- [ ] **REVISE SCOPE:**",
                content,
            )
        else:
            content = re.sub(
                r"- \[ \] \*\*REVISE SCOPE:\*\*",
                "- [x] **REVISE SCOPE:**",
                content,
            )
            content = re.sub(
                r"- \[x\] \*\*CONFIRMED \(Gate Passed\):\*\*",
                "- [ ] **CONFIRMED (Gate Passed):**",
                content,
            )
    elif not debug_mode:
        print("Gate 0 rewrite skipped: rebuild timer not observed (Phase 1B eliminated).")

    # 3. Update Section 5 run logs
    run_log_text = ""
    for i, r in enumerate(runs, 1):
        files_str = f"{r.entries:,}" if r.entries else "N/A"
        run_log_text += (
            f"[Run {i}]\n"
            f"- Files: {files_str}\n"
            f"- Phases 1-4: {format_ms(r.phases_1_4_ms)}\n"
            f"- Phase 5 (sort): {format_ms(r.phase_5_sort_ms)}\n"
            f"- Phase 6 (build+insert): {format_ms(r.phase_6_build_insert_ms)}\n"
            f"- Prune orphans: {format_ms(r.prune_orphans_ms)}\n"
            f"- Rebuild path-to-id: {format_ms(r.rebuild_path_to_id_ms)}\n"
            f"- Total RecomputeAllPaths: {format_ms(r.total_recompute_ms)}\n\n"
        )

    section_header = (
        r"### 5\.2 Debug Build Runs \(Informational Baseline\)"
        if debug_mode
        else r"### 5\.1 Release Build Runs \(`/O2`, `NDEBUG`\)"
    )
    pattern = re.compile(
        rf"({section_header}\s*\n\s*```text\n)(.*?)(\n```)", re.DOTALL
    )
    m = pattern.search(content)
    if m:
        content = (
            content[: m.start(2)]
            + run_log_text.rstrip()
            + content[m.end(2) :]
        )

    # 4. Update File Count in Section 2 if available
    latest_entries = next((r.entries for r in reversed(runs) if r.entries), None)
    if latest_entries:
        content = re.sub(
            r"\|\s*\*\*Indexed Volume File Count\*\*\s*\|[^|]+\|",
            f"| **Indexed Volume File Count** | ~{latest_entries:,} files/directories |",
            content,
        )

    with open(baseline_path, "w", encoding="utf-8") as f:
        f.write(content)

    print(f"Successfully updated baseline benchmark file: {baseline_path}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Parse FindHelper Stage 4 RecomputeAllPaths telemetry from log files."
    )
    parser.add_argument(
        "log_files",
        nargs="*",
        type=Path,
        help="Path to FindHelper debug.log file(s). If omitted, searches default locations.",
    )
    parser.add_argument(
        "--update",
        action="store_true",
        help="Update internal-docs/benchmarks/2026-09-24_STAGE4_RECOMPUTE_BASELINE.md in-place.",
    )
    parser.add_argument(
        "--baseline-file",
        type=Path,
        default=Path("internal-docs/benchmarks/2026-09-24_STAGE4_RECOMPUTE_BASELINE.md"),
        help="Path to baseline markdown file to update (default: internal-docs/benchmarks/2026-09-24_STAGE4_RECOMPUTE_BASELINE.md).",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=None,
        help="Number of most recent runs to include (default: all parsed complete runs).",
    )
    parser.add_argument(
        "--debug-mode",
        action="store_true",
        help="Treat runs as Debug configuration instead of Release configuration.",
    )
    parser.add_argument(
        "--release",
        action="store_true",
        help="Explicitly treat runs as Release configuration (overrides auto-detection).",
    )

    args = parser.parse_args()

    limit = args.runs or 5
    log_files: List[Path] = []
    if args.log_files:
        for p in args.log_files:
            if p.is_dir():
                candidates: List[Path] = []
                seen: set = set()
                for pat in ("FindHelper*.log", "*FindHelper*.log", "debug.log", "*.log"):
                    for f in p.glob(pat):
                        res = f.resolve()
                        if res not in seen and f.is_file() and f.stat().st_size > 0:
                            seen.add(res)
                            candidates.append(f)
                candidates.sort(key=lambda x: x.stat().st_mtime, reverse=True)
                selected = candidates[:limit]
                selected.sort(key=lambda x: x.stat().st_mtime)
                if selected:
                    log_files.extend(selected)
                else:
                    print(f"Warning: No log files found in directory: {p}", file=sys.stderr)
            elif p.is_file():
                log_files.append(p)
            else:
                print(f"Warning: Path not found: {p}", file=sys.stderr)
    else:
        log_files = find_default_log_files(limit=limit)
        if not log_files:
            print("No log files provided and could not automatically find a log file in %TEMP%, %LOCALAPPDATA%, or local folders.")
            print("Usage: python3 scripts/parse_stage4_benchmark_log.py <path_to_log_file>")
            return 1

    if not log_files:
        print("No valid log files to process.")
        return 1

    print(f"Discovered {len(log_files)} recent log file(s):")
    for i, lf in enumerate(log_files, 1):
        print(f"  [{i}] {lf.resolve()}")

    # Auto-detect build mode from log contents
    is_debug = False
    if args.debug_mode:
        is_debug = True
    elif args.release:
        is_debug = False
    else:
        for lp in log_files:
            if lp.is_file():
                with open(lp, "r", encoding="utf-8", errors="replace") as f:
                    for _ in range(500):
                        line = f.readline()
                        if not line:
                            break
                        if "[INFO]" in line or "[DEBUG]" in line:
                            is_debug = True
                            break
            if is_debug:
                break

    all_runs: List[Stage4Run] = []
    for log_path in log_files:
        if not log_path.is_file():
            continue
        parsed = parse_log_file(log_path)
        for r in parsed:
            r.run_index = len(all_runs) + 1
            all_runs.append(r)

    # Filter to runs with data
    valid_runs = [r for r in all_runs if r.phases_1_4_ms is not None]
    if not valid_runs:
        print("No Stage 4 benchmark runs found in log file(s).")
        return 1

    if args.runs is not None and args.runs > 0:
        valid_runs = valid_runs[-args.runs :]

    for i, r in enumerate(valid_runs, 1):
        r.run_index = i

    mode_label = "Debug" if is_debug else "Release"
    print(
        f"\n================================================================================"
    )
    print(f" FindHelper Stage 4 Recompute Benchmark Analysis ({len(valid_runs)} runs, {mode_label} Mode)")
    if is_debug and not args.debug_mode:
        print(" [Auto-detected Debug build from log entries; use --release to override]")
    print(
        f"================================================================================\n"
    )

    for i, r in enumerate(valid_runs, 1):
        files_str = f" ({r.entries:,} entries)" if r.entries else ""
        print(f"Run #{i}{files_str}:")
        print(f"  Phases 1-4 (collect/adjacency/BFS) : {format_ms(r.phases_1_4_ms)}")
        print(f"  Phase 5 (depth sort)               : {format_ms(r.phase_5_sort_ms)}")
        print(f"  Phase 6 (build+insert)             : {format_ms(r.phase_6_build_insert_ms)}")
        print(f"  Prune orphans                      : {format_ms(r.prune_orphans_ms)}")
        print(f"  Rebuild path-to-id map             : {format_ms(r.rebuild_path_to_id_ms)}")
        print(f"  Total RecomputeAllPaths            : {format_sec_or_ms(r.total_recompute_ms)}")
        print()

    # Aggregate statistics
    mean_p1_4 = sum(r.phases_1_4_ms or 0.0 for r in valid_runs) / len(valid_runs)
    mean_p5 = sum(r.phase_5_sort_ms or 0.0 for r in valid_runs) / len(valid_runs)
    mean_p6 = sum(r.phase_6_build_insert_ms or 0.0 for r in valid_runs) / len(valid_runs)
    mean_prune = sum(r.prune_orphans_ms or 0.0 for r in valid_runs) / len(valid_runs)
    mean_rebuild = sum(r.rebuild_path_to_id_ms or 0.0 for r in valid_runs) / len(valid_runs)
    mean_total = sum(r.total_recompute_ms or 0.0 for r in valid_runs) / len(valid_runs)

    pct_p1_4 = (mean_p1_4 / mean_total * 100.0) if mean_total > 0 else 0.0
    pct_p5 = (mean_p5 / mean_total * 100.0) if mean_total > 0 else 0.0
    pct_p6 = (mean_p6 / mean_total * 100.0) if mean_total > 0 else 0.0
    pct_prune = (mean_prune / mean_total * 100.0) if mean_total > 0 else 0.0
    pct_rebuild = (mean_rebuild / mean_total * 100.0) if mean_total > 0 else 0.0

    # Observed-ness, not the mean value, decides the eliminated branch: a 0.0 mean
    # also arises from mixed logs (pre/post-Phase-1B runs) or a tiny index.
    rebuild_seen = any(r.rebuild_path_to_id_ms is not None for r in valid_runs)
    rebuild_observed = sum(1 for r in valid_runs if r.rebuild_path_to_id_ms is not None)

    print("--------------------------------------------------------------------------------")
    print(" Aggregated Breakdown (Averages)")
    print("--------------------------------------------------------------------------------")
    print(f"  Phases 1-4 (collect/adjacency/BFS) : {mean_p1_4:8.1f} ms  ({pct_p1_4:5.1f}%)")
    print(f"  Phase 5 (depth sort)               : {mean_p5:8.1f} ms  ({pct_p5:5.1f}%)")
    print(f"  Phase 6 (build+insert)             : {mean_p6:8.1f} ms  ({pct_p6:5.1f}%)")
    print(f"  Prune orphans                      : {mean_prune:8.1f} ms  ({pct_prune:5.1f}%)")
    print(f"  Rebuild path-to-id map             : {mean_rebuild:8.1f} ms  ({pct_rebuild:5.1f}%)")
    print(f"  ---------------------------------------------------------")
    print(f"  Total Stage 4 (RecomputeAllPaths)  : {mean_total:8.1f} ms  (100.0%) -> {mean_total / 1000.0:.3f} s")
    print("--------------------------------------------------------------------------------\n")

    # Gate 0 / Phase 1B Evaluation
    print("--------------------------------------------------------------------------------")
    print(f" Performance Evaluation ({mode_label} Build)")
    print("--------------------------------------------------------------------------------")
    if rebuild_seen and rebuild_observed < len(valid_runs):
        print(f" [!] Mixed logs: rebuild timer observed in only {rebuild_observed}/{len(valid_runs)} runs;")
        print("     the rebuild mean below is diluted — evaluate each binary's runs separately.")
    if is_debug:
        print(" [!] Note: Performance evaluation officially requires measurements from a Release (/O2) build.")
        if not rebuild_seen:
            print(f"     In Debug (Phase 1B), RebuildPathToIdMap is eliminated; Recompute total: {mean_total:.1f} ms.")
        else:
            print(f"     In Debug, RebuildPathToIdMap took {mean_rebuild:.1f} ms.")
        print("     To measure official performance, run on Release binary: build\\Release\\FindHelper.exe")
    else:
        if not rebuild_seen:
            print(" Phase 1B Status: RebuildPathToIdMapLocked eliminated (folded into Phase 6).")
            print(f"   Stage 4 Recompute Total: {mean_total:.1f} ms ({mean_total / 1000.0:.3f} s)")
            if mean_total < 2000.0:
                print("   -> PASSED: Recompute total < 2.0 s! Pipeline is on track for sub-2s target.")
            else:
                print("   -> EVALUATE: Recompute total >= 2.0 s. Consider Phase 2 folder table spike.")
        else:
            print(f" Criterion 1: RebuildPathToIdMapLocked >= 1,000 ms?")
            print(f"   Measured Rebuild Mean: {mean_rebuild:.1f} ms")
            if mean_rebuild >= 1000.0:
                print("   -> PASSED (Gate Met): Rebuild accounts for >= 1.0s.")
                print("      Decision: Proceed with Phase 1B (fold map updates into single pass).")
            else:
                print(f"   -> REVISE SCOPE: Rebuild is only {mean_rebuild:.1f} ms (< 1,000 ms).")
                print("      Decision: Re-evaluate Phase 1B scope against empirical data.")
    print("--------------------------------------------------------------------------------\n")

    if args.update:
        baseline_path = args.baseline_file
        if not baseline_path.is_file():
            # Try repo root relative
            repo_root = Path(__file__).resolve().parent.parent
            baseline_path = repo_root / args.baseline_file

        update_baseline_markdown(baseline_path, valid_runs, debug_mode=is_debug)

    return 0


if __name__ == "__main__":
    sys.exit(main())
