#!/usr/bin/env bash
set -euo pipefail

# Get the most recent failed GitHub Actions workflow run for this repository
# and print the logs of only the failed jobs.

run_id="$(
  gh run list \
    -L 20 \
    --json databaseId,conclusion \
    --jq '[.[] | select(.conclusion==\"failure\")][0].databaseId' 2>/dev/null || true
)"

if [[ -z "${run_id}" ]]; then
  echo "No failed workflow runs found for this repository."
  exit 1
fi

echo "Last failed run ID: ${run_id}"
echo "Fetching failed job logs for that run..."
echo

gh run view "${run_id}" --log-failed

