#!/bin/bash
# Test script to find the right API parameters to get SonarCloud issues

set -euo pipefail

TOKEN="${SONARQUBE_TOKEN:-}"
ORG="${SONARQUBE_ORG:-BrunoO}"
PROJECT="${SONARQUBE_PROJECT:-BrunoO_USN_WINDOWS}"

if [[ -z "$TOKEN" ]]; then
    echo "ERROR: SONARQUBE_TOKEN is required. Set it or run: export SONARQUBE_TOKEN=your_token" >&2
    exit 1
fi

# Python code to parse and display issue counts (reused multiple times)
PYTHON_PARSE_SCRIPT="import sys, json; data=json.load(sys.stdin); print(f\"  Total: {data.get('total', 0)}, Issues: {len(data.get('issues', []))}\")"

echo "Testing different API parameter combinations..."
echo "=============================================="
echo ""

# Test 1: componentKeys with resolved=true
echo "Test 1: componentKeys with resolved=true"
curl -s -u "${TOKEN}:" "https://sonarcloud.io/api/issues/search?organization=${ORG}&componentKeys=${PROJECT}&resolved=true&ps=5" | python3 -c "$PYTHON_PARSE_SCRIPT" 2>/dev/null || echo "  Error" >&2
echo ""

# Test 2: componentKeys with resolved=false
echo "Test 2: componentKeys with resolved=false"
curl -s -u "${TOKEN}:" "https://sonarcloud.io/api/issues/search?organization=${ORG}&componentKeys=${PROJECT}&resolved=false&ps=5" | python3 -c "$PYTHON_PARSE_SCRIPT" 2>/dev/null || echo "  Error" >&2
echo ""

# Test 3: componentKeys without resolved parameter
echo "Test 3: componentKeys (default - unresolved only)"
curl -s -u "${TOKEN}:" "https://sonarcloud.io/api/issues/search?organization=${ORG}&componentKeys=${PROJECT}&ps=5" | python3 -c "import sys, json; data=json.load(sys.stdin); print(f\"  Total: {data.get('total', 0)}, Issues: {len(data.get('issues', []))}\"); [print(f\"    - {i.get('key', 'N/A')}: {i.get('severity', 'N/A')} {i.get('type', 'N/A')}\") for i in data.get('issues', [])[:3]]" 2>/dev/null || echo "  Error" >&2
echo ""

# Test 4: projects parameter
echo "Test 4: projects parameter"
curl -s -u "${TOKEN}:" "https://sonarcloud.io/api/issues/search?organization=${ORG}&projects=${PROJECT}&ps=5" | python3 -c "$PYTHON_PARSE_SCRIPT" 2>/dev/null || echo "  Error" >&2
echo ""

# Test 5: componentKeys with all statuses
echo "Test 5: componentKeys with all statuses"
curl -s -u "${TOKEN}:" "https://sonarcloud.io/api/issues/search?organization=${ORG}&componentKeys=${PROJECT}&statuses=OPEN,CONFIRMED,REOPENED,RESOLVED,CLOSED&ps=5" | python3 -c "$PYTHON_PARSE_SCRIPT" 2>/dev/null || echo "  Error" >&2
echo ""

# Test 6: componentKeys with branch (main)
echo "Test 6: componentKeys with branch=main"
curl -s -u "${TOKEN}:" "https://sonarcloud.io/api/issues/search?organization=${ORG}&componentKeys=${PROJECT}&branch=main&ps=5" | python3 -c "$PYTHON_PARSE_SCRIPT" 2>/dev/null || echo "  Error" >&2
echo ""

echo "Done testing."



