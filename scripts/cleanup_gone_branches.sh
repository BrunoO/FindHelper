#!/usr/bin/env zsh
# Clean up local git branches whose remote upstream branches have been deleted.
# Run from the repository root. Use --dry-run to preview without deleting.

set -e

SCRIPT_NAME="${0:t}"
CURRENT_BRANCH="$(git branch --show-current)"

show_help() {
  cat << EOF
Usage: $SCRIPT_NAME [OPTIONS]

Delete local branches whose remote upstream no longer exists (e.g. after PR merge
and branch deletion on remote). Runs \`git fetch --prune\` first to update refs.

Options:
  --dry-run    List branches that would be deleted (fetch --prune, but no delete)
  --force      Use \`git branch -D\` instead of \`-d\` (delete even if not merged)
  --help, -h   Show this help

Examples:
  $SCRIPT_NAME --dry-run   # See which branches would be removed
  $SCRIPT_NAME             # Prune remotes, then delete gone branches with -d
  $SCRIPT_NAME --force     # Same but use -D to delete unmerged branches
EOF
  exit 0
}

DRY_RUN=false
USE_FORCE=false
for arg in "$@"; do
  case "$arg" in
    --dry-run)   DRY_RUN=true ;;
    --force)     USE_FORCE=true ;;
    --help|-h)   show_help ;;
    *)
      echo "Unknown option: $arg" >&2
      show_help
      ;;
  esac
done

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "Not inside a git repository." >&2
  exit 1
fi

git fetch --prune

# Branches with upstream marked ": gone]" (remote branch was deleted)
gone_branches=()
while IFS= read -r branch; do
  [[ -z "$branch" ]] && continue
  [[ "$branch" == "$CURRENT_BRANCH" ]] && continue
  gone_branches+=("$branch")
done < <(git branch -vv | grep -E '\s+\[.+: gone\]' | sed 's/^[* ]*//' | awk '{print $1}')

if [[ ${#gone_branches[@]} -eq 0 ]]; then
  echo "No local branches with gone upstream."
  exit 0
fi

delete_flag="-d"
[[ "$USE_FORCE" == true ]] && delete_flag="-D"

if [[ "$DRY_RUN" == true ]]; then
  echo "Would delete ${#gone_branches[@]} branch(es) (--dry-run):"
  for b in "${gone_branches[@]}"; do
    echo "  - $b"
  done
  exit 0
fi

deleted=0
for b in "${gone_branches[@]}"; do
  if git branch "$delete_flag" "$b" 2>/dev/null; then
    echo "Deleted branch: $b"
    (( deleted++ )) || true
  else
    echo "Could not delete '$b' (try --force?)." >&2
  fi
done

echo "Cleaned $deleted branch(es)."
exit 0
