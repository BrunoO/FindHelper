#!/bin/bash

# Script to fetch project measures (metrics) from SonarCloud, including duplication
# Usage: ./get_sonarqube_measures.sh [OPTIONS]
#
# Options:
#   --org ORGANIZATION     SonarCloud organization (default: BrunoO)
#   --project PROJECT_KEY  Project key (default: BrunoO_USN_WINDOWS)
#   --token TOKEN          SonarCloud token (or set SONARQUBE_TOKEN env var)
#   --output FILE          Output file (default: sonarqube_measures.json)
#   --format FORMAT        Output format: json, summary (default: json)
#   --help                 Show this help message
#
# Metrics fetched: duplication (density, lines, blocks), ncloc, quality gate, etc.

set -euo pipefail

# Default values
ORGANIZATION="${SONARQUBE_ORG:-BrunoO}"
PROJECT_KEY="${SONARQUBE_PROJECT:-BrunoO_USN_WINDOWS}"
TOKEN="${SONARQUBE_TOKEN:-}"
OUTPUT_FILE="sonarqube_measures.json"
FORMAT="json"
BASE_URL="https://sonarcloud.io/api"

# Metrics: duplication + key project health
METRIC_KEYS="duplicated_lines_density,duplicated_lines,duplicated_blocks,ncloc,new_duplicated_lines_density,new_duplicated_lines,new_lines_to_cover,coverage,new_coverage,bugs,vulnerabilities,code_smells,sqale_rating,reliability_rating,security_rating"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

show_help() {
    cat << EOF
Usage: $0 [OPTIONS]

Fetch project measures (metrics) from SonarCloud, including duplication rate.

Options:
    --org ORGANIZATION     SonarCloud organization (default: BrunoO)
    --project PROJECT_KEY  Project key (default: BrunoO_USN_WINDOWS)
    --token TOKEN          SonarCloud token (or set SONARQUBE_TOKEN env var)
    --output FILE          Output file (default: sonarqube_measures.json)
    --format FORMAT        Output format: json, summary (default: json)
    --help                 Show this help message

Environment Variables:
    SONARQUBE_TOKEN        SonarCloud authentication token (required)
    SONARQUBE_ORG          SonarCloud organization
    SONARQUBE_PROJECT      Project key

Examples:
    export SONARQUBE_TOKEN="your_token_here"
    $0
    $0 --format summary
    $0 --output sonar-results/measures.json --format summary
EOF
    exit 0
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --org) ORGANIZATION="$2"; shift 2 ;;
        --project) PROJECT_KEY="$2"; shift 2 ;;
        --token) TOKEN="$2"; shift 2 ;;
        --output) OUTPUT_FILE="$2"; shift 2 ;;
        --format) FORMAT="$2"; shift 2 ;;
        --help|-h) show_help ;;
        *)
            echo -e "${RED}ERROR: Unknown option: $1${NC}" >&2
            echo "Use --help for usage" >&2
            exit 1
            ;;
    esac
done

if [[ -z "$TOKEN" ]]; then
    echo -e "${RED}ERROR: SonarCloud token is required${NC}" >&2
    echo "Set SONARQUBE_TOKEN or use --token" >&2
    exit 1
fi

if [[ ! "$FORMAT" =~ ^(json|summary)$ ]]; then
    echo -e "${RED}ERROR: Invalid format: $FORMAT${NC}" >&2
    echo "Valid formats: json, summary" >&2
    exit 1
fi

echo -e "${BLUE}Fetching SonarCloud measures...${NC}"
echo "Organization: $ORGANIZATION"
echo "Project: $PROJECT_KEY"
echo ""

# SonarCloud: component is the project key
API_URL="${BASE_URL}/measures/component"
PARAMS="component=${PROJECT_KEY}&metricKeys=${METRIC_KEYS}"
# Optional: branch for new code metrics
# PARAMS="${PARAMS}&branch=main"

TEMP_FILE=$(mktemp)
trap "rm -f $TEMP_FILE" EXIT

HTTP_CODE=$(curl -s -w "%{http_code}" -o "$TEMP_FILE" \
    -u "${TOKEN}:" \
    "${API_URL}?${PARAMS}")

if [[ "$HTTP_CODE" != "200" ]]; then
    echo -e "${RED}ERROR: API request failed (HTTP $HTTP_CODE)${NC}" >&2
    if [[ -s "$TEMP_FILE" ]]; then
        echo "Response:" >&2
        head -c 500 "$TEMP_FILE" >&2
        echo "" >&2
    fi
    exit 1
fi

if ! command -v jq &> /dev/null; then
    echo -e "${YELLOW}Warning: jq not found. Saving raw JSON. Install jq for summary format.${NC}"
    cp "$TEMP_FILE" "$OUTPUT_FILE"
    echo -e "${GREEN}Measures saved to: $OUTPUT_FILE${NC}"
    exit 0
fi

case "$FORMAT" in
    json)
        jq '.' "$TEMP_FILE" > "$OUTPUT_FILE"
        echo -e "${GREEN}Measures saved to: $OUTPUT_FILE${NC}"
        ;;
    summary)
        SUMMARY_FILE="${OUTPUT_FILE%.json}.summary.txt"
        if [[ "$SUMMARY_FILE" == "$OUTPUT_FILE" ]]; then
            SUMMARY_FILE="${OUTPUT_FILE%.*}.summary.txt"
        fi
        {
            echo "SonarCloud Measures Summary"
            echo "==========================="
            echo "Project: $PROJECT_KEY"
            echo "Organization: $ORGANIZATION"
            echo ""
            echo "Duplication:"
            jq -r '
                .component.measures[] | select(.metric == "duplicated_lines_density") | "  Duplicated lines density: \(.value)%"
            ' "$TEMP_FILE" 2>/dev/null || true
            jq -r '
                .component.measures[] | select(.metric == "duplicated_lines") | "  Duplicated lines: \(.value)"
            ' "$TEMP_FILE" 2>/dev/null || true
            jq -r '
                .component.measures[] | select(.metric == "duplicated_blocks") | "  Duplicated blocks: \(.value)"
            ' "$TEMP_FILE" 2>/dev/null || true
            jq -r '
                .component.measures[] | select(.metric == "new_duplicated_lines_density") | "  New duplicated lines density: \(.value)%"
            ' "$TEMP_FILE" 2>/dev/null || true
            echo ""
            echo "Size:"
            jq -r '
                .component.measures[] | select(.metric == "ncloc") | "  Lines of code (ncloc): \(.value)"
            ' "$TEMP_FILE" 2>/dev/null || true
            echo ""
            echo "Issues:"
            jq -r '
                .component.measures[] | select(.metric == "bugs") | "  Bugs: \(.value)"
            ' "$TEMP_FILE" 2>/dev/null || true
            jq -r '
                .component.measures[] | select(.metric == "vulnerabilities") | "  Vulnerabilities: \(.value)"
            ' "$TEMP_FILE" 2>/dev/null || true
            jq -r '
                .component.measures[] | select(.metric == "code_smells") | "  Code smells: \(.value)"
            ' "$TEMP_FILE" 2>/dev/null || true
            echo ""
            echo "Ratings (1=A, 2=B, 3=C, 4=D, 5=E):"
            jq -r '
                .component.measures[] | select(.metric == "sqale_rating") | "  Maintainability: \(.value)"
            ' "$TEMP_FILE" 2>/dev/null || true
            jq -r '
                .component.measures[] | select(.metric == "reliability_rating") | "  Reliability: \(.value)"
            ' "$TEMP_FILE" 2>/dev/null || true
            jq -r '
                .component.measures[] | select(.metric == "security_rating") | "  Security: \(.value)"
            ' "$TEMP_FILE" 2>/dev/null || true
        } > "$SUMMARY_FILE"
        jq '.' "$TEMP_FILE" > "$OUTPUT_FILE"
        cat "$SUMMARY_FILE"
        echo ""
        echo -e "${GREEN}Summary: $SUMMARY_FILE  |  Full JSON: $OUTPUT_FILE${NC}"
        ;;
    *)
        echo -e "${RED}Invalid format${NC}" >&2
        exit 1
        ;;
esac

echo ""
echo -e "${BLUE}Duplications in SonarCloud:${NC} https://sonarcloud.io/project/duplications?id=${PROJECT_KEY}"
echo -e "${BLUE}Quality gate:${NC} https://sonarcloud.io/project/quality_gate?id=${PROJECT_KEY}"
