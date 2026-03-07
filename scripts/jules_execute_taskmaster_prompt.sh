#!/usr/bin/env bash
# Run from project root. Creates a jules remote session with the prompt
# extracted from a task document (markdown code block).
#
# Usage:
#   ./scripts/jules_execute_taskmaster_prompt.sh [TASK_DOC]
#
# TASK_DOC: path to the task .md file (relative to project root or absolute).
#           Use a prompt file from docs/prompts/, e.g.:
#           docs/prompts/2026-02-14_FULL_INDEX_TOTAL_SIZE_ROOT_CAUSE_TASK.md
#           Default: docs/prompts/2026-02-01_CLEAN_CLANG_TIDY_TEST_FILES_TASK.md
#
# The script extracts the prompt from a ```markdown code block if present (Taskmaster
# format); otherwise uses the whole file (direct prompt format).

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

DEFAULT_TASK_DOC="docs/prompts/2026-02-01_CLEAN_CLANG_TIDY_TEST_FILES_TASK.md"
TASK_DOC="${1:-${DEFAULT_TASK_DOC}}"

# Resolve path: if not absolute, treat as relative to project root
if [[ "${TASK_DOC}" != /* ]]; then
  TASK_DOC="${PROJECT_ROOT}/${TASK_DOC}"
fi

if [[ ! -f "${TASK_DOC}" ]]; then
  echo "Error: Task document not found: ${TASK_DOC}" >&2
  echo "Usage: $0 [TASK_DOC]" >&2
  echo "  TASK_DOC: path to task .md (default: ${DEFAULT_TASK_DOC})" >&2
  exit 1
fi

# Extract prompt: prefer content inside ```markdown code block (Taskmaster format);
# if none found, use the whole file (direct prompt format).
X=$(awk '/^```markdown$/{f=1;next} f{if(/^```$/){exit} print}' "${TASK_DOC}")
if [[ -z "${X}" ]]; then
  X=$(<"${TASK_DOC}")
fi
# Escape double quotes for safe passing to jules
X=$(printf '%s' "${X}" | sed 's/"/\\"/g')
# Pass prompt with newlines preserved
jules remote new --repo . --session "$X"
