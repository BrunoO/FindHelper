# Running clang-tidy on macOS

This guide explains how to run clang-tidy static analysis on macOS for the USN_WINDOWS project.

In the commands below, `$PROJECT_ROOT` is the path to your clone of the repository (the directory containing `CMakeLists.txt`). For example: `export PROJECT_ROOT=$(pwd)` when you are in the repo root.

## Prerequisites

### Install clang-tidy

**Option 1: Homebrew (Recommended)**
```bash
brew install llvm
```

**Option 2: Xcode Command Line Tools**
```bash
xcode-select --install
# Note: Xcode's clang-tidy may be older
```

### Verify Installation

```bash
# If installed via Homebrew (Apple Silicon)
/opt/homebrew/opt/llvm/bin/clang-tidy --version

# If installed via Homebrew (Intel)
/usr/local/opt/llvm/bin/clang-tidy --version

# If in PATH
clang-tidy --version
```

## Setup

### 1. Generate Compilation Database

clang-tidy needs a compilation database (`compile_commands.json`) to understand your project's build configuration.

**Using CMake:**
```bash
cd $PROJECT_ROOT
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
# This creates compile_commands.json in the build directory
```

**Link to project root (optional but recommended):**
```bash
# Create symlink so clang-tidy can find it
ln -sf build/compile_commands.json compile_commands.json
```

### 2. Add clang-tidy to PATH (Optional)

If installed via Homebrew, add to your `~/.zshrc`:
```bash
# For Apple Silicon
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"

# For Intel Mac
export PATH="/usr/local/opt/llvm/bin:$PATH"
```

Then reload:
```bash
source ~/.zshrc
```

## Running clang-tidy

### Check a Single File

```bash
# Using full path (if not in PATH)
/opt/homebrew/opt/llvm/bin/clang-tidy Application.cpp

# If in PATH
clang-tidy Application.cpp
```

### Check Multiple Files

```bash
# Check all C++ files (excluding external/)
find . -name "*.cpp" -o -name "*.h" | grep -v external | xargs clang-tidy

# Check specific directory
find ui -name "*.cpp" -o -name "*.h" | xargs clang-tidy
```

### Apply Automatic Fixes

```bash
# Fix a single file
clang-tidy Application.cpp -fix

# Fix multiple files
find . -name "*.cpp" | grep -v external | xargs clang-tidy -fix
```

### Check Specific Rules

```bash
# Check only include order
clang-tidy Application.cpp -checks='readability-include-order'

# Check only naming conventions
clang-tidy Application.cpp -checks='readability-identifier-naming'

# Check multiple specific rules
clang-tidy Application.cpp -checks='readability-include-order,readability-identifier-naming'
```

### Use Project Configuration

The project's `.clang-tidy` file is automatically used when running clang-tidy from the project root:

```bash
cd $PROJECT_ROOT
clang-tidy Application.cpp  # Uses .clang-tidy automatically
```

## Common Workflows

### Quick Check Before Committing

```bash
# Check files you've modified
git diff --name-only HEAD | grep -E '\.(cpp|h)$' | xargs clang-tidy
```

### Full Project Check

```bash
# Generate compilation database first
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Check all project files
find . -name "*.cpp" -o -name "*.h" | \
  grep -v external | \
  grep -v build | \
  xargs clang-tidy
```

### Check and Fix Include Order

```bash
# Check include order issues
clang-tidy Application.h -checks='readability-include-order' -- -std=c++17

# Fix automatically (where possible)
clang-tidy Application.h -checks='readability-include-order' -fix -- -std=c++17
```

## Troubleshooting

### "clang-tidy: command not found"

**Solution:** Use full path or add to PATH:
```bash
# Find clang-tidy
find /opt/homebrew /usr/local -name clang-tidy 2>/dev/null

# Use full path
/opt/homebrew/opt/llvm/bin/clang-tidy file.cpp
```

### "Could not find compile_commands.json"

**Solution:** Generate compilation database:
```bash
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
ln -sf build/compile_commands.json compile_commands.json
```

### "No such file or directory" errors

**Solution:** Ensure you're running from project root and compilation database is up to date:
```bash
cd $PROJECT_ROOT
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

### Include path errors

**Solution:** Specify include paths manually:
```bash
clang-tidy file.cpp -- -I./external/imgui -I./external/doctest -std=c++17
```

### "'memory' file not found" or other system header errors

**Problem:** Homebrew's LLVM clang-tidy can't find system C++ headers.

**Solution 1: Use system include paths (Recommended)**
```bash
# Find system C++ headers
xcrun --show-sdk-path

# Run clang-tidy with system includes
/opt/homebrew/opt/llvm/bin/clang-tidy Application.h \
  --extra-arg=-I$(xcrun --show-sdk-path)/usr/include/c++/v1 \
  --extra-arg=-I$(xcrun --show-sdk-path)/usr/include
```

**Solution 2: Create a wrapper script**
Create `scripts/clang-tidy-wrapper.sh`:
```bash
#!/bin/bash
SDK_PATH=$(xcrun --show-sdk-path)
/opt/homebrew/opt/llvm/bin/clang-tidy "$@" \
  --extra-arg=-I${SDK_PATH}/usr/include/c++/v1 \
  --extra-arg=-I${SDK_PATH}/usr/include
```

Make it executable:
```bash
chmod +x scripts/clang-tidy-wrapper.sh
```

Then use it:
```bash
./scripts/clang-tidy-wrapper.sh Application.h
```

**Solution 3: Set environment variable (if supported)**
```bash
export CPATH="$(xcrun --show-sdk-path)/usr/include/c++/v1:$(xcrun --show-sdk-path)/usr/include"
/opt/homebrew/opt/llvm/bin/clang-tidy Application.h
```

**Note:** The compilation database (`compile_commands.json`) should contain the correct include paths. If errors persist, regenerate it:
```bash
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

## Integration with Editors

### Visual Studio Code

1. Install "clang-tidy" extension
2. Configure in `.vscode/settings.json`:
```json
{
  "clang-tidy.path": "/opt/homebrew/opt/llvm/bin/clang-tidy",
  "clang-tidy.compileCommandsPath": "${workspaceFolder}/compile_commands.json"
}
```

### CLion

1. Settings → Editor → Inspections
2. Enable "Clang-Tidy" inspections
3. Configure clang-tidy path if needed

## Example: Checking Include Order

```bash
# Check include order for a specific file
/opt/homebrew/opt/llvm/bin/clang-tidy Application.h \
  -checks='readability-include-order' \
  -- -std=c++17

# Output will show issues like:
# Application.h:30:1: warning: #include "CommandLineArgs.h" should be moved to the top [readability-include-order]
```

## Notes

- The project's `.clang-tidy` configuration includes include order rules
- clang-tidy automatically uses `.clang-tidy` when run from project root
- Some fixes require manual intervention (e.g., complex include dependencies)
- Always review automatic fixes before committing

