#!/usr/bin/env bash
#
# Fetch per-file code duplication details from SonarCloud Web API.
#
# Lists all file components (api/components/tree), then for each file calls
# api/duplications/show and writes a JSON array. Pair with
# scripts/get_sonarqube_measures.sh for project-level duplication metrics.
#
# Requires: curl, jq
# Auth: SONARQUBE_TOKEN or --token
#
# API: https://docs.sonarsource.com/sonarcloud/advanced-setup/web-api/
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

ORGANIZATION="${SONARQUBE_ORG:-BrunoO}"
PROJECT_KEY="${SONARQUBE_PROJECT:-BrunoO_USN_WINDOWS}"
TOKEN="${SONARQUBE_TOKEN:-}"
BASE_URL="https://sonarcloud.io/api"
OUTPUT_DIR="${PROJECT_ROOT}/sonar-results"
OUTPUT_JSON=""
FORMAT="json"
BRANCH_NAME=""
SLEEP_SECS="0.12"
MAX_FILES=""
PATH_FILTER=""
SUMMARY_NONEMPTY_ONLY="OFF"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

show_help() {
    cat << EOF
Usage: $0 [OPTIONS]

Fetch per-file duplication data from SonarCloud (api/components/tree + api/duplications/show).

Options:
    --org ORGANIZATION       SonarCloud organization (default: BrunoO, or SONARQUBE_ORG)
    --project PROJECT_KEY    Project key (default: BrunoO_USN_WINDOWS, or SONARQUBE_PROJECT)
    --token TOKEN            SonarCloud token (or set SONARQUBE_TOKEN)
    --output-dir DIR         Directory for output (default: PROJECT_ROOT/sonar-results)
    --output FILE            JSON output path (default: DIR/sonar_duplications.json)
    --format FORMAT          json | summary (default: json)
    --branch BRANCH          Optional branch name (e.g. main) for branch-based analyses
    --sleep SECONDS          Delay between duplications/show calls (default: 0.12)
    --max-files N            Stop after N files (debug / limit API calls)
    --path-contains SUBSTR   Only include files whose path contains SUBSTR (case-sensitive)
    --nonempty-only          With --format summary: print only files with duplicate blocks

Environment:
    SONARQUBE_TOKEN          Required
    SONARQUBE_ORG / SONARQUBE_PROJECT   Defaults for --org / --project

Examples:
    export SONARQUBE_TOKEN="..."
    $0 --format summary --nonempty-only
    $0 --branch main --path-contains src/search --format summary
    $0 --max-files 30 --output /tmp/sonar_duplications.json

See also: scripts/get_sonarqube_measures.sh
EOF
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --org) ORGANIZATION="$2"; shift 2 ;;
        --project) PROJECT_KEY="$2"; shift 2 ;;
        --token) TOKEN="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --output) OUTPUT_JSON="$2"; shift 2 ;;
        --format) FORMAT="$2"; shift 2 ;;
        --branch) BRANCH_NAME="$2"; shift 2 ;;
        --sleep) SLEEP_SECS="$2"; shift 2 ;;
        --max-files) MAX_FILES="$2"; shift 2 ;;
        --path-contains) PATH_FILTER="$2"; shift 2 ;;
        --nonempty-only) SUMMARY_NONEMPTY_ONLY="ON"; shift ;;
        --help|-h) show_help ;;
        *)
            echo -e "${RED}ERROR: Unknown option: $1${NC}" >&2
            exit 1
            ;;
    esac
done

if [[ -z "$TOKEN" ]]; then
    echo -e "${RED}ERROR: SONARQUBE_TOKEN is required (or use --token).${NC}" >&2
    exit 1
fi

if [[ ! "$FORMAT" =~ ^(json|summary)$ ]]; then
    echo -e "${RED}ERROR: Invalid --format (use json or summary).${NC}" >&2
    exit 1
fi

if ! command -v jq &> /dev/null; then
    echo -e "${RED}ERROR: jq is required.${NC}" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
if [[ -z "$OUTPUT_JSON" ]]; then
    OUTPUT_JSON="${OUTPUT_DIR}/sonar_duplications.json"
fi

KEYS_FILE=$(mktemp)
RESP_TMP=$(mktemp)
NDJSON_TMP=$(mktemp)
trap 'rm -f "$KEYS_FILE" "$RESP_TMP" "$NDJSON_TMP"' EXIT

echo -e "${BLUE}SonarCloud duplication details${NC}"
echo "Organization: $ORGANIZATION"
echo "Project: $PROJECT_KEY"
echo "Output: $OUTPUT_JSON"
if [[ -n "$BRANCH_NAME" ]]; then
    echo "Branch: $BRANCH_NAME"
fi
echo ""

# --- Paginated components/tree (FIL only) ---
page=1
page_size=500
while true; do
    CURL_ARGS=(
        -s -S
        -H "Authorization: Bearer ${TOKEN}"
        -G
        "${BASE_URL}/components/tree"
        --data-urlencode "component=${PROJECT_KEY}"
        --data-urlencode "qualifiers=FIL"
        --data-urlencode "ps=${page_size}"
        --data-urlencode "p=${page}"
    )
    if [[ -n "$BRANCH_NAME" ]]; then
        CURL_ARGS+=(--data-urlencode "branch=${BRANCH_NAME}")
    fi
    HTTP_CODE=$(curl -w "%{http_code}" -o "$RESP_TMP" "${CURL_ARGS[@]}")
    if [[ "$HTTP_CODE" != "200" ]]; then
        echo -e "${RED}ERROR: components/tree failed (HTTP $HTTP_CODE)${NC}" >&2
        head -c 800 "$RESP_TMP" >&2 || true
        echo "" >&2
        exit 1
    fi
    total=$(jq -r '.paging.total // 0' "$RESP_TMP")
    idx=$(jq -r '.paging.pageIndex // 1' "$RESP_TMP")
    ps=$(jq -r '.paging.pageSize // 500' "$RESP_TMP")

    if [[ -n "$PATH_FILTER" ]]; then
        jq -r --arg sub "$PATH_FILTER" \
            '.components[] | select(.path | contains($sub)) | [.key, .path] | @tsv' "$RESP_TMP" >> "$KEYS_FILE"
    else
        jq -r '.components[] | [.key, .path] | @tsv' "$RESP_TMP" >> "$KEYS_FILE"
    fi

    if (( idx * ps >= total )); then
        break
    fi
    page=$((page + 1))
done

if [[ -n "${MAX_FILES:-}" ]]; then
    head -n "$MAX_FILES" "$KEYS_FILE" > "${KEYS_FILE}.trim"
    mv "${KEYS_FILE}.trim" "$KEYS_FILE"
fi

file_count=$(wc -l < "$KEYS_FILE" | tr -d ' ')
echo -e "${GREEN}Files to query: ${file_count}${NC}"
if [[ "$file_count" -eq 0 ]]; then
    echo -e "${YELLOW}No files matched (check --path-contains or project key).${NC}" >&2
    echo '[]' > "$OUTPUT_JSON"
    exit 0
fi

while IFS=$'\t' read -r comp_key path; do
    [[ -z "$comp_key" ]] && continue

    CURL_DUP=(
        -s -S
        -H "Authorization: Bearer ${TOKEN}"
        -G
        "${BASE_URL}/duplications/show"
        --data-urlencode "key=${comp_key}"
    )
    if [[ -n "$BRANCH_NAME" ]]; then
        CURL_DUP+=(--data-urlencode "branch=${BRANCH_NAME}")
    fi
    HTTP_CODE=$(curl -w "%{http_code}" -o "$RESP_TMP" "${CURL_DUP[@]}")

    if [[ "$HTTP_CODE" != "200" ]]; then
        echo -e "${YELLOW}WARN: duplications/show HTTP $HTTP_CODE for ${path}${NC}" >&2
        jq -n -c --arg k "$comp_key" --arg p "$path" --argjson h "$HTTP_CODE" \
            '{componentKey: $k, path: $p, error: ("HTTP " + ($h | tostring))}' >> "$NDJSON_TMP"
    else
        jq -c --arg k "$comp_key" --arg p "$path" \
            '. + {componentKey: $k, path: $p}' "$RESP_TMP" >> "$NDJSON_TMP"
    fi

    sleep "$SLEEP_SECS"
done < "$KEYS_FILE"

jq -s '.' "$NDJSON_TMP" > "$OUTPUT_JSON"

echo ""
echo -e "${GREEN}Wrote: ${OUTPUT_JSON}${NC}"

if [[ "$FORMAT" == "summary" ]]; then
    echo ""
    echo "========== Duplication summary (per file) =========="
    FILTER_JSON='false'
    if [[ "$SUMMARY_NONEMPTY_ONLY" == "ON" ]]; then
        FILTER_JSON='true'
    fi
    jq -r --argjson only_nonempty "$FILTER_JSON" '
      .[] | select(.error | not) |
      ([.duplications[]? | .blocks[]?] | length) as $blocks |
      (.duplications | length) as $groups |
      select(($only_nonempty | not) or ($blocks > 0)) |
      "\(.path)\tduplicate_blocks=\($blocks)\tgroups=\($groups)"
    ' "$OUTPUT_JSON"
fi

echo ""
echo -e "${BLUE}Duplications UI:${NC} https://sonarcloud.io/project/duplications?id=${PROJECT_KEY}"
