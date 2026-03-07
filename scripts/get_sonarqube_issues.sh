#!/bin/bash

# Script to fetch all issues from SonarCloud using curl
# Usage: ./get_sonarqube_issues.sh [OPTIONS]
#
# Options:
#   --org ORGANIZATION     SonarCloud organization (default: BrunoO)
#   --project PROJECT_KEY  Project key (default: BrunoO_USN_WINDOWS)
#   --token TOKEN          SonarCloud token (or set SONARQUBE_TOKEN env var)
#   --output FILE          Output file (default: sonarqube_issues.json)
#   --format FORMAT        Output format: json, csv, summary (default: json)
#   --severities SEVS     Comma-separated severities (default: all)
#   --types TYPES          Comma-separated types (default: all)
#   --open-only            Fetch only open/unresolved issues (default: all issues)
#   --help                 Show this help message

set -euo pipefail

# Default values
ORGANIZATION="${SONARQUBE_ORG:-BrunoO}"
PROJECT_KEY="${SONARQUBE_PROJECT:-BrunoO_USN_WINDOWS}"
TOKEN="${SONARQUBE_TOKEN:-}"
OUTPUT_FILE="sonarqube_issues.json"
FORMAT="json"
SEVERITIES=""
TYPES=""
OPEN_ONLY=false
BASE_URL="https://sonarcloud.io/api"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Help function
show_help() {
    cat << EOF
Usage: $0 [OPTIONS]

Fetch all issues from SonarCloud for a project.

Options:
    --org ORGANIZATION     SonarCloud organization (default: BrunoO)
    --project PROJECT_KEY  Project key (default: BrunoO_USN_WINDOWS)
    --token TOKEN          SonarCloud token (or set SONARQUBE_TOKEN env var)
    --output FILE          Output file (default: sonarqube_issues.json)
    --format FORMAT        Output format: json, csv, summary (default: json)
    --severities SEVS      Comma-separated severities: BLOCKER,HIGH,MEDIUM,LOW,INFO
    --types TYPES          Comma-separated types: BUG,VULNERABILITY,CODE_SMELL,SECURITY_HOTSPOT
    --open-only            Fetch only open/unresolved issues (default: all issues)
    --help                 Show this help message

Environment Variables:
    SONARQUBE_TOKEN        SonarCloud authentication token
    SONARQUBE_ORG          SonarCloud organization
    SONARQUBE_PROJECT      Project key

Examples:
    # Get all issues in JSON format
    $0

    # Get only bugs and vulnerabilities
    $0 --types BUG,VULNERABILITY --format summary

    # Get high and blocker severity issues
    $0 --severities BLOCKER,HIGH --format csv --output critical_issues.csv

    # Use environment variables
    export SONARQUBE_TOKEN="your_token_here"
    $0 --org MyOrg --project MyProject
EOF
    exit 0
    return 0
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --org)
            ORGANIZATION="$2"
            shift 2
            ;;
        --project)
            PROJECT_KEY="$2"
            shift 2
            ;;
        --token)
            TOKEN="$2"
            shift 2
            ;;
        --output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        --format)
            FORMAT="$2"
            shift 2
            ;;
        --severities)
            SEVERITIES="$2"
            shift 2
            ;;
        --types)
            TYPES="$2"
            shift 2
            ;;
        --open-only)
            OPEN_ONLY=true
            shift
            ;;
        --help|-h)
            show_help
            ;;
        *)
            echo -e "${RED}ERROR: Unknown option: $1${NC}" >&2
            echo "Use --help for usage information" >&2
            exit 1
            ;;
    esac
done

# Validate token
if [[ -z "$TOKEN" ]]; then
    echo -e "${RED}ERROR: SonarCloud token is required${NC}" >&2
    echo "Set SONARQUBE_TOKEN environment variable or use --token option" >&2
    exit 1
fi

# Validate format
if [[ ! "$FORMAT" =~ ^(json|csv|summary)$ ]]; then
    echo -e "${RED}ERROR: Invalid format: $FORMAT${NC}" >&2
    echo "Valid formats: json, csv, summary" >&2
    exit 1
fi

echo -e "${GREEN}Fetching issues from SonarCloud...${NC}"
echo "Organization: $ORGANIZATION"
echo "Project: $PROJECT_KEY"
echo "Output: $OUTPUT_FILE"
echo ""

# Build API URL
API_URL="${BASE_URL}/issues/search"

# Build query parameters
PARAMS=(
    "organization=${ORGANIZATION}"
    "projects=${PROJECT_KEY}"
    "ps=500"  # Page size (max 500)
    "p=1"     # Start at page 1
)

if [[ -n "$SEVERITIES" ]]; then
    PARAMS+=("severities=${SEVERITIES}")
fi

if [[ -n "$TYPES" ]]; then
    PARAMS+=("types=${TYPES}")
fi

if [[ "$OPEN_ONLY" == "true" ]]; then
    PARAMS+=("resolved=false")
fi

# Join parameters
QUERY_STRING=$(IFS='&'; echo "${PARAMS[*]}")

# Temporary file for all issues
TEMP_FILE=$(mktemp)
trap "rm -f $TEMP_FILE" EXIT

# Fetch all pages
PAGE=1
TOTAL_ISSUES=0
ALL_ISSUES="[]"

while true; do
    echo -e "${YELLOW}Fetching page $PAGE...${NC}"
    
    # Update page parameter
    QUERY_STRING=$(echo "$QUERY_STRING" | sed "s/p=[0-9]*/p=$PAGE/")
    
    # Make API request
    HTTP_CODE=$(curl -s -w "%{http_code}" -o "$TEMP_FILE" \
        -u "${TOKEN}:" \
        "${API_URL}?${QUERY_STRING}")
    
    # Check HTTP response code
    if [[ "$HTTP_CODE" != "200" ]]; then
        echo -e "${RED}ERROR: API request failed with HTTP code $HTTP_CODE${NC}" >&2
        if [[ -f "$TEMP_FILE" ]]; then
            echo "Response:" >&2
            cat "$TEMP_FILE" >&2
            echo "" >&2
        fi
        exit 1
    fi
    
    # Parse response
    if ! command -v jq &> /dev/null; then
        echo -e "${YELLOW}Warning: jq not found. Installing basic JSON parsing...${NC}"
        # Basic parsing without jq (less reliable)
        PAGE_ISSUES=$(grep -o '"issues":\[.*\]' "$TEMP_FILE" || echo '[]')
        TOTAL=$(grep -o '"total":[0-9]*' "$TEMP_FILE" | grep -o '[0-9]*' || echo "0")
        PAGES=$(grep -o '"p":[0-9]*' "$TEMP_FILE" | head -1 | grep -o '[0-9]*' || echo "1")
        PS=$(grep -o '"ps":[0-9]*' "$TEMP_FILE" | head -1 | grep -o '[0-9]*' || echo "500")
    else
        # Use jq for proper JSON parsing
        PAGE_ISSUES=$(jq -c '.issues' "$TEMP_FILE")
        TOTAL=$(jq -r '.total' "$TEMP_FILE")
        PAGES=$(jq -r '.paging.total' "$TEMP_FILE" 2>/dev/null || jq -r '.total' "$TEMP_FILE")
        PS=$(jq -r '.paging.pageSize' "$TEMP_FILE" 2>/dev/null || echo "500")
    fi
    
    # Calculate total pages
    if [[ -z "$PAGES" ]] || [[ "$PAGES" == "null" ]]; then
        # Calculate from total and page size
        if [[ -n "$TOTAL" ]] && [[ "$TOTAL" != "null" ]] && [[ "$TOTAL" -gt 0 ]]; then
            PAGES=$(( (TOTAL + PS - 1) / PS ))
        else
            PAGES=1
        fi
    fi
    
    # Check if we have issues
    if [[ "$PAGE_ISSUES" == "[]" ]] || [[ -z "$PAGE_ISSUES" ]]; then
        echo "No more issues found."
        break
    fi
    
    # Merge issues (requires jq for proper merging)
    if command -v jq &> /dev/null; then
        ALL_ISSUES=$(echo "$ALL_ISSUES" | jq -c ". + $PAGE_ISSUES")
        ISSUE_COUNT=$(echo "$PAGE_ISSUES" | jq 'length')
        TOTAL_ISSUES=$((TOTAL_ISSUES + ISSUE_COUNT))
    else
        # Without jq, just append (less reliable)
        echo "Page $PAGE issues:" >> "${OUTPUT_FILE}.raw"
        cat "$TEMP_FILE" >> "${OUTPUT_FILE}.raw"
        TOTAL_ISSUES=$((TOTAL_ISSUES + 1))  # Approximation
    fi
    
    echo "  Found $ISSUE_COUNT issues on page $PAGE (Total so far: $TOTAL_ISSUES)"
    
    # Check if we've fetched all pages
    if [[ "$PAGE" -ge "$PAGES" ]]; then
        break
    fi
    
    PAGE=$((PAGE + 1))
done

echo ""
echo -e "${GREEN}Total issues fetched: $TOTAL_ISSUES${NC}"

# Format output
if command -v jq &> /dev/null; then
    case "$FORMAT" in
        json)
            # Output as JSON array
            echo "$ALL_ISSUES" | jq '.' > "$OUTPUT_FILE"
            echo -e "${GREEN}Issues saved to: $OUTPUT_FILE${NC}"
            ;;
        csv)
            # Convert to CSV
            echo "key,severity,type,status,component,line,message,rule" > "$OUTPUT_FILE"
            echo "$ALL_ISSUES" | jq -r '.[] | [
                .key,
                .severity,
                .type,
                .status,
                .component,
                .line // "N/A",
                .message,
                .rule
            ] | @csv' >> "$OUTPUT_FILE"
            echo -e "${GREEN}CSV saved to: $OUTPUT_FILE${NC}"
            ;;
        summary)
            # Create summary
            {
                echo "SonarCloud Issues Summary"
                echo "========================="
                echo "Project: $PROJECT_KEY"
                echo "Organization: $ORGANIZATION"
                echo "Total Issues: $TOTAL_ISSUES"
                echo ""
                echo "By Severity:"
                echo "$ALL_ISSUES" | jq -r 'group_by(.severity) | .[] | "  \(.[0].severity): \(length)"'
                echo ""
                echo "By Type:"
                echo "$ALL_ISSUES" | jq -r 'group_by(.type) | .[] | "  \(.[0].type): \(length)"'
                echo ""
                echo "By Status:"
                echo "$ALL_ISSUES" | jq -r 'group_by(.status) | .[] | "  \(.[0].status): \(length)"'
                echo ""
                echo "Top 10 Issues:"
                echo "$ALL_ISSUES" | jq -r '.[:10] | .[] | "  [\(.severity)] \(.type) - \(.component):\(.line // "N/A") - \(.message)"'
            } > "$OUTPUT_FILE"
            echo -e "${GREEN}Summary saved to: $OUTPUT_FILE${NC}"
            ;;
        *)
            echo -e "${RED}ERROR: Invalid format: $FORMAT${NC}" >&2
            echo "Valid formats: json, csv, summary" >&2
            exit 1
            ;;
    esac
else
    echo -e "${YELLOW}Warning: jq not found. Raw JSON saved to: ${OUTPUT_FILE}.raw${NC}"
    echo "Install jq for formatted output: brew install jq (macOS) or apt-get install jq (Linux)"
    if [[ -f "${OUTPUT_FILE}.raw" ]]; then
        mv "${OUTPUT_FILE}.raw" "$OUTPUT_FILE"
    fi
fi

echo ""
echo -e "${GREEN}Done!${NC}"

