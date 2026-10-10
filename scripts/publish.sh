#!/usr/bin/env bash
# publish.sh — Publish current main to the public GitHub repo (findhelper remote).
#
# Usage:
#   scripts/publish.sh              # push snapshot; CI creates/updates "latest" pre-release
#   scripts/publish.sh v1.2.0       # push snapshot + tag; CI creates versioned release v1.2.0
#
# Workflow:
#   1. Checks that main is clean and committed.
#   2. Recreates the orphan tracking branch (findhelper-public).
#   3. Overlays the full tree from main.
#   4. Removes internal-only content.
#   5. Commits a snapshot, tags it (versioned publishes), and pushes.
#      The tag is pushed BEFORE the branch: the tag-triggered CI run owns the
#      commit (versioned release + latest), and the branch-triggered run skips
#      its build when HEAD already carries a version tag (see build.yml).
#   6. Returns to main.
#
# One-time setup (already done):
#   git remote add findhelper https://github.com/BrunoO/FindHelper.git

set -euo pipefail

REMOTE="findhelper"
PUBLIC_BRANCH="findhelper-public"
SOURCE_BRANCH="main"
VERSION_TAG="${1:-}"

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

if [[ -n "$VERSION_TAG" && ! "$VERSION_TAG" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "ERROR: Version tag must match vX.Y.Z (got: $VERSION_TAG)." >&2
  exit 1
fi

SOURCE_SHA=$(git rev-parse --short HEAD)
SOURCE_MSG=$(git log -1 --pretty=format:"%s")

echo "Publishing main ($SOURCE_SHA: $SOURCE_MSG) → $REMOTE/main ..."
[[ -n "$VERSION_TAG" ]] && echo "  + versioned release: $VERSION_TAG"

# ── 2. Recreate orphan publish branch ────────────────────────────────────────
# Always recreate the orphan branch so removed large files from old snapshots
# are not kept in branch history and re-pushed forever.
if git show-ref --verify --quiet "refs/heads/$PUBLIC_BRANCH"; then
  git branch -D "$PUBLIC_BRANCH" > /dev/null
fi
git checkout --orphan "$PUBLIC_BRANCH"

# ── 3. Overlay the full tree from main ────────────────────────────────────────
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

# ── 5. Commit, tag, and push (tag first) ───────────────────────────────────────
git add -A

if git diff --cached --quiet; then
  echo "Nothing changed since last publish. Skipping commit."
else
  git commit --no-verify -m "$(cat <<EOF
Publish snapshot from main

Source: $SOURCE_SHA $SOURCE_MSG
EOF
)"
fi

if [[ -n "$VERSION_TAG" ]]; then
  # Tag the snapshot commit and push the tag FIRST: the tag-triggered CI run
  # owns this commit (builds once, publishes versioned release + latest).
  git tag "$VERSION_TAG"
  git push "$REMOTE" "$VERSION_TAG"
  echo "Pushed tag $VERSION_TAG → CI will create release $VERSION_TAG (+ latest)"
  echo "NOTE: the tag run (not the branch run below) does the build — the"
  echo "branch run skips its matrix by design. Watch the '$VERSION_TAG' run at:"
  echo "https://github.com/BrunoO/FindHelper/actions"
fi

# Branch push follows; its CI run skips the build when HEAD is version-tagged.
git push "$REMOTE" "$PUBLIC_BRANCH:main" --force
echo "Pushed snapshot → $REMOTE/main"

# ── 6. Return to main ──────────────────────────────────────────────────────────
git checkout "$SOURCE_BRANCH"

echo "Done. https://github.com/BrunoO/FindHelper"
