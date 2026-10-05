#!/bin/bash
# Shared clang-tidy failure patterns. Sourced by pre-commit-clang-tidy.sh and
# run_clang_tidy.sh so both decide "the toolchain is broken" the same way.
#
# Defines:
#   STDLIB_HEADERS      - alternation of C/C++ standard library header names
#   TOOLCHAIN_FAILURE_RE - extended regex matching diagnostics that mean
#                          clang-tidy failed to parse a translation unit
#
# A missing standard-library header belongs in this set. clang-tidy does not stop
# when an #include fails: it goes on to analyze a degraded AST that both invents
# findings and hides real ones. Both scripts then discard those fake diagnostics
# as cross-platform noise, so a completely broken analysis reads as a clean pass.
# On macOS the cause is almost always xcrun failing inside
# scripts/clang-tidy-wrapper.sh, which then injects no --extra-arg and the C++
# standard library leaves the include path entirely.
#
# Matched by header name, never by the generic "'X' file not found" form, which is
# what keeps this from breaking legitimate commits: 'windows.h', 'WinSock2.h',
# 'Ntifs.h' and curl's own headers stay non-blocking. A standard library header
# can only produce this diagnostic if clang tried to include it and failed, so a
# translation unit that simply does not include one never matches.
STDLIB_HEADERS='cstdint|cstddef|cstdio|cstdlib|cstring|cmath|climits|cfloat'
STDLIB_HEADERS+='|ctime|cctype|cerrno|array|algorithm|utility|vector|string'
STDLIB_HEADERS+='|memory|functional|type_traits|optional|variant|tuple|iterator'
STDLIB_HEADERS+='|limits|stdexcept|exception|initializer_list|numeric|ratio'
STDLIB_HEADERS+='|chrono|atomic|thread|mutex|future|queue|map|set'
STDLIB_HEADERS+='|unordered_map|unordered_set'

TOOLCHAIN_FAILURE_RE='uses an older format that is no longer supported'
TOOLCHAIN_FAILURE_RE+='|unable to handle compilation|expected exactly one compiler job'
TOOLCHAIN_FAILURE_RE+='|no input files|PCH file .* not found|unable to read PCH file'
TOOLCHAIN_FAILURE_RE+="|'($STDLIB_HEADERS)' file not found"

# Reports whether stdin (clang-tidy output) indicates a broken toolchain rather
# than a finding in the code. These are never filtered: a toolchain or database
# problem silently turning a check into a no-op is the exact failure both scripts
# exist to prevent.
is_toolchain_failure() {
    grep -qE "$TOOLCHAIN_FAILURE_RE"
}

# True when the failure is specifically a vanished standard library, which is not
# a database problem: regenerating compile_commands.json cannot fix it, so the
# caller must not send the reader down that path.
is_missing_stdlib_failure() {
    grep -qE "'($STDLIB_HEADERS)' file not found"
}

# Prints the remediation for a broken toolchain. A missing SDK header needs
# different advice from a stale database, and giving only the database steps
# sends the reader through work guaranteed not to help.
print_toolchain_failure_help() {
    echo "Regenerate the database with:"
    echo "  cmake -B build_tests -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    echo "  python3 scripts/make_clang_tidy_compile_db.py"
    if is_missing_stdlib_failure; then
        echo ""
        echo "A C++ standard library header was not found, so clang-tidy analyzed a"
        echo "degraded AST and its findings are meaningless. Check that the SDK is"
        echo "visible to the wrapper:"
        echo "  xcrun --show-sdk-path"
        echo "  ls scripts/clang-tidy-wrapper.sh"
    fi
}