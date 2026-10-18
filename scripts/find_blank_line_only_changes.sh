#!/bin/bash

# Script to identify files modified only by adding blank lines at the end
# and optionally discard those changes

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to remove trailing blank lines from a file
remove_trailing_blanks() {
    # Use awk to remove trailing blank lines
    # This preserves all content except trailing empty lines
    awk '
    {
        lines[NR] = $0
        if (NF > 0 || length($0) > 0) {
            last_nonblank = NR
        }
    }
    END {
        for (i = 1; i <= last_nonblank; i++) {
            print lines[i]
        }
    }'
    return 0
}

# Function to check if a file's changes are only trailing blank lines
check_blank_line_only_change() {
    local file="$1"
    local check_staged="${2:-false}"  # Optional: check staged changes instead of unstaged
    
    # Skip if file is not tracked by git
    if ! git ls-files --error-unmatch -- "$file" &>/dev/null; then
        return 1
    fi
    
    # Get the diff for this file (staged or unstaged)
    local diff_output
    if [[ "$check_staged" == "true" ]]; then
        diff_output=$(git diff --cached -- "$file" 2>/dev/null || echo "")
    else
        diff_output=$(git diff -- "$file" 2>/dev/null || echo "")
    fi
    
    # If no diff, skip
    if [[ -z "$diff_output" ]]; then
        return 1
    fi
    
    # Check if diff contains any deletions (lines starting with - but not --- header)
    if echo "$diff_output" | grep -qE '^-[^-]'; then
        # Has deletions, so not just blank line additions
        return 1
    fi
    
    # Get HEAD version and comparison version, both with trailing blanks removed
    local head_file
    head_file=$(mktemp)
    local compare_file
    compare_file=$(mktemp)
    
    # Get HEAD version
    git show "HEAD:$file" 2>/dev/null | remove_trailing_blanks > "$head_file" || touch "$head_file"
    
    # Get comparison version (staged or working)
    if [[ "$check_staged" == "true" ]]; then
        git show ":0:$file" 2>/dev/null | remove_trailing_blanks > "$compare_file" || touch "$compare_file"
    else
        cat "$file" 2>/dev/null | remove_trailing_blanks > "$compare_file" || touch "$compare_file"
    fi
    
    # Compare content (ignoring trailing blanks)
    local files_match=false
    if diff -q "$head_file" "$compare_file" &>/dev/null; then
        files_match=true
    fi
    
    # Clean up temp files
    rm -f "$head_file" "$compare_file"
    
    if [[ "$files_match" != "true" ]]; then
        return 1
    fi
    
    # Now verify that the diff only shows blank line additions
    # Extract all added lines (lines starting with + but not +++ which is the file header)
    local added_lines
    added_lines=$(echo "$diff_output" | grep -E '^\+' | grep -vE '^\+\+\+' || true)
    
    if [[ -z "$added_lines" ]]; then
        return 1
    fi
    
    # Check if all added lines are blank (only whitespace)
    local all_blank=true
    while IFS= read -r line || [[ -n "$line" ]]; do
        # Remove the '+' prefix and check if remaining is blank
        local content="${line#+}"
        # Remove all whitespace and check if anything remains
        local trimmed="${content//[[:space:]]/}"
        if [[ -n "$trimmed" ]]; then
            all_blank=false
            break
        fi
    done <<< "$added_lines"
    
    if [[ "$all_blank" == "true" ]]; then
        return 0  # Only blank lines added
    fi
    
    return 1  # Other changes exist
}

# Main script
main() {
    echo "Checking for files modified only by trailing blank lines..."
    echo ""
    
    # Get list of modified files (unstaged and staged)
    local unstaged_files
    unstaged_files=$(git diff --name-only 2>/dev/null || echo "")
    local staged_files
    staged_files=$(git diff --cached --name-only 2>/dev/null || echo "")
    
    if [[ -z "$unstaged_files" && -z "$staged_files" ]]; then
        echo -e "${GREEN}No modified files found.${NC}"
        exit 0
    fi
    
    # Find files with only blank line changes
    local blank_line_only_files=()
    local staged_blank_line_files=()
    
    # Check unstaged files
    while IFS= read -r file; do
        if [[ -n "$file" && -f "$file" ]] && check_blank_line_only_change "$file" false; then
            blank_line_only_files+=("$file")
        fi
    done <<< "$unstaged_files"
    
    # Check staged files
    while IFS= read -r file; do
        if [[ -n "$file" && -f "$file" ]] && check_blank_line_only_change "$file" true; then
            staged_blank_line_files+=("$file")
        fi
    done <<< "$staged_files"
    
    # Report results
    local unstaged_count=${#blank_line_only_files[@]}
    local staged_count=${#staged_blank_line_files[@]}
    local total_count=$((unstaged_count + staged_count))
    
    if [[ $total_count -eq 0 ]]; then
        echo -e "${GREEN}No files found that were modified only by adding trailing blank lines.${NC}"
        exit 0
    fi
    
    echo -e "${YELLOW}Found $total_count file(s) modified only by trailing blank lines:${NC}"
    echo ""
    
    if [[ $unstaged_count -gt 0 ]]; then
        echo -e "${YELLOW}Unstaged files:${NC}"
        for file in "${blank_line_only_files[@]}"; do
            echo "  - $file"
        done
        echo ""
    fi
    
    if [[ $staged_count -gt 0 ]]; then
        echo -e "${YELLOW}Staged files:${NC}"
        for file in "${staged_blank_line_files[@]}"; do
            echo "  - $file"
        done
        echo ""
    fi
    
    echo -e "${YELLOW}Do you want to discard changes to these files? (y/N)${NC}"
    read -r response
    
    if [[ "$response" =~ ^[Yy]$ ]]; then
        echo ""
        echo "Discarding changes..."
        
        # Discard unstaged changes
        if [[ $unstaged_count -gt 0 ]]; then
            for file in "${blank_line_only_files[@]}"; do
                if git checkout -- "$file" 2>/dev/null || git restore "$file" 2>/dev/null; then
                    echo -e "${GREEN}✓${NC} Discarded unstaged changes to $file"
                else
                    echo -e "${RED}✗${NC} Failed to discard unstaged changes to $file"
                fi
            done
        fi
        
        # Discard staged changes
        if [[ $staged_count -gt 0 ]]; then
            for file in "${staged_blank_line_files[@]}"; do
                if git restore --staged "$file" 2>/dev/null && (git checkout -- "$file" 2>/dev/null || git restore "$file" 2>/dev/null); then
                    echo -e "${GREEN}✓${NC} Discarded staged changes to $file"
                else
                    echo -e "${RED}✗${NC} Failed to discard staged changes to $file"
                fi
            done
        fi
        
        echo ""
        echo -e "${GREEN}Done!${NC}"
    else
        echo "Changes kept."
    fi
    return 0
}

# Run main function
main "$@"

