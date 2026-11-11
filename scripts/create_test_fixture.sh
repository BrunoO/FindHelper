#!/bin/bash
# Recreate tests/data/fixture/ — the known-structure directory used by FolderCrawlerTests.
#
# Run this whenever the fixture needs to be regenerated from scratch (e.g., after an
# accidental deletion or a structural change to the expected layout).
#
# Expected result after running:
#   10 files  (sizes follow the Fibonacci sequence: 1,1,2,3,5,8,13,21,34,55 bytes)
#    6 dirs   (alpha  beta  beta/sub  gamma  gamma/deep  gamma/deep/deeper)
#   16 total index entries (what FolderCrawler inserts into FileIndex)
#
# Usage:
#   scripts/create_test_fixture.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIXTURE_DIR="$SCRIPT_DIR/../tests/data/fixture"

echo "Recreating $FIXTURE_DIR ..."

rm -rf "$FIXTURE_DIR"

mkdir -p \
    "$FIXTURE_DIR/alpha" \
    "$FIXTURE_DIR/beta/sub" \
    "$FIXTURE_DIR/gamma/deep/deeper"

# Helper: write exactly N bytes of 'A' to a file.
write_bytes() {
    local n=$1
    local path=$2
    python3 -c "import sys; sys.stdout.buffer.write(b'A' * $n)" > "$path"
}

# alpha/ — Fibonacci 1, 1, 2
write_bytes  1  "$FIXTURE_DIR/alpha/a.txt"
write_bytes  1  "$FIXTURE_DIR/alpha/b.txt"
write_bytes  2  "$FIXTURE_DIR/alpha/c.cpp"

# beta/ — Fibonacci 3, 5, 8
write_bytes  3  "$FIXTURE_DIR/beta/x.h"
write_bytes  5  "$FIXTURE_DIR/beta/y.json"
write_bytes  8  "$FIXTURE_DIR/beta/z.md"

# beta/sub/ — Fibonacci 13, 21
write_bytes 13  "$FIXTURE_DIR/beta/sub/p.txt"
write_bytes 21  "$FIXTURE_DIR/beta/sub/q.cpp"

# gamma/ — Fibonacci 55
write_bytes 55  "$FIXTURE_DIR/gamma/r.txt"

# gamma/deep/deeper/ — Fibonacci 34
write_bytes 34  "$FIXTURE_DIR/gamma/deep/deeper/leaf.h"

echo ""
echo "Files (path, size in bytes):"
find "$FIXTURE_DIR" -type f | sort | while read -r f; do
    printf "  %3d B  %s\n" "$(wc -c < "$f")" "${f#$FIXTURE_DIR/}"
done

echo ""
echo "Directories:"
find "$FIXTURE_DIR" -type d | sort | while read -r d; do
    echo "  ${d#$FIXTURE_DIR/}"
done

FILE_COUNT=$(find "$FIXTURE_DIR" -type f | wc -l | tr -d ' ')
DIR_COUNT=$(find "$FIXTURE_DIR" -mindepth 1 -type d | wc -l | tr -d ' ')
echo ""
echo "Summary: $FILE_COUNT files, $DIR_COUNT dirs (= $((FILE_COUNT + DIR_COUNT)) total index entries)"
echo "Done."
