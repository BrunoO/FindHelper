#!/usr/bin/env python3
"""
analyze_trace.py — Analyze a symbolicated xctrace time-profile export.

USAGE
-----
1. Export a symbolicated time-profile table from a .trace file:

   xctrace symbolicate --input my.trace --dsym path/to/App.dSYM --output my-sym.trace
   xctrace export --input my-sym.trace \
       --xpath '//run[@number="1"]//table[@schema="time-profile"]' \
       > run1.json

   Or use the xctools MCP in Claude Code:
     mcp__xctools__xctrace_symbolicate(input_file=..., dsym_path=..., output_path=...)
     mcp__xctools__xctrace_export(input_file=..., xpath='//run[@number="1"]//table[@schema="time-profile"]')

2. Run this script:

   python3 scripts/analyze_trace.py run1.json
   python3 scripts/analyze_trace.py run1.json --top 40 --chains 5 --filter FindHelper

NOTES
-----
- xctrace export wraps XML in a JSON envelope: {"result": "<xml>..."}.
  This script unwraps it automatically.
- Frame elements use XML ref= IDs for deduplication. This script resolves
  all refs to their original name so full call stacks are recovered.
- "Leaf" frames = where the CPU was at sample time (self time).
- "All-stack" frames = frames that appear anywhere in a backtrace (inclusive time).
- Call chains are grouped by exact stack tuple (up to --depth frames deep).
"""

import argparse
import json
import re
import sys
from collections import Counter


def load_xml(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        raw = f.read()
    try:
        data = json.loads(raw)
        return data["result"]
    except (json.JSONDecodeError, KeyError):
        return raw  # already raw XML


def build_id_map(xml: str) -> dict[str, str]:
    """Map frame id → name from all <frame id="X" name="Y"> elements."""
    return {m.group(1): m.group(2) for m in re.finditer(r'<frame\s+id="(\d+)"\s+name="([^"]+)"', xml)}


def resolve_frame(frag: str, id_map: dict[str, str]) -> str | None:
    m = re.match(r'<frame\s+id="\d+"\s+name="([^"]+)"', frag)
    if m:
        return m.group(1)
    m = re.match(r'<frame\s+ref="(\d+)"', frag)
    if m:
        return id_map.get(m.group(1))
    return None


def extract_stacks(xml: str, id_map: dict[str, str]) -> list[tuple[str, ...]]:
    stacks = []
    for bt in re.findall(r'<backtrace[^>]*>(.*?)</backtrace>', xml, re.DOTALL):
        frames = [resolve_frame(f, id_map) for f in re.findall(r'<frame[^>]+>', bt)]
        frames = [f for f in frames if f]
        if frames:
            stacks.append(tuple(frames))
    return stacks


def truncate(name: str, width: int = 110) -> str:
    return name if len(name) <= width else name[:width - 1] + "…"


def print_section(title: str) -> None:
    print(f"\n{'=' * 70}")
    print(title)
    print('=' * 70)


def analyze(xml: str, top: int, chains: int, depth: int, filter_str: str | None) -> None:
    id_map = build_id_map(xml)
    stacks = extract_stacks(xml, id_map)
    n = len(stacks)

    if n == 0:
        print("No backtraces found. Is this a time-profile export from a symbolicated trace?")
        sys.exit(1)

    print(f"Unique frames in id map : {len(id_map)}")
    print(f"Backtraces (samples)    : {n}")

    # ── Leaf frames ──────────────────────────────────────────────────────────
    leaf_counts = Counter(s[0] for s in stacks)
    if filter_str:
        leaf_counts = Counter({k: v for k, v in leaf_counts.items() if filter_str.lower() in k.lower()})

    print_section(f"TOP {top} LEAF FRAMES  (self time — where CPU was at sample time)")
    for name, cnt in leaf_counts.most_common(top):
        print(f"  {cnt:5d} ({100 * cnt / n:5.1f}%)  {truncate(name)}")

    # ── All-stack frames ──────────────────────────────────────────────────────
    all_counts = Counter(f for s in stacks for f in s)
    if filter_str:
        all_counts = Counter({k: v for k, v in all_counts.items() if filter_str.lower() in k.lower()})

    print_section(f"TOP {top} ALL-STACK FRAMES  (inclusive time — appears anywhere in backtrace)")
    for name, cnt in all_counts.most_common(top):
        print(f"  {cnt:5d} ({100 * cnt / n:5.1f}%)  {truncate(name)}")

    # ── Call chains for the hottest leaf frames ───────────────────────────────
    hot_leaves = [name for name, _ in Counter(s[0] for s in stacks).most_common(top)]
    if filter_str:
        hot_leaves = [n for n in hot_leaves if filter_str.lower() in n.lower()]

    print_section(f"CALL CHAINS for top {min(chains, len(hot_leaves))} hottest leaf frames  (up to {depth} frames deep)")
    for target in hot_leaves[:chains]:
        matching = [s for s in stacks if s[0] == target]
        chain_counts = Counter(s[:depth] for s in matching)
        print(f"\n  [{len(matching)} samples]  {truncate(target, 90)}")
        for chain, cnt in chain_counts.most_common(3):
            print(f"    {cnt}×  (depth {len(chain)}):")
            for i, frame in enumerate(chain):
                tag = "[leaf]" if i == 0 else f"[+{i:2d}]"
                print(f"      {tag}  {truncate(frame, 100)}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Analyze a symbolicated xctrace time-profile JSON export.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("input", help="Path to exported JSON (or raw XML) file")
    parser.add_argument("--top", type=int, default=30, help="Number of frames to show in top-N lists (default: 30)")
    parser.add_argument("--chains", type=int, default=8, help="Number of hot leaves to show call chains for (default: 8)")
    parser.add_argument("--depth", type=int, default=10, help="Max frames per call chain (default: 10)")
    parser.add_argument("--filter", dest="filter_str", default=None,
                        help="Only show frames containing this substring (case-insensitive)")
    args = parser.parse_args()

    xml = load_xml(args.input)
    analyze(xml, args.top, args.chains, args.depth, args.filter_str)


if __name__ == "__main__":
    main()
