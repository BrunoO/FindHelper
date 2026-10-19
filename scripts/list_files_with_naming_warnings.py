#!/usr/bin/env python3
"""
List src files that have readability-identifier-naming warnings.

Uses same config and build dir as run_clang_tidy.sh.
Run from project root.
"""

import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent


def get_build_dir():
    for candidate in (PROJECT_ROOT / "build", PROJECT_ROOT / "build_coverage"):
        if (candidate / "compile_commands.json").is_file():
            return candidate
    return None


def main():
    src_dir = PROJECT_ROOT / "src"
    if not src_dir.exists():
        print("Error: src/ not found (run from project root)")
        return

    config_file = PROJECT_ROOT / ".clang-tidy"
    args = [f"--config-file={config_file}", "--quiet"]
    build_dir = get_build_dir()
    if build_dir is not None:
        args.extend(["-p", str(build_dir)])

    cpp_files = sorted(
        [f for f in list(src_dir.rglob("*.cpp")) + list(src_dir.rglob("*.h"))
         if "external" not in str(f) and not f.name.startswith("Embedded")]
    )

    files_with_naming = []
    check = "readability-identifier-naming"

    for i, file_path in enumerate(cpp_files, 1):
        if i % 50 == 0:
            print(f"  ... {i}/{len(cpp_files)}", flush=True, file=sys.stderr)
        try:
            result = subprocess.run(
                ["clang-tidy", str(file_path)] + args,
                capture_output=True,
                text=True,
                timeout=25,
                cwd=str(PROJECT_ROOT),
            )
            for line in result.stdout.split("\n"):
                if "warning:" in line and check in line:
                    files_with_naming.append(str(file_path))
                    break
        except (subprocess.TimeoutExpired, Exception):
            pass

    files_with_naming.sort()
    for f in files_with_naming:
        print(f)
    print(f"\n# Total: {len(files_with_naming)} files with {check} warnings", file=sys.stderr)


if __name__ == "__main__":
    main()
