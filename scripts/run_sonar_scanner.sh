#!/usr/bin/env zsh

# Script to run SonarScanner CLI for SonarCloud analysis
# Usage: ./run_sonar_scanner.sh [OPTIONS]
#
# Note: If analysis is done automatically (CI/CD, GitHub Actions, etc.),
#       you can use fetch_sonar_results.sh to get results locally instead.
#
# Options:
#   --org ORGANIZATION     SonarCloud organization (default: BrunoO)
#   --project PROJECT_KEY  Project key (default: BrunoO_USN_WINDOWS)
#   --token TOKEN          SonarCloud token (or set SONARQUBE_TOKEN env var)
#   --scanner-path PATH    Path to sonar-scanner binary (if not in PATH)
#   --fetch-results        Automatically fetch results after analysis; with json format, also prints open issues to the terminal
#   --fetch-format FORMAT  Format for fetched results: json, csv, summary (default: json)
#   --output-dir DIR       Directory to save results (default: ./sonar-results)
#   --help                 Show this help message

set -euo pipefail

# Default values
ORGANIZATION="${SONARQUBE_ORG:-BrunoO}"
PROJECT_KEY="${SONARQUBE_PROJECT:-BrunoO_USN_WINDOWS}"
TOKEN="${SONARQUBE_TOKEN:-}"
SONAR_HOST_URL="${SONAR_HOST_URL:-https://sonarcloud.io}"
SONAR_SCANNER_PATH="${SONAR_SCANNER_PATH:-}"
FETCH_RESULTS=false
FETCH_FORMAT="json"
OUTPUT_DIR="./sonar-results"

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

Run SonarScanner CLI to analyze the project with SonarCloud.

Options:
    --org ORGANIZATION     SonarCloud organization (default: BrunoO)
    --project PROJECT_KEY  Project key (default: BrunoO_USN_WINDOWS)
    --token TOKEN          SonarCloud token (or set SONARQUBE_TOKEN env var)
    --scanner-path PATH    Path to sonar-scanner binary (if not in PATH)
    --fetch-results        Automatically fetch results after analysis; with json, prints open issues to terminal
    --fetch-format FORMAT  Format for fetched results: json, csv, summary (default: json)
    --output-dir DIR       Directory to save results (default: ./sonar-results)
    --help                 Show this help message

Environment Variables:
    SONARQUBE_TOKEN        SonarCloud authentication token (required)
    SONARQUBE_ORG          SonarCloud organization (default: BrunoO)
    SONARQUBE_PROJECT      Project key (default: BrunoO_USN_WINDOWS)
    SONAR_HOST_URL         SonarCloud host URL (default: https://sonarcloud.io)
    SONAR_SCANNER_PATH     Path to sonar-scanner binary (if not in PATH)

Examples:
    # Use environment variables
    export SONARQUBE_TOKEN="your_token_here"
    $0

    # Override organization and project
    $0 --org MyOrg --project MyProject

    # Specify scanner path if not in PATH
    $0 --scanner-path ~/tools/sonar-scanner/bin/sonar-scanner

    # Use token from command line
    $0 --token your_token_here

    # Run analysis and automatically fetch results
    $0 --fetch-results

    # Fetch results in CSV format
    $0 --fetch-results --fetch-format csv

    # Save results to custom directory
    $0 --fetch-results --output-dir ./reports

Configuration:
    Analysis scope (sources, exclusions, encoding, cfamily cache) is in
    sonar-project.properties so local and SonarCloud match. See:
    internal-docs/guides/2026-02-03_CONFIGURING_LOCAL_SONAR_SCANNER_TO_MATCH_SONARCLOUD.md

Note on C++ Analysis:
    SonarQube requires a compilation database for C++ analysis. The script
    automatically detects and uses one of the following (in order of preference):
    
    1. compile_commands.json (recommended for CMake projects)
       - Generate with: cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -B build
       - Script checks: ./compile_commands.json or ./build/compile_commands.json
    
    2. build-wrapper-dump.json (alternative method)
       - Download build-wrapper from SonarQube
       - Wrap your build: build-wrapper-macosx-x86-64 --out-dir build cmake --build build
       - This generates build/build-wrapper-dump.json
    
    If neither is found, C++ analysis will be disabled to prevent errors.
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
        --scanner-path)
            SONAR_SCANNER_PATH="$2"
            shift 2
            ;;
        --fetch-results)
            FETCH_RESULTS=true
            shift
            ;;
        --fetch-format)
            FETCH_FORMAT="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
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

# Find sonar-scanner binary
if [[ -n "$SONAR_SCANNER_PATH" ]]; then
    if [[ ! -f "$SONAR_SCANNER_PATH" ]]; then
        echo -e "${RED}ERROR: Scanner not found at: $SONAR_SCANNER_PATH${NC}" >&2
        exit 1
    fi
    SONAR_SCANNER="$SONAR_SCANNER_PATH"
elif command -v sonar-scanner &> /dev/null; then
    SONAR_SCANNER="sonar-scanner"
elif command -v sonar-scanner.bat &> /dev/null; then
    SONAR_SCANNER="sonar-scanner.bat"
else
    echo -e "${RED}ERROR: sonar-scanner not found in PATH${NC}" >&2
    echo "Please either:" >&2
    echo "  1. Add sonar-scanner to your PATH" >&2
    echo "  2. Use --scanner-path option to specify the path" >&2
    echo "  3. Set SONAR_SCANNER_PATH environment variable" >&2
    exit 1
fi

# Validate token
if [[ -z "$TOKEN" ]]; then
    echo -e "${RED}ERROR: SonarCloud token is required${NC}" >&2
    echo "Set SONARQUBE_TOKEN environment variable or use --token option" >&2
    echo "" >&2
    echo "To get a token:" >&2
    echo "  1. Go to https://sonarcloud.io" >&2
    echo "  2. Sign in and go to My Account → Security" >&2
    echo "  3. Generate a new token" >&2
    exit 1
fi

# Get project root directory (where this script is located)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Change to project root
cd "$PROJECT_ROOT"

# Display configuration
echo -e "${BLUE}SonarScanner Configuration${NC}"
echo "================================"
echo "Organization: $ORGANIZATION"
echo "Project Key:  $PROJECT_KEY"
echo "Host URL:     $SONAR_HOST_URL"
echo "Scanner:     $SONAR_SCANNER"
echo "Project Root: $PROJECT_ROOT"
echo ""

# Check if sonar-project.properties exists
if [[ -f "sonar-project.properties" ]]; then
    echo -e "${YELLOW}Note: sonar-project.properties found. Some settings may be overridden.${NC}"
    echo ""
fi

# Verify scanner version
echo -e "${GREEN}Verifying SonarScanner installation...${NC}"
if ! "$SONAR_SCANNER" -v &> /dev/null; then
    echo -e "${YELLOW}Warning: Could not verify scanner version${NC}"
else
    "$SONAR_SCANNER" -v
    echo ""
fi

# Set environment variables for sonar-scanner
export SONAR_HOST_URL
# Use sonar.token instead of deprecated sonar.login
export SONAR_TOKEN="$TOKEN"

# Check for C++ analysis configuration files
BUILD_WRAPPER_OUTPUT=""
BUILD_WRAPPER_DUMP="$PROJECT_ROOT/build/build-wrapper-dump.json"
COMPILE_COMMANDS=""

# Check for compile_commands.json (preferred for CMake projects)
if [[ -f "$PROJECT_ROOT/compile_commands.json" ]]; then
    COMPILE_COMMANDS="$PROJECT_ROOT/compile_commands.json"
    echo -e "${GREEN}✓ Found compile_commands.json in project root${NC}"
elif [[ -f "$PROJECT_ROOT/build/compile_commands.json" ]]; then
    COMPILE_COMMANDS="$PROJECT_ROOT/build/compile_commands.json"
    echo -e "${GREEN}✓ Found compile_commands.json in build/ directory${NC}"
fi

# Check for build-wrapper output (alternative to compile_commands.json)
if [[ -f "$BUILD_WRAPPER_DUMP" ]]; then
    BUILD_WRAPPER_OUTPUT="build"
    echo -e "${GREEN}✓ Found build-wrapper-dump.json - will use build wrapper data${NC}"
fi

# Warn if no C++ configuration found and disable C++ analysis
if [[ -z "$COMPILE_COMMANDS" ]] && [[ -z "$BUILD_WRAPPER_OUTPUT" ]]; then
    echo -e "${YELLOW}⚠ No C++ analysis configuration found${NC}"
    echo -e "${YELLOW}  SonarQube requires either:${NC}"
    echo -e "${YELLOW}    - compile_commands.json (generate with: cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)${NC}"
    echo -e "${YELLOW}    - build-wrapper-dump.json (run build-wrapper during compilation)${NC}"
    echo -e "${YELLOW}  C++ analysis will be disabled to prevent errors${NC}"
    echo ""
fi

# Build sonar-scanner command: connection and C++ input only.
# Analysis scope (sources, exclusions, encoding, cfamily cache) come from
# sonar-project.properties so local and SonarCloud use the same config.
# See: internal-docs/guides/2026-02-03_CONFIGURING_LOCAL_SONAR_SCANNER_TO_MATCH_SONARCLOUD.md
SCANNER_ARGS=(
    "-Dsonar.projectKey=$PROJECT_KEY"
    "-Dsonar.organization=$ORGANIZATION"
    "-Dsonar.host.url=$SONAR_HOST_URL"
    "-Dsonar.token=$TOKEN"
)

# Set compile-commands if found (preferred method)
if [[ -n "$COMPILE_COMMANDS" ]]; then
    SCANNER_ARGS+=("-Dsonar.cfamily.compile-commands=$COMPILE_COMMANDS")
fi

# Set build-wrapper-output if found (alternative method)
if [[ -n "$BUILD_WRAPPER_OUTPUT" ]]; then
    SCANNER_ARGS+=("-Dsonar.cfamily.build-wrapper-output=$BUILD_WRAPPER_OUTPUT")
fi

# Disable C++ analysis if no configuration found (prevents errors)
if [[ -z "$COMPILE_COMMANDS" ]] && [[ -z "$BUILD_WRAPPER_OUTPUT" ]]; then
    SCANNER_ARGS+=(
        "-Dsonar.c.file.suffixes=-"
        "-Dsonar.cpp.file.suffixes=-"
        "-Dsonar.objc.file.suffixes=-"
    )
    echo -e "${YELLOW}  C++ files will be excluded from analysis${NC}"
    echo ""
fi

# Run sonar-scanner (sonar-project.properties supplies sources, exclusions, encoding, cfamily cache)
echo -e "${GREEN}Running SonarScanner analysis...${NC}"
echo ""

# Execute sonar-scanner with all arguments
if "$SONAR_SCANNER" "${SCANNER_ARGS[@]}"; then
    echo ""
    echo -e "${GREEN}✅ SonarScanner analysis completed successfully!${NC}"
    echo ""
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${GREEN}📊 View Analysis Results${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo ""
    echo -e "${BLUE}Project Overview:${NC}"
    echo "  https://sonarcloud.io/project/overview?id=$PROJECT_KEY"
    echo ""
    echo -e "${BLUE}View Issues:${NC}"
    echo "  All Issues:     https://sonarcloud.io/project/issues?id=$PROJECT_KEY"
    echo "  Bugs:           https://sonarcloud.io/project/issues?id=$PROJECT_KEY&types=BUG"
    echo "  Code Smells:    https://sonarcloud.io/project/issues?id=$PROJECT_KEY&types=CODE_SMELL"
    echo "  Vulnerabilities: https://sonarcloud.io/project/issues?id=$PROJECT_KEY&types=VULNERABILITY"
    echo "  Security Hotspots: https://sonarcloud.io/project/issues?id=$PROJECT_KEY&types=SECURITY_HOTSPOT"
    echo ""
    echo -e "${BLUE}Other Views:${NC}"
    echo "  Quality Gate:   https://sonarcloud.io/project/quality_gate?id=$PROJECT_KEY"
    echo "  Duplications:   https://sonarcloud.io/project/duplications?id=$PROJECT_KEY"
    echo "  Code Coverage:  https://sonarcloud.io/project/coverage?id=$PROJECT_KEY"
    echo "  Measures:      https://sonarcloud.io/project/measures?id=$PROJECT_KEY"
    echo ""
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo ""
    
    # Fetch results locally if requested
    if [[ "$FETCH_RESULTS" == true ]]; then
        echo -e "${GREEN}📥 Fetching analysis results locally...${NC}"
        echo ""
        
        # Create output directory
        mkdir -p "$OUTPUT_DIR"
        
        # Check if get_sonarqube_issues.sh exists and use it
        ISSUES_SCRIPT="$SCRIPT_DIR/get_sonarqube_issues.sh"
        if [[ -f "$ISSUES_SCRIPT" ]] && [[ -x "$ISSUES_SCRIPT" ]]; then
            echo -e "${BLUE}Using existing get_sonarqube_issues.sh script...${NC}"
            
            # Determine output file based on format
            case "$FETCH_FORMAT" in
                json)
                    OUTPUT_FILE="$OUTPUT_DIR/sonarqube_issues.json"
                    ;;
                csv)
                    OUTPUT_FILE="$OUTPUT_DIR/sonarqube_issues.csv"
                    ;;
                summary)
                    OUTPUT_FILE="$OUTPUT_DIR/sonarqube_issues_summary.txt"
                    ;;
                *)
                    OUTPUT_FILE="$OUTPUT_DIR/sonarqube_issues.$FETCH_FORMAT"
                    ;;
            esac
            
            # Run the issues script
            if "$ISSUES_SCRIPT" \
                --org "$ORGANIZATION" \
                --project "$PROJECT_KEY" \
                --token "$TOKEN" \
                --format "$FETCH_FORMAT" \
                --output "$OUTPUT_FILE"; then
                echo ""
                echo -e "${GREEN}✅ Results saved to: $OUTPUT_FILE${NC}"
                # Display OPEN issues on stdout when we have JSON (so user doesn't have to open the file)
                if [[ "$FETCH_FORMAT" == "json" ]] && [[ -f "$OUTPUT_FILE" ]] && command -v jq &> /dev/null; then
                    OPEN_ISSUES=$(jq -r '(if type == "array" then . else .issues end) | map(select(.status == "OPEN")) | length' "$OUTPUT_FILE" 2>/dev/null || echo "0")
                    if [[ -n "$OPEN_ISSUES" ]] && [[ "$OPEN_ISSUES" != "null" ]] && [[ "$OPEN_ISSUES" -gt 0 ]]; then
                        echo ""
                        echo -e "${YELLOW}━━━━ Open issues ($OPEN_ISSUES) ━━━━${NC}"
                        jq -r '(if type == "array" then . else .issues end) | .[] | select(.status == "OPEN") | "  \(.component | split(":")[1] // .component):\(.line // " ") \(.rule) \(.message)"' "$OUTPUT_FILE" 2>/dev/null || true
                        echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
                    else
                        echo -e "${GREEN}  No open issues.${NC}"
                    fi
                fi
            else
                echo -e "${YELLOW}⚠ Failed to fetch issues. You can still view them in SonarCloud.${NC}"
            fi
        else
            # Fallback: Use curl to fetch issues directly
            echo -e "${BLUE}Fetching issues via API...${NC}"
            
            BASE_URL="https://sonarcloud.io/api"
            API_URL="${BASE_URL}/issues/search"
            OUTPUT_FILE="$OUTPUT_DIR/sonarqube_issues.json"
            
            # Fetch first page to get total count
            TEMP_FILE=$(mktemp)
            HTTP_CODE=$(curl -s -w "%{http_code}" -o "$TEMP_FILE" \
                -u "${TOKEN}:" \
                "${API_URL}?organization=${ORGANIZATION}&componentKeys=${PROJECT_KEY}&ps=500&p=1")
            
            if [[ "$HTTP_CODE" == "200" ]]; then
                if command -v jq &> /dev/null; then
                    TOTAL=$(jq -r '.total' "$TEMP_FILE" 2>/dev/null || echo "0")
                    echo "Found $TOTAL issues"
                    
                    # Save JSON
                    if [[ "$FETCH_FORMAT" == "json" ]]; then
                        mv "$TEMP_FILE" "$OUTPUT_FILE"
                        echo -e "${GREEN}✅ Issues saved to: $OUTPUT_FILE${NC}"
                        # Display OPEN issues on stdout
                        OPEN_ISSUES=$(jq -r '.issues | map(select(.status == "OPEN")) | length' "$OUTPUT_FILE" 2>/dev/null || echo "0")
                        if [[ -n "$OPEN_ISSUES" ]] && [[ "$OPEN_ISSUES" != "null" ]] && [[ "$OPEN_ISSUES" -gt 0 ]]; then
                            echo ""
                            echo -e "${YELLOW}━━━━ Open issues ($OPEN_ISSUES) ━━━━${NC}"
                            jq -r '.issues[] | select(.status == "OPEN") | "  \(.component | split(":")[1] // .component):\(.line // " ") \(.rule) \(.message)"' "$OUTPUT_FILE" 2>/dev/null || true
                            echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
                        else
                            echo -e "${GREEN}  No open issues.${NC}"
                        fi
                    else
                        # Convert format
                        case "$FETCH_FORMAT" in
                            csv)
                                OUTPUT_FILE="$OUTPUT_DIR/sonarqube_issues.csv"
                                echo "key,severity,type,status,component,line,message,rule" > "$OUTPUT_FILE"
                                jq -r '.issues[] | [
                                    .key,
                                    .severity,
                                    .type,
                                    .status,
                                    .component,
                                    (.line // "N/A" | tostring),
                                    .message,
                                    .rule
                                ] | @csv' "$TEMP_FILE" >> "$OUTPUT_FILE"
                                echo -e "${GREEN}✅ CSV saved to: $OUTPUT_FILE${NC}"
                                ;;
                            summary)
                                OUTPUT_FILE="$OUTPUT_DIR/sonarqube_issues_summary.txt"
                                {
                                    echo "SonarCloud Issues Summary"
                                    echo "========================="
                                    echo "Project: $PROJECT_KEY"
                                    echo "Organization: $ORGANIZATION"
                                    echo "Total Issues: $TOTAL"
                                    echo ""
                                    echo "By Severity:"
                                    jq -r '.issues | group_by(.severity) | .[] | "  \(.[0].severity): \(length)"' "$TEMP_FILE"
                                    echo ""
                                    echo "By Type:"
                                    jq -r '.issues | group_by(.type) | .[] | "  \(.[0].type): \(length)"' "$TEMP_FILE"
                                    echo ""
                                    echo "Top 10 Issues:"
                                    jq -r '.issues[:10][] | "  [\(.severity)] \(.type) - \(.component):\(.line // "N/A") - \(.message)"' "$TEMP_FILE"
                                } > "$OUTPUT_FILE"
                                echo -e "${GREEN}✅ Summary saved to: $OUTPUT_FILE${NC}"
                                ;;
                        esac
                        rm -f "$TEMP_FILE"
                    fi
                else
                    mv "$TEMP_FILE" "$OUTPUT_FILE"
                    echo -e "${GREEN}✅ Issues saved to: $OUTPUT_FILE${NC}"
                    echo -e "${YELLOW}Note: Install jq for format conversion: brew install jq${NC}"
                fi
            else
                echo -e "${YELLOW}⚠ Failed to fetch issues (HTTP $HTTP_CODE). You can still view them in SonarCloud.${NC}"
                rm -f "$TEMP_FILE"
            fi
        fi
        
        echo ""
        echo -e "${BLUE}Local Results Directory:${NC} $OUTPUT_DIR"
        echo ""
    fi
    
    exit 0
else
    echo ""
    echo -e "${RED}❌ SonarScanner analysis failed${NC}" >&2
    echo ""
    echo -e "${YELLOW}Check the error messages above for details.${NC}"
    echo -e "${YELLOW}Common issues:${NC}"
    echo "  - Missing compile_commands.json or build-wrapper-dump.json"
    echo "  - Invalid SonarCloud token"
    echo "  - Network connectivity issues"
    echo ""
    exit 1
fi

