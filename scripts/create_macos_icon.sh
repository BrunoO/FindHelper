#!/bin/bash
# Script to convert app_icon.ico to app_icon.icns for macOS
# Run this script from the project root directory

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ICO_FILE="$PROJECT_ROOT/app_icon.ico"
ICNS_FILE="$PROJECT_ROOT/app_icon.icns"
ICONSET_DIR="$PROJECT_ROOT/AppIcon.iconset"

echo "=== macOS Icon Generator ==="
echo ""

# Check if source file exists
if [[ ! -f "$ICO_FILE" ]]; then
    echo "Error: app_icon.ico not found in project root" >&2
    exit 1
fi

# Clean up any existing iconset
rm -rf "$ICONSET_DIR"
mkdir -p "$ICONSET_DIR"

# Method 1: Try using ImageMagick (if installed)
if command -v magick &> /dev/null || command -v convert &> /dev/null; then
    echo "Using ImageMagick to extract and resize icons..."
    
    CONVERT_CMD="convert"
    if command -v magick &> /dev/null; then
        CONVERT_CMD="magick"
    fi
    
    # Extract largest image from ICO and create all required sizes
    $CONVERT_CMD "$ICO_FILE[0]" -resize 1024x1024 "$ICONSET_DIR/icon_512x512@2x.png"
    $CONVERT_CMD "$ICO_FILE[0]" -resize 512x512   "$ICONSET_DIR/icon_512x512.png"
    $CONVERT_CMD "$ICO_FILE[0]" -resize 512x512   "$ICONSET_DIR/icon_256x256@2x.png"
    $CONVERT_CMD "$ICO_FILE[0]" -resize 256x256   "$ICONSET_DIR/icon_256x256.png"
    $CONVERT_CMD "$ICO_FILE[0]" -resize 256x256   "$ICONSET_DIR/icon_128x128@2x.png"
    $CONVERT_CMD "$ICO_FILE[0]" -resize 128x128   "$ICONSET_DIR/icon_128x128.png"
    $CONVERT_CMD "$ICO_FILE[0]" -resize 64x64     "$ICONSET_DIR/icon_32x32@2x.png"
    $CONVERT_CMD "$ICO_FILE[0]" -resize 32x32     "$ICONSET_DIR/icon_32x32.png"
    $CONVERT_CMD "$ICO_FILE[0]" -resize 32x32     "$ICONSET_DIR/icon_16x16@2x.png"
    $CONVERT_CMD "$ICO_FILE[0]" -resize 16x16     "$ICONSET_DIR/icon_16x16.png"

# Method 2: Try using sips with a PNG source (if user provides one)
elif [[ -f "$PROJECT_ROOT/app_icon.png" ]]; then
    echo "Using sips with app_icon.png..."
    PNG_SOURCE="$PROJECT_ROOT/app_icon.png"
    
    sips -z 1024 1024 "$PNG_SOURCE" --out "$ICONSET_DIR/icon_512x512@2x.png"
    sips -z 512 512   "$PNG_SOURCE" --out "$ICONSET_DIR/icon_512x512.png"
    sips -z 512 512   "$PNG_SOURCE" --out "$ICONSET_DIR/icon_256x256@2x.png"
    sips -z 256 256   "$PNG_SOURCE" --out "$ICONSET_DIR/icon_256x256.png"
    sips -z 256 256   "$PNG_SOURCE" --out "$ICONSET_DIR/icon_128x128@2x.png"
    sips -z 128 128   "$PNG_SOURCE" --out "$ICONSET_DIR/icon_128x128.png"
    sips -z 64 64     "$PNG_SOURCE" --out "$ICONSET_DIR/icon_32x32@2x.png"
    sips -z 32 32     "$PNG_SOURCE" --out "$ICONSET_DIR/icon_32x32.png"
    sips -z 32 32     "$PNG_SOURCE" --out "$ICONSET_DIR/icon_16x16@2x.png"
    sips -z 16 16     "$PNG_SOURCE" --out "$ICONSET_DIR/icon_16x16.png"

else
    echo "" >&2
    echo "Error: Neither ImageMagick nor app_icon.png found." >&2
    echo "" >&2
    echo "Options to fix:" >&2
    echo "  1. Install ImageMagick: brew install imagemagick" >&2
    echo "  2. Export app_icon.ico to app_icon.png (1024x1024) manually" >&2
    echo "" >&2
    rm -rf "$ICONSET_DIR"
    exit 1
fi

# Generate the .icns file
echo ""
echo "Generating app_icon.icns..."
iconutil -c icns "$ICONSET_DIR" -o "$ICNS_FILE"

# Clean up
rm -rf "$ICONSET_DIR"

echo ""
echo "✅ Success! Created: $ICNS_FILE"
echo ""
echo "Rebuild the project to include the new icon in the app bundle."



