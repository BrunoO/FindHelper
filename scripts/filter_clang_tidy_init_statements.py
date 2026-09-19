#!/usr/bin/env python3
"""
Filter clang-tidy warnings for cppcoreguidelines-init-variables that are false positives
from C++17 init-statements.

This script filters out warnings where the variable is declared in a C++17 init-statement
(e.g., if (int x = foo(); x > 0) or switch (auto y = expr(); y)).

Usage:
    clang-tidy -p . file.cpp 2>&1 | python3 scripts/filter_clang_tidy_init_statements.py
    OR
    python3 scripts/filter_clang_tidy_init_statements.py < clang-tidy-output.txt
"""

import re
import sys
from pathlib import Path


def is_init_statement_warning(file_path: str, line_num: int, var_name: str) -> bool:
    """
    Check if a warning is for a variable declared in a C++17 init-statement.
    
    Args:
        file_path: Path to the source file
        line_num: Line number of the warning
        var_name: Name of the variable
    
    Returns:
        True if the variable is in an init-statement, False otherwise
    """
    try:
        file = Path(file_path)
        if not file.exists():
            return False
        
        with open(file, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
        
        if line_num < 1 or line_num > len(lines):
            return False
        
        # Get the line with the variable declaration
        line = lines[line_num - 1]
        
        # Pattern 1: if (type var = ...; condition)
        # Pattern 2: switch (type var = ...; var)
        # Pattern 3: for (type var = ...; condition; increment)
        # Pattern 4: while (type var = ...; condition)
        
        # Check if this line contains an init-statement pattern
        # Look for: if/switch/for/while followed by (type var = ...; or type var(...);
        # Pattern: if (type var = ...; condition) or if (const type var = ...; condition)
        # We need to match the variable name followed by = and then a semicolon before the condition
        
        # More flexible pattern: look for if/switch/for/while, then opening paren,
        # then optional const, then type, then variable name, then =, then anything, then semicolon
        init_statement_patterns = [
            # if (type var = ...; condition) - matches: if (const size_t first_sep = ...; ...)
            rf'\bif\s*\(\s*(?:const\s+)?(?:\w+\s+)*{re.escape(var_name)}\s*=\s*[^;]+;',
            # switch (type var = ...; var) - matches: switch (PatternType type = ...; type)
            rf'\bswitch\s*\(\s*(?:const\s+)?(?:\w+\s+)*{re.escape(var_name)}\s*=\s*[^;]+;',
            # for (type var = ...; condition; increment)
            rf'\bfor\s*\(\s*(?:const\s+)?(?:\w+\s+)*{re.escape(var_name)}\s*=\s*[^;]+;',
            # while (type var = ...; condition) - less common but possible
            rf'\bwhile\s*\(\s*(?:const\s+)?(?:\w+\s+)*{re.escape(var_name)}\s*=\s*[^;]+;',
        ]
        
        for pattern in init_statement_patterns:
            if re.search(pattern, line):
                return True
        
        # Also check if the variable is in a structured binding (C++17)
        # auto [var1, var2] = ...
        if re.search(rf'\bauto\s*\[[^\]]*{re.escape(var_name)}', line):
            return True
        
        return False
    
    except Exception:
        # If we can't read the file or parse it, don't filter (show the warning)
        return False


def filter_clang_tidy_output(input_lines):
    """
    Filter clang-tidy output to remove init-statement false positives.
    
    Args:
        input_lines: Iterable of input lines from clang-tidy
    
    Yields:
        Lines that should be shown (filtered output)
    """
    warning_pattern = re.compile(
        r'^([^:]+):(\d+):(\d+):\s+warning:\s+variable\s+[\'"](\w+)[\'"]\s+is\s+not\s+initialized\s+\[cppcoreguidelines-init-variables\]'
    )
    
    for line in input_lines:
        match = warning_pattern.match(line)
        if match:
            file_path = match.group(1)
            line_num = int(match.group(2))
            var_name = match.group(4)
            
            # Check if this is an init-statement (false positive)
            if is_init_statement_warning(file_path, line_num, var_name):
                # Skip this warning (it's a false positive)
                continue
        
        # Show all other lines (including non-init-variables warnings)
        yield line


def main():
    """Main entry point."""
    if len(sys.argv) > 1 and sys.argv[1] in ('-h', '--help'):
        print(__doc__)
        sys.exit(0)
    
    # Read from stdin
    try:
        for line in filter_clang_tidy_output(sys.stdin):
            print(line, end='')
    except KeyboardInterrupt:
        sys.exit(1)
    except BrokenPipeError:
        # Handle broken pipe gracefully (e.g., when piped to head)
        sys.exit(0)


if __name__ == '__main__':
    main()
