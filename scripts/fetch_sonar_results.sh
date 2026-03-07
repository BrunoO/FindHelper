#!/usr/bin/env zsh

# Script to fetch SonarCloud analysis results locally
# Usage: ./fetch_sonar_results.sh [OPTIONS]
#
# This script fetches results from SonarCloud via API, so it works regardless
# of how the analysis was triggered (local scanner, CI/CD, GitHub Actions, etc.)
#
# Options:
#   --org ORGANIZATION     SonarCloud organization (default: BrunoO)
#   --project PROJECT_KEY  Project key (default: BrunoO_USN_WINDOWS)
#   --token TOKEN          SonarCloud token (or set SONARQUBE_TOKEN env var)
#   --format FORMAT        Output format: json, csv, summary (default: json)
#   --output-dir DIR       Directory to save results (default: ./sonar-results)
#   --open-only            Fetch only open/unresolved issues (default: all issues)
#   --help                 Show this help message

set -euo pipefail

# Default values
ORGANIZATION="${SONARQUBE_ORG:-BrunoO}"
PROJECT_KEY="${SONARQUBE_PROJECT:-BrunoO_USN_WINDOWS}"
TOKEN="${SONARQUBE_TOKEN:-}"
FORMAT="json"
OUTPUT_DIR="./sonar-results"
OPEN_ONLY=false

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Help function
show_help() {
    cat << EOF
Usage: $0 [OPTIONS]

Fetch SonarCloud analysis results locally in various formats.

This script works regardless of how the analysis was triggered:
  - Local analysis (run_sonar_scanner.sh)
  - CI/CD pipelines (GitHub Actions, GitLab CI, etc.)
  - Automated server-side analysis
  - Any other SonarCloud analysis method

The script fetches results via SonarCloud API, so as long as the analysis
has completed and results are available in SonarCloud, this script can fetch them.

Options:
    --org ORGANIZATION     SonarCloud organization (default: BrunoO)
    --project PROJECT_KEY  Project key (default: BrunoO_USN_WINDOWS)
    --token TOKEN          SonarCloud token (or set SONARQUBE_TOKEN env var)
    --format FORMAT        Output format: json, csv, summary (default: json)
    --output-dir DIR       Directory to save results (default: ./sonar-results)
    --open-only            Fetch only open/unresolved issues (default: all issues)
    --help                 Show this help message

Environment Variables:
    SONARQUBE_TOKEN        SonarCloud authentication token (required)
    SONARQUBE_ORG          SonarCloud organization (default: BrunoO)
    SONARQUBE_PROJECT      Project key (default: BrunoO_USN_WINDOWS)

Formats:
    json      - JSON format (default, full issue details)
    csv       - CSV format (spreadsheet-friendly)
    summary   - Text summary (human-readable overview)

Examples:
    # Fetch all issues in JSON format
    export SONARQUBE_TOKEN="your_token_here"
    $0

    # Fetch issues in CSV format
    $0 --format csv

    # Fetch summary only
    $0 --format summary

    # Save to custom directory
    $0 --output-dir ./reports

    # Fetch for different project
    $0 --org MyOrg --project MyProject

    # After CI/CD analysis completes, fetch results locally
    # (works even if analysis ran on GitHub Actions, GitLab CI, etc.)
    $0 --format csv
    
    # Fetch only open issues
    $0 --open-only --format csv
EOF
    exit 0
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
        --format)
            FORMAT="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
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

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Check if get_sonarqube_issues.sh exists and use it
ISSUES_SCRIPT="$SCRIPT_DIR/get_sonarqube_issues.sh"
if [[ -f "$ISSUES_SCRIPT" ]] && [[ -x "$ISSUES_SCRIPT" ]]; then
    echo -e "${BLUE}Fetching SonarCloud results...${NC}"
    echo "Organization: $ORGANIZATION"
    echo "Project: $PROJECT_KEY"
    echo "Format: $FORMAT"
    if [[ "$OPEN_ONLY" == "true" ]]; then
        echo "Filter: Open issues only"
    else
        echo "Filter: All issues (open and closed)"
    fi
    echo ""
    
    # Create output directory
    mkdir -p "$OUTPUT_DIR"
    
    # Determine output file based on format
    case "$FORMAT" in
        json)
            OUTPUT_FILE="$OUTPUT_DIR/sonarqube_issues.json"
            ;;
        csv)
            OUTPUT_FILE="$OUTPUT_DIR/sonarqube_issues.csv"
            ;;
        summary)
            OUTPUT_FILE="$OUTPUT_DIR/sonarqube_issues_summary.txt"
            ;;
    esac
    
    # Build arguments for issues script
    ISSUES_ARGS=(
        --org "$ORGANIZATION"
        --project "$PROJECT_KEY"
        --token "$TOKEN"
        --format "$FORMAT"
        --output "$OUTPUT_FILE"
    )
    
    # Add --open-only if specified
    if [[ "$OPEN_ONLY" == "true" ]]; then
        ISSUES_ARGS+=(--open-only)
    fi
    
    # Run the issues script
    if "$ISSUES_SCRIPT" "${ISSUES_ARGS[@]}"; then
        echo ""
        echo -e "${GREEN}✅ Results saved to: $OUTPUT_FILE${NC}"
        echo ""
        echo -e "${BLUE}View in SonarCloud:${NC}"
        echo "  https://sonarcloud.io/project/issues?id=$PROJECT_KEY"
        exit 0
    else
        echo -e "${RED}❌ Failed to fetch results${NC}" >&2
        exit 1
    fi
else
    echo -e "${RED}ERROR: get_sonarqube_issues.sh not found${NC}" >&2
    echo "Expected location: $ISSUES_SCRIPT" >&2
    exit 1
fi

