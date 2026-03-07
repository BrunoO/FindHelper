#!/usr/bin/env bash
# publish.sh — Publish current main to the public GitHub repo (findhelper remote).
#
# Usage: scripts/publish.sh
#
# Workflow:
#   1. Checks that main is clean and committed.
#   2. Switches to the orphan tracking branch (findhelper-public).
#   3. Overlays the full tree from main.
#   4. Removes internal-only content.
#   5. Commits a snapshot and force-pushes to findhelper/main.
#   6. Returns to main.
#
# One-time setup (already done):
#   git remote add findhelper https://github.com/BrunoO/FindHelper.git

set -euo pipefail

REMOTE="findhelper"
PUBLIC_BRANCH="findhelper-public"
SOURCE_BRANCH="main"

# ── 1. Sanity check ────────────────────────────────────────────────────────────
current_branch=$(git symbolic-ref --short HEAD)
if [[ "$current_branch" != "$SOURCE_BRANCH" ]]; then
  echo "ERROR: Must be on '$SOURCE_BRANCH' to publish (currently on '$current_branch')." >&2
  exit 1
fi

if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "ERROR: Working tree or index is dirty. Commit or stash changes first." >&2
  exit 1
fi

SOURCE_SHA=$(git rev-parse --short HEAD)
SOURCE_MSG=$(git log -1 --pretty=format:"%s")

echo "Publishing main ($SOURCE_SHA: $SOURCE_MSG) → $REMOTE/main ..."

# ── 2. Switch to orphan publish branch ────────────────────────────────────────
git checkout "$PUBLIC_BRANCH"

# ── 3. Overlay the full tree from main ────────────────────────────────────────
# Remove everything currently in the index so we get a clean overlay.
git rm -rf --cached . > /dev/null

# Restore the full tree from main (working tree + index).
git checkout "$SOURCE_BRANCH" -- .

# ── 4. Remove internal-only content ───────────────────────────────────────────
# internal-docs/ is never tracked on this branch; remove from working tree too.
if [[ -d internal-docs ]]; then
  git rm -rf --cached internal-docs/ > /dev/null 2>&1 || true
  rm -rf internal-docs/
fi

# .claude/ is in .gitignore so it won't be staged, but clean it up anyway.
rm -rf .claude/

# Large internal-only test data files.
git rm -rf --cached tests/data/std-linux-filesystem.txt > /dev/null 2>&1 || true
rm -f tests/data/std-linux-filesystem.txt

# ── 5. Commit and push ─────────────────────────────────────────────────────────
git add -A

if git diff --cached --quiet; then
  echo "Nothing changed since last publish. Skipping commit."
else
  git commit --no-verify -m "$(cat <<EOF
Publish snapshot from main

Source: $SOURCE_SHA $SOURCE_MSG

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
fi

git push "$REMOTE" "$PUBLIC_BRANCH:main" --force

# ── 6. Return to main ──────────────────────────────────────────────────────────
git checkout "$SOURCE_BRANCH"

echo "Done. Published to https://github.com/BrunoO/FindHelper"
