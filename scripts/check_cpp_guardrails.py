#!/usr/bin/env python3
"""
check_cpp_guardrails.py — Fast static check for SonarQube rules and C++/MSVC guardrails.

Catches issues locally and in CI before they reach SonarCloud or fail on MSVC:
  - cpp:S107: Maximum 7 parameters per function declaration/definition
  - cpp:S954: Move forward declarations after all #include directives in headers
  - cpp:S8460: Disallow std::async (use thread pool or explicit threads)
  - cpp:S2807: Make overloaded operators hidden friends of classes
  - cpp:S6012: Avoid explicit template arguments when CTAD can deduce them (locks, atomics)
  - msvc:min-max: Enforce parenthesized (std::min) and (std::max)
  - msvc:C4099: Detect struct vs class forward declaration tag mismatches
  - msvc:declaration-parity: Verify .cpp method definitions are declared in class headers

Usage:
  python3 scripts/check_cpp_guardrails.py [--files file1 file2 ...]
  python3 scripts/check_cpp_guardrails.py --staged
  python3 scripts/check_cpp_guardrails.py [directories...]
"""

import argparse
import os
import re
import subprocess
import sys
from typing import List, NamedTuple, Set


class Issue(NamedTuple):
    file_path: str
    line_number: int
    rule: str
    severity: str
    message: str


def is_suppressed(line: str, rule: str) -> bool:
    """Check if the line contains a NOSONAR or NOLINT suppression for this rule."""
    if "// NOSONAR" in line or "/* NOSONAR */" in line:
        if f"NOSONAR({rule})" in line or "NOSONAR" in line:
            return True
    if "// NOLINT" in line or "/* NOLINT */" in line:
        return True
    return False


def strip_comments_and_strings(content: str) -> str:
    """Replace comments and string literals with spaces to preserve line numbers/offsets."""
    result = []
    i = 0
    n = len(content)
    while i < n:
        if content[i : i + 2] == "/*":
            end = content.find("*/", i + 2)
            if end == -1:
                end = n
            else:
                end += 2
            comment = content[i:end]
            result.append("".join("\n" if c == "\n" else " " for c in comment))
            i = end
        elif content[i : i + 2] == "//":
            end = content.find("\n", i + 2)
            if end == -1:
                end = n
            comment = content[i:end]
            result.append("".join(" " for _ in comment))
            i = end
        elif content[i] == '"' and (i == 0 or content[i - 1] != "\\"):
            end = i + 1
            while end < n:
                if content[end] == '"' and content[end - 1] != "\\":
                    end += 1
                    break
                end += 1
            s = content[i:end]
            result.append("".join("\n" if c == "\n" else " " for c in s))
            i = end
        else:
            result.append(content[i])
            i += 1
    return "".join(result)


def count_parameters(param_str: str) -> int:
    """Count comma-separated parameters accounting for templates and nested parens."""
    params = []
    current = []
    angle_depth = 0
    paren_depth = 0
    brace_depth = 0

    for ch in param_str:
        if ch == "<":
            angle_depth += 1
        elif ch == ">":
            if angle_depth > 0:
                angle_depth -= 1
        elif ch == "(":
            paren_depth += 1
        elif ch == ")":
            if paren_depth > 0:
                paren_depth -= 1
        elif ch == "{":
            brace_depth += 1
        elif ch == "}":
            if brace_depth > 0:
                brace_depth -= 1
        elif ch == "," and angle_depth == 0 and paren_depth == 0 and brace_depth == 0:
            p = "".join(current).strip()
            if p:
                params.append(p)
            current = []
            continue
        current.append(ch)

    last = "".join(current).strip()
    if last and last != "void":
        params.append(last)

    return len(params)


def is_function_declaration(raw_params: str) -> bool:
    """Heuristic to check if parameter string looks like a declaration (types present) vs call (values only)."""
    param_tokens = [p.strip() for p in raw_params.split(",") if p.strip()]
    if not param_tokens:
        return False
    typed_count = 0
    type_indicators = {
        "const", "*", "&", "int", "bool", "size_t", "uint64_t", "uint32_t",
        "float", "double", "char", "void", "auto", "std::", "string_view", "string",
        "vector", "shared_ptr", "unique_ptr", "atomic", "FILETIME", "HANDLE"
    }
    for p in param_tokens:
        if any(ind in p for ind in type_indicators) or len(p.split()) >= 2:
            typed_count += 1
    return typed_count >= len(param_tokens) * 0.7


def check_s107_parameter_count(file_path: str, lines: List[str]) -> List[Issue]:
    """Flag functions with more than 7 parameters (cpp:S107)."""
    issues = []
    full_text = "".join(lines)
    stripped = strip_comments_and_strings(full_text)

    # Match function-like signatures: <return_type> <name>(<params>) [const] [noexcept] [{;]
    pattern = re.compile(r"(\b[A-Za-z0-9_::~]+)\s*\(([^)]*)\)\s*(?:const|noexcept|\s)*(\{|;)", re.DOTALL)
    for m in pattern.finditer(stripped):
        name = m.group(1).strip()
        if name in ("if", "while", "for", "switch", "catch", "return", "sizeof", "decltype", "static_assert"):
            continue
        if name.isupper():
            continue

        raw_params = m.group(2)
        if not is_function_declaration(raw_params):
            continue

        param_count = count_parameters(raw_params)
        if param_count > 7:
            line_no = full_text[: m.start()].count("\n") + 1
            orig_line = lines[line_no - 1]
            if not is_suppressed(orig_line, "cpp:S107") and not is_suppressed(orig_line, "S107"):
                issues.append(
                    Issue(
                        file_path,
                        line_no,
                        "cpp:S107",
                        "MAJOR",
                        f"Function '{name}' has {param_count} parameters, which exceeds the limit of 7.",
                    )
                )
    return issues


def check_s954_include_order(file_path: str, lines: List[str]) -> List[Issue]:
    """Flag forward declarations placed before #include directives in header files (cpp:S954)."""
    if not (file_path.endswith(".h") or file_path.endswith(".hpp")):
        return []

    issues = []
    first_include_line = -1
    forward_decl_lines = []

    in_block_comment = False
    for idx, line in enumerate(lines, start=1):
        s = line.strip()
        if "/*" in s:
            in_block_comment = True
        if "*/" in s:
            in_block_comment = False
            continue
        if in_block_comment or s.startswith("//"):
            continue

        if s.startswith("#include"):
            if first_include_line == -1:
                first_include_line = idx
            continue

        # Look for forward declarations: class Foo;, struct Bar;, namespace X { class Foo; }
        # before any include
        if first_include_line == -1:
            if re.match(r"^(?:class|struct)\s+[A-Za-z0-9_]+(?:\s*:\s*[^{;]+)?\s*;", s):
                forward_decl_lines.append((idx, line))
            elif re.match(r"^namespace\s+[A-Za-z0-9_]+\s*\{", s):
                forward_decl_lines.append((idx, line))

    if first_include_line != -1 and forward_decl_lines:
        for line_no, raw_line in forward_decl_lines:
            if not is_suppressed(raw_line, "cpp:S954") and not is_suppressed(raw_line, "S954"):
                issues.append(
                    Issue(
                        file_path,
                        line_no,
                        "cpp:S954",
                        "MAJOR",
                        f"Forward declaration before #include directives (first #include at line {first_include_line}). Move after includes.",
                    )
                )

    return issues


def check_s8460_std_async(file_path: str, lines: List[str]) -> List[Issue]:
    """Disallow std::async (cpp:S8460)."""
    issues = []
    for idx, line in enumerate(lines, start=1):
        if is_suppressed(line, "cpp:S8460") or is_suppressed(line, "S8460"):
            continue
        s = line.strip()
        if s.startswith("//") or s.startswith("/*") or s.startswith("*"):
            continue
        if "std::async" in line and "(" in line:
            issues.append(
                Issue(
                    file_path,
                    idx,
                    "cpp:S8460",
                    "MAJOR",
                    "Do not use 'std::async'; use thread pool or explicit threading mechanisms instead.",
                )
            )
    return issues


def check_s2807_hidden_friends(file_path: str, lines: List[str]) -> List[Issue]:
    """Flag non-member binary operators defined at namespace scope instead of hidden friends (cpp:S2807)."""
    if not (file_path.endswith(".h") or file_path.endswith(".hpp")):
        return []

    issues = []
    pattern = re.compile(
        r"^(?:\[\[nodiscard\]\]\s*)?(?:constexpr\s+)?([A-Za-z0-9_:]+)\s+operator(\||&|\^|==|!=)\s*\(([^)]+)\)\s*(?:noexcept)?\s*\{"
    )

    for idx, line in enumerate(lines, start=1):
        s = line.strip()
        m = pattern.match(s)
        if m and "friend" not in s:
            raw_params = m.group(3)
            params = count_parameters(raw_params)
            if params == 2:
                if not is_suppressed(line, "cpp:S2807") and not is_suppressed(line, "S2807"):
                    issues.append(
                        Issue(
                            file_path,
                            idx,
                            "cpp:S2807",
                            "MAJOR",
                            f"Make overloaded operator '{m.group(2)}' a hidden friend of its operand class.",
                        )
                    )
    return issues


def check_s6012_ctad(file_path: str, lines: List[str]) -> List[Issue]:
    """Flag explicit template arguments when CTAD can deduce them (cpp:S6012)."""
    issues = []
    # 1. Lock types with explicit template parameters where constructor arg deduces it
    lock_pattern = re.compile(r"\b(?:const\s+)?std::(unique_lock|scoped_lock|lock_guard)<[^>]+>\s+([A-Za-z0-9_]+)\s*\(")
    # 2. Local atomic boolean initialization
    atomic_pattern = re.compile(r"\bstd::atomic<bool>\s+([A-Za-z0-9_]+)\s*\{")

    for idx, line in enumerate(lines, start=1):
        if is_suppressed(line, "cpp:S6012") or is_suppressed(line, "S6012"):
            continue
        s = line.strip()
        if s.startswith("//") or s.startswith("/*") or s.startswith("*"):
            continue

        if m := lock_pattern.search(line):
            lock_kind = m.group(1)
            issues.append(
                Issue(
                    file_path,
                    idx,
                    "cpp:S6012",
                    "MINOR",
                    f"Rely on Class Template Argument Deduction (CTAD) instead of specifying std::{lock_kind}<...> explicitly.",
                )
            )
        elif atomic_pattern.search(line) and not (file_path.endswith(".h") and ("private:" in line or ";" in line)):
            # Only flag local variable atomics initialized with braces
            issues.append(
                Issue(
                    file_path,
                    idx,
                    "cpp:S6012",
                    "MINOR",
                    "Rely on Class Template Argument Deduction (CTAD) instead of specifying <bool> explicitly (use 'std::atomic var{val}').",
                )
            )
    return issues


def check_parenthesized_min_max(file_path: str, lines: List[str]) -> List[Issue]:
    """Flag unparenthesized std::min/std::max (project rule: always use (std::min)/(std::max))."""
    issues = []
    call_pattern = re.compile(r"\bstd::(min|max)(?:\s*<[^>]*>)?\s*\(")

    for idx, line in enumerate(lines, start=1):
        if is_suppressed(line, "min-max") or is_suppressed(line, "windows-msvc"):
            continue
        s = line.strip()
        if s.startswith("//") or s.startswith("/*") or s.startswith("*") or s.startswith("#include"):
            continue
        code_part = line
        if "//" in code_part:
            code_part = code_part[: code_part.index("//")]

        for m in call_pattern.finditer(code_part):
            start = m.start()
            pre = code_part[:start].rstrip()
            match_str = m.group(0)
            arg_paren_idx = start + match_str.rfind("(")
            between = code_part[start:arg_paren_idx].strip()
            has_closing = between.endswith(")")
            has_opening = pre.endswith("(")
            if not (has_opening and has_closing):
                func = m.group(1)
                issues.append(
                    Issue(
                        file_path,
                        idx,
                        "msvc:min-max",
                        "MAJOR",
                        f"Use parenthesized '(std::{func})' instead of unparenthesized 'std::{func}' to prevent MSVC <windows.h> macro conflicts.",
                    )
                )
    return issues


_CLASS_METHOD_MAP = None
_CLASS_DEF_TAG_MAP = None


def get_class_metadata():
    """Build or retrieve cached index of class names, their tags (class vs struct), and declared identifiers."""
    global _CLASS_METHOD_MAP, _CLASS_DEF_TAG_MAP
    if _CLASS_METHOD_MAP is not None:
        return _CLASS_METHOD_MAP, _CLASS_DEF_TAG_MAP

    _CLASS_METHOD_MAP = {}
    _CLASS_DEF_TAG_MAP = {}

    header_files = collect_repo_files(["src"])
    class_decl_pattern = re.compile(r"\b(class|struct)\s+(?:\[\[[^\]]*\]\]\s*)?([A-Za-z0-9_]+)\b[^{;]*\{")

    for h in header_files:
        if not (h.endswith(".h") or h.endswith(".hpp")):
            continue
        try:
            with open(h, "r", encoding="utf-8", errors="replace") as f:
                content = f.read()
        except OSError:
            continue

        stripped = strip_comments_and_strings(content)
        for m in class_decl_pattern.finditer(stripped):
            tag = m.group(1)
            cls_name = m.group(2)
            if cls_name not in _CLASS_DEF_TAG_MAP:
                _CLASS_DEF_TAG_MAP[cls_name] = (tag, h)

            start_idx = m.end() - 1
            brace_depth = 0
            end_idx = start_idx
            for i in range(start_idx, len(stripped)):
                if stripped[i] == "{":
                    brace_depth += 1
                elif stripped[i] == "}":
                    brace_depth -= 1
                    if brace_depth == 0:
                        end_idx = i
                        break

            if end_idx > start_idx:
                body = stripped[start_idx + 1 : end_idx]
                tokens = set(re.findall(r"\b[A-Za-z0-9_]+\b", body))
                if cls_name not in _CLASS_METHOD_MAP:
                    _CLASS_METHOD_MAP[cls_name] = set()
                _CLASS_METHOD_MAP[cls_name].update(tokens)

    return _CLASS_METHOD_MAP, _CLASS_DEF_TAG_MAP


def check_msvc_tag_mismatch(file_path: str, lines: List[str]) -> List[Issue]:
    """Flag forward declarations where struct vs class tag mismatches definition (MSVC warning/error C4099)."""
    if not (file_path.endswith(".h") or file_path.endswith(".hpp")):
        return []

    _, class_tags = get_class_metadata()
    issues = []
    fwd_pattern = re.compile(r"^\s*(class|struct)\s+([A-Za-z0-9_]+)\s*;")

    for idx, line in enumerate(lines, start=1):
        if is_suppressed(line, "msvc:C4099") or is_suppressed(line, "C4099"):
            continue
        s = line.strip()
        if "//" in s:
            s = s[: s.index("//")].strip()
        if m := fwd_pattern.match(s):
            fwd_tag, name = m.group(1), m.group(2)
            if name in class_tags:
                def_tag, def_file = class_tags[name]
                if fwd_tag != def_tag:
                    issues.append(
                        Issue(
                            file_path,
                            idx,
                            "msvc:C4099",
                            "MAJOR",
                            f"Tag mismatch: forward declared as '{fwd_tag} {name};', but defined as '{def_tag} {name}' in {def_file}. MSVC generates warning/error C4099 due to symbol name mangling mismatch.",
                        )
                    )
    return issues


def check_msvc_declaration_parity(file_path: str, lines: List[str]) -> List[Issue]:
    """Verify methods defined in .cpp are declared in their class definition (prevents MSVC C2039/C2065)."""
    if not (file_path.endswith(".cpp") or file_path.endswith(".cxx") or file_path.endswith(".cc")):
        return []

    class_methods, _ = get_class_metadata()
    issues = []
    method_def_pattern = re.compile(r"\b([A-Za-z0-9_]+)::([A-Za-z0-9_~]+)\s*\([^;]*\)\s*(?:const|noexcept|\s)*\{")

    full_text = "".join(lines)
    stripped = strip_comments_and_strings(full_text)

    for m in method_def_pattern.finditer(stripped):
        cls, method = m.group(1), m.group(2)
        if cls in ("std", "chrono", "filesystem", "this_thread"):
            continue
        if method.startswith("~"):
            continue

        if cls in class_methods:
            if method not in class_methods[cls]:
                line_no = full_text[: m.start()].count("\n") + 1
                orig_line = lines[line_no - 1]
                if not is_suppressed(orig_line, "msvc:declaration-parity"):
                    issues.append(
                        Issue(
                            file_path,
                            line_no,
                            "msvc:declaration-parity",
                            "MAJOR",
                            f"Method '{cls}::{method}' is defined here, but is not declared in class '{cls}'. This causes MSVC compiler errors C2039/C2065.",
                        )
                    )
    return issues


def scan_file(file_path: str) -> List[Issue]:
    """Run all guardrail checks on a single file."""
    norm_path = file_path.replace("\\", "/")
    if (
        norm_path.startswith("external/")
        or "/external/" in norm_path
        or "EmbeddedFont_" in norm_path
        or not re.search(r"\.(cpp|cxx|cc|h|hpp|inl)$", norm_path)
    ):
        return []

    try:
        with open(file_path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except OSError:
        return []

    issues: List[Issue] = []
    issues.extend(check_s107_parameter_count(file_path, lines))
    issues.extend(check_s954_include_order(file_path, lines))
    issues.extend(check_s8460_std_async(file_path, lines))
    issues.extend(check_s2807_hidden_friends(file_path, lines))
    issues.extend(check_s6012_ctad(file_path, lines))
    issues.extend(check_parenthesized_min_max(file_path, lines))
    issues.extend(check_msvc_tag_mismatch(file_path, lines))
    issues.extend(check_msvc_declaration_parity(file_path, lines))
    return issues


def get_staged_files() -> List[str]:
    """Get staged C++ files from git."""
    try:
        out = subprocess.check_output(
            ["git", "diff", "--cached", "--name-only", "--diff-filter=ACM"], text=True
        )
        return [line.strip() for line in out.splitlines() if line.strip()]
    except Exception:
        return []


def collect_repo_files(directories: List[str]) -> List[str]:
    """Find all C++ files under the given directories."""
    files = []
    for d in directories:
        if os.path.isfile(d):
            files.append(d)
        elif os.path.isdir(d):
            for root, _, filenames in os.walk(d):
                for fn in filenames:
                    if re.search(r"\.(cpp|cxx|cc|h|hpp|inl)$", fn):
                        files.append(os.path.join(root, fn))
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description="Check C++ quality, SonarQube rules, and MSVC guardrails.")
    parser.add_argument("--files", nargs="*", help="Specific files to check.")
    parser.add_argument("--staged", action="store_true", help="Check only staged files in git.")
    parser.add_argument("paths", nargs="*", default=["src", "tests"], help="Directories or files to scan.")

    args = parser.parse_args()

    if args.files:
        files = args.files
    elif args.staged:
        files = get_staged_files()
    else:
        files = collect_repo_files(args.paths)

    all_issues: List[Issue] = []
    for file_path in sorted(files):
        all_issues.extend(scan_file(file_path))

    if not all_issues:
        # ASCII-only markers: Windows CI consoles default to cp1252, where
        # glyphs like U+2713 raise UnicodeEncodeError and fail the step.
        print("\033[0;32m[OK] All C++ guardrails passed (0 issues found).\033[0m")
        return 0

    print(f"\033[0;31mFound {len(all_issues)} issue(s) violating C++ guardrails:\033[0m\n")
    for issue in all_issues:
        color = "\033[1;31m" if issue.severity == "MAJOR" else "\033[1;33m"
        print(f"  {issue.file_path}:{issue.line_number}: {color}[{issue.severity}] [{issue.rule}]\033[0m {issue.message}")

    print("\n\033[0;33mNote: To suppress an intentional violation on a line, add '// NOSONAR(rule-id)' or '// NOLINT'.\033[0m")
    return 1


if __name__ == "__main__":
    sys.exit(main())
