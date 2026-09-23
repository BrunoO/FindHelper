#!/usr/bin/env python3
"""Find class/struct declaration mismatches that cause C4099 warnings on Windows.

This script scans C++ header and source files to find cases where:
- A type is forward declared as 'class' but defined as 'struct' (or vice versa)
- Multiple forward declarations use different keywords for the same type

Warning C4099 occurs when MSVC sees a forward declaration with one keyword
but the actual definition uses a different keyword.

Usage:
  python scripts/find_class_struct_mismatches.py [--root <path>] [--verbose]

Runs from the repo root by default.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

# Directories that should be excluded
EXCLUDED_DIR_NAMES = {
    ".git",
    ".idea",
    ".vscode",
    "build",
    "dist",
    "external",
    "third_party",
    "vendor",
    "node_modules",
    "windirstat",
    "coverage",
    "build_tests",
    "build_coverage",
    "build_test_linux",
    "build_test_linux_check",
    "build_test_linux_check2",
}

EXCLUDED_DIR_PREFIXES = ("cmake-build-",)

# File extensions to scan
INCLUDED_FILE_EXTENSIONS = {".h", ".hpp", ".cpp", ".cxx", ".cc"}

# Regex patterns
# Forward declaration: "class X;" or "struct X;" (may have leading whitespace)
FORWARD_DECL_PATTERN = re.compile(
    r"^\s*(class|struct)\s+(\w+)\s*;", re.MULTILINE
)

# Definition: "class X {" or "struct X {" (may have leading whitespace, template, etc.)
# This is more complex because we need to handle:
# - Templates: "template <...> class X {"
# - Nested: "class Outer { class Inner {"
# - Attributes: "__attribute__((...)) struct X {"
DEFINITION_PATTERN = re.compile(
    r"^\s*(?:template\s*<[^>]*>\s*)?(?:__attribute__\s*\([^)]*\)\s*)?"
    r"(class|struct)\s+(\w+)\s*[:<{]",
    re.MULTILINE
)


def is_excluded_dir(dirname: str) -> bool:
    """Return True if a directory name should be skipped entirely."""
    if dirname in EXCLUDED_DIR_NAMES:
        return True
    return any(dirname.startswith(prefix) for prefix in EXCLUDED_DIR_PREFIXES)


def should_scan_file(path: Path) -> bool:
    """Return True if the file should be scanned."""
    return path.suffix.lower() in INCLUDED_FILE_EXTENSIONS


def extract_forward_declarations(content: str) -> Dict[str, str]:
    """Extract forward declarations from file content.
    
    Returns: Dict mapping type name to keyword ('class' or 'struct')
    """
    forward_decls: Dict[str, str] = {}
    
    for match in FORWARD_DECL_PATTERN.finditer(content):
        keyword = match.group(1)
        type_name = match.group(2)
        forward_decls[type_name] = keyword
    
    return forward_decls


def extract_definitions(content: str) -> Dict[str, str]:
    """Extract type definitions from file content.
    
    Returns: Dict mapping type name to keyword ('class' or 'struct')
    """
    definitions: Dict[str, str] = {}
    
    for match in DEFINITION_PATTERN.finditer(content):
        keyword = match.group(1)
        type_name = match.group(2)
        definitions[type_name] = keyword
    
    return definitions


def scan_file(path: Path) -> Tuple[Dict[str, str], Dict[str, str]]:
    """Scan a single file for forward declarations and definitions.
    
    Returns: (forward_declarations, definitions) where each is a dict
             mapping type name to keyword.
    """
    try:
        with path.open("r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
    except OSError:
        return {}, {}
    
    forward_decls = extract_forward_declarations(content)
    definitions = extract_definitions(content)
    
    return forward_decls, definitions


def scan_directory(root: Path) -> Tuple[
    Dict[str, Dict[str, str]],  # forward_decls: type_name -> {file -> keyword}
    Dict[str, Dict[str, str]],  # definitions: type_name -> {file -> keyword}
]:
    """Scan all C++ files in directory tree.
    
    Returns:
        forward_decls: Dict mapping type name to dict of {file_path: keyword}
        definitions: Dict mapping type name to dict of {file_path: keyword}
    """
    forward_decls: Dict[str, Dict[str, str]] = defaultdict(dict)
    definitions: Dict[str, Dict[str, str]] = defaultdict(dict)
    
    for dirpath, dirnames, filenames in os.walk(root):
        # Prune excluded directories in-place so os.walk does not descend
        dirnames[:] = [d for d in dirnames if not is_excluded_dir(d)]
        
        for filename in filenames:
            file_path = Path(dirpath) / filename
            if not should_scan_file(file_path):
                continue
            
            file_forward_decls, file_definitions = scan_file(file_path)
            
            # Add forward declarations
            for type_name, keyword in file_forward_decls.items():
                forward_decls[type_name][str(file_path.relative_to(root))] = keyword
            
            # Add definitions
            for type_name, keyword in file_definitions.items():
                definitions[type_name][str(file_path.relative_to(root))] = keyword
    
    return forward_decls, definitions


def find_mismatches(
    forward_decls: Dict[str, Dict[str, str]],
    definitions: Dict[str, Dict[str, str]],
) -> List[Tuple[str, str, str, str, List[str]]]:
    """Find mismatches between forward declarations and definitions.
    
    Returns: List of (type_name, forward_keyword, definition_keyword, 
                      definition_file, forward_decl_files) tuples
    """
    mismatches: List[Tuple[str, str, str, str, List[str]]] = []
    
    # Check types that have both forward declarations and definitions
    for type_name in set(forward_decls.keys()) & set(definitions.keys()):
        forward_decl_files = forward_decls[type_name]
        definition_files = definitions[type_name]
        
        # Get the keyword used in definitions (should be consistent)
        definition_keywords = set(definition_files.values())
        if len(definition_keywords) != 1:
            # Multiple definitions with different keywords - this is also a problem
            mismatches.append((
                type_name,
                "multiple",
                f"multiple ({', '.join(sorted(definition_keywords))})",
                ", ".join(sorted(definition_files.keys())),
                sorted(forward_decl_files.keys()),
            ))
            continue
        
        definition_keyword = definition_keywords.pop()
        definition_file = sorted(definition_files.keys())[0]  # Use first definition
        
        # Check if any forward declaration uses a different keyword
        mismatched_forwards = [
            (file, keyword)
            for file, keyword in forward_decl_files.items()
            if keyword != definition_keyword
        ]
        
        if mismatched_forwards:
            forward_keywords = set(forward_decl_files.values())
            forward_keyword_str = ", ".join(sorted(forward_keywords))
            forward_files = [file for file, _ in mismatched_forwards]
            
            mismatches.append((
                type_name,
                forward_keyword_str,
                definition_keyword,
                definition_file,
                forward_files,
            ))
    
    return mismatches


def find_inconsistent_forwards(
    forward_decls: Dict[str, Dict[str, str]],
) -> List[Tuple[str, Dict[str, str]]]:
    """Find types with inconsistent forward declarations (same type declared
    as both 'class' and 'struct' in different files).
    
    Returns: List of (type_name, {file: keyword}) tuples
    """
    inconsistencies: List[Tuple[str, Dict[str, str]]] = []
    
    for type_name, files in forward_decls.items():
        keywords = set(files.values())
        if len(keywords) > 1:
            inconsistencies.append((type_name, files))
    
    return inconsistencies


def report_mismatches(mismatches: List[Tuple[str, str, str, str, List[str]]]) -> None:
    """Report mismatches between forward declarations and definitions."""
    if not mismatches:
        return
    
    print("=" * 80)
    print("❌ MISMATCHES FOUND (Forward declaration keyword != Definition keyword)")
    print("=" * 80)
    print()
    
    for type_name, forward_keyword, definition_keyword, definition_file, forward_files in sorted(mismatches):
        print(f"Type: {type_name}")
        print(f"  Forward declared as: {forward_keyword}")
        print(f"  Defined as:         {definition_keyword}")
        print(f"  Definition in:      {definition_file}")
        print(f"  Forward decls in:   {', '.join(forward_files)}")
        print()
    
    print(f"\nTotal mismatches: {len(mismatches)}")
    print()


def report_inconsistent_forwards(inconsistent_forwards: List[Tuple[str, Dict[str, str]]]) -> None:
    """Report inconsistent forward declarations."""
    if not inconsistent_forwards:
        return
    
    print("=" * 80)
    print("⚠️  INCONSISTENT FORWARD DECLARATIONS")
    print("=" * 80)
    print("(Same type forward declared as both 'class' and 'struct' in different files)")
    print()
    
    for type_name, files in sorted(inconsistent_forwards):
        print(f"Type: {type_name}")
        for file, keyword in sorted(files.items()):
            print(f"  {file}: {keyword}")
        print()
    
    print(f"\nTotal inconsistent forward declarations: {len(inconsistent_forwards)}")
    print()


def report_verbose_output(
    forward_decls: Dict[str, Dict[str, str]],
    definitions: Dict[str, Dict[str, str]]
) -> None:
    """Report verbose output showing all forward declarations and definitions."""
    print("=" * 80)
    print("VERBOSE: All forward declarations and definitions")
    print("=" * 80)
    print()
    
    print("Forward declarations:")
    for type_name in sorted(forward_decls.keys()):
        files = forward_decls[type_name]
        keywords = set(files.values())
        print(f"  {type_name}: {', '.join(sorted(keywords))} in {len(files)} file(s)")
    
    print()
    print("Definitions:")
    for type_name in sorted(definitions.keys()):
        files = definitions[type_name]
        keywords = set(files.values())
        print(f"  {type_name}: {', '.join(sorted(keywords))} in {len(files)} file(s)")


def main() -> int:
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="Find class/struct declaration mismatches (C4099 warnings)"
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=None,
        help="Root directory to scan (default: script directory)",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Show all forward declarations and definitions",
    )
    args = parser.parse_args()
    
    if args.root is None:
        script_dir = Path(__file__).resolve().parent
        repo_root = script_dir.parent
    else:
        repo_root = args.root.resolve()
    
    if not repo_root.is_dir():
        print(f"Error: {repo_root} is not a directory", file=sys.stderr)
        return 1
    
    print(f"Scanning C++ files in: {repo_root}")
    print("This may take a moment...\n")
    
    forward_decls, definitions = scan_directory(repo_root)
    
    # Find mismatches
    mismatches = find_mismatches(forward_decls, definitions)
    
    # Find inconsistent forward declarations
    inconsistent_forwards = find_inconsistent_forwards(forward_decls)
    
    # Report results
    exit_code = 0
    
    if mismatches:
        exit_code = 1
        report_mismatches(mismatches)
    
    if inconsistent_forwards:
        exit_code = 1
        report_inconsistent_forwards(inconsistent_forwards)
    
    if exit_code == 0:
        print("=" * 80)
        print("✅ No class/struct mismatches found!")
        print("=" * 80)
        print()
    
    if args.verbose:
        report_verbose_output(forward_decls, definitions)
    
    return exit_code


if __name__ == "__main__":
    sys.exit(main())

