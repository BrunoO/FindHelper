#!/bin/bash

# This script recursively lists all files and directories from a given root directory
# and saves the list to a specified output file.
#
# Usage: ./create_file_list.sh <root_directory> <output_file>
#
# It handles permission errors by redirecting them to /dev/null,
# ensuring the script continues to run without interruption.
# It also appends a trailing slash to directories to allow the C++
# application to correctly identify them.

if [[ "$#" -ne 2 ]]; then
    echo "Usage: $0 <root_directory> <output_file>" >&2
    exit 1
fi

ROOT_DIR="$1"
OUTPUT_FILE="$2"

# Check if the root directory exists
if [[ ! -d "$ROOT_DIR" ]]; then
    echo "Error: Directory '$ROOT_DIR' not found." >&2
    exit 1
fi

# Convert ROOT_DIR to absolute path
# This ensures all paths in the output are absolute, not relative
ROOT_DIR="$(cd "$ROOT_DIR" && pwd)"

# Clear the output file
> "$OUTPUT_FILE"

# Use find to recursively list all files and directories
# and pipe the output to a while loop to process each path.
# 2>/dev/null redirects any standard error (like permission denied) to null.
echo "Scanning directory: $ROOT_DIR"
find "$ROOT_DIR" -print0 2>/dev/null | while IFS= read -r -d $'\0' path; do
    if [[ -d "$path" ]]; then
        echo "$path/" >> "$OUTPUT_FILE"
    else
        echo "$path" >> "$OUTPUT_FILE"
    fi
done

echo "File list saved to: $OUTPUT_FILE"
