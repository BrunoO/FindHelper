# UI Search Matchers Reference

This document lists all available pattern matchers that users can use in UI search fields.

## Overview

The application supports **5 different matcher types**, automatically detected based on pattern content or explicit prefixes.

---

## 1. Substring Search (Default)

**Type**: `Substring`  
**Prefix**: None (default when no prefix or wildcards)  
**Performance**: ⚡ Fastest  
**Use Case**: Simple text search

### Description
Searches for the pattern as a literal substring anywhere in the text. This is the default matcher when no special syntax is used.

### Examples
```
file        → matches "file", "file.txt", "myfile", "file123"
test        → matches "test", "testing", "mytest", "test.cpp"
```

### When to Use
- Simple text search
- Finding files/directories containing specific text
- No pattern matching needed

---

## 2. Glob Patterns (Wildcards)

**Type**: `Glob`  
**Prefix**: None (auto-detected when `*` or `?` found)  
**Performance**: ⚡ Fast  
**Use Case**: File matching with wildcards

### Description
Uses wildcard patterns similar to shell globbing. Automatically detected when the pattern contains `*` or `?`.

### Wildcards
- `*` - Matches zero or more characters
- `?` - Matches exactly one character

### Examples
```
*.txt           → matches "file.txt", "document.txt", ".txt"
*.cpp           → matches "main.cpp", "test.cpp"
file?.*         → matches "file1.txt", "filea.cpp" (not "file.txt" or "file12.txt")
src/*.cpp       → matches "src/main.cpp" (not "src/foo/main.cpp")
test*.cpp       → matches "test.cpp", "test_main.cpp", "test123.cpp"
```

### When to Use
- Matching file extensions
- Simple filename patterns
- Shell-like wildcard matching

---

## 3. Simple Regex (`re:` prefix)

**Type**: `SimpleRegex`  
**Prefix**: `re:`  
**Performance**: ⚡ Fast  
**Use Case**: Basic regex patterns without full regex complexity

### Description
Rob Pike's simple regex matcher. Supports basic regex features without the overhead of full ECMAScript regex.

### Supported Features
- `.` - Matches any character
- `*` - Matches zero or more of preceding element
- `^` - Start of string anchor
- `$` - End of string anchor

### Examples
```
re:^main        → matches "main.cpp", "main.h" (starts with "main")
re:\.cpp$       → matches "file.cpp", "test.cpp" (ends with ".cpp")
re:^test.*\.cpp$ → matches "test_main.cpp" (starts with "test", ends with ".cpp")
re:.*\.(cpp|h)$ → matches "file.cpp", "file.h" (ends with .cpp or .h)
```

### When to Use
- Simple regex patterns
- Anchors (start/end of string)
- When you need regex but don't need full ECMAScript features
- Better performance than `rs:` for simple patterns

---

## 4. Path Pattern (`pp:` prefix)

**Type**: `PathPattern`  
**Prefix**: `pp:`  
**Performance**: ⚡⚡ Very Fast (DFA-optimized for simple patterns)  
**Use Case**: Advanced path matching with glob-like syntax and regex features

### Description
Advanced path pattern matcher optimized for file paths. Supports glob-style wildcards plus regex-like features including character classes, quantifiers, and shorthands.

### Features

#### Basic Wildcards
- `*` - Matches zero or more non-separator characters (doesn't cross `/` or `\`)
- `**` - Matches zero or more characters including separators (crosses directories)
- `?` - Matches exactly one non-separator character

#### Character Classes
- `[abc]` - Matches 'a', 'b', or 'c'
- `[0-9]` - Matches any digit
- `[a-z]` - Matches any lowercase letter
- `[^a]` - Matches any character except 'a' (negation)

#### Shorthands
- `\d` - Matches any digit (equivalent to `[0-9]`)
- `\w` - Matches word characters (letters, digits, underscore)

#### Quantifiers
- `?` - Zero or one (e.g., `file?.txt` matches "file.txt" or "file1.txt")
- `*` - Zero or more (e.g., `file*.txt` matches "file.txt", "file1.txt", "file123.txt")
- `+` - One or more (e.g., `\w+.txt` matches "file.txt" but not ".txt")
- `{n}` - Exactly n times (e.g., `\d{3}` matches exactly 3 digits)
- `{n,m}` - Between n and m times
- `{n,}` - n or more times

#### Anchors
- `^` - Start of path
- `$` - End of path

### Examples
```
pp:src/*.cpp                    → matches "src/main.cpp" (not "src/foo/main.cpp")
pp:src/**/*.cpp                 → matches "src/main.cpp", "src/foo/main.cpp", "src/a/b/c/main.cpp"
pp:**/*.txt                     → matches "file.txt", "dir/file.txt", "a/b/c/file.txt"
pp:file?.txt                    → matches "file1.txt", "filea.txt" (not "file.txt" or "file12.txt")
pp:**/[0-9]*.log                → matches "logs/123.log", "logs/error123.log"
pp:**/\\d{3}*.log              → matches "logs/123.log", "logs/error123.log"
pp:**/\\w+.txt                  → matches "dir/file_1.txt" (not "dir/file-name.txt")
pp:**/[A-Za-z]{2}_test.cpp      → matches "src/ab_test.cpp" (exactly 2 letters)
pp:^C:\\\\Windows\\\\**/*.exe$ → matches "C:\\Windows\\notepad.exe" (anchored)
```

### When to Use
- Complex path matching patterns
- Combining glob wildcards with regex features
- Matching file paths with specific requirements
- When you need `**` to cross directory boundaries
- Performance-critical path matching (uses DFA optimization)

---

## 5. Standard Regex (`rs:` prefix)

**Type**: `StdRegex`  
**Prefix**: `rs:`  
**Performance**: ⚠️ Slower (but optimized with caching)  
**Use Case**: Full ECMAScript regex patterns

### Description
Full ECMAScript regex engine using C++ `std::regex`. Most powerful but also slowest. Automatically optimized for simple patterns (routes to faster matchers when possible).

### Features
Full ECMAScript regex syntax including:
- Character classes: `[abc]`, `[^abc]`, `[a-z]`
- Quantifiers: `*`, `+`, `?`, `{n}`, `{n,m}`, `{n,}`
- Anchors: `^`, `$`
- Alternation: `(a|b)`
- Groups: `(abc)`
- And much more...

### Examples
```
rs:^main\.(cpp|h)$             → matches "main.cpp" or "main.h"
rs:.*\.md$                     → matches any file ending with ".md"
rs:\d{4}-\d{2}-\d{2}           → matches date patterns like "2024-12-25"
rs:test.*\.(cpp|hpp)$          → matches "test_main.cpp", "test_util.hpp"
rs:^[A-Z][a-z]+\.txt$          → matches "File.txt", "Document.txt" (capitalized)
```

### When to Use
- Complex regex patterns
- When you need full ECMAScript regex features
- Patterns that can't be expressed with simpler matchers
- When performance is less critical than pattern expressiveness

### Performance Note
The implementation automatically optimizes simple patterns:
- Literal patterns → substring search (fastest)
- Simple patterns → SimpleRegex (fast)
- Complex patterns → std::regex (slower, but cached)

---

## Detection Priority

Patterns are detected in this order:

1. **Prefix-based** (highest priority):
   - `rs:` → StdRegex
   - `re:` → SimpleRegex
   - `pp:` → PathPattern

2. **Content-based**:
   - Contains `*` or `?` → Glob

3. **Default**:
   - Everything else → Substring

---

## Performance Comparison

| Matcher | Speed | Use Case |
|---------|-------|----------|
| Substring | ⚡⚡⚡ Fastest | Simple text search |
| Glob | ⚡⚡ Fast | File wildcards |
| SimpleRegex | ⚡⚡ Fast | Basic regex |
| PathPattern | ⚡⚡⚡ Very Fast* | Path matching with features |
| StdRegex | ⚡ Slower | Full regex (auto-optimized) |

*PathPattern uses DFA optimization for simple patterns, making it very fast.

---

## Tips for Users

1. **Start simple**: Use substring search for basic needs
2. **Use globs**: For file extensions and simple wildcards (`*.txt`)
3. **Use `re:`**: For simple regex with anchors (`re:^main`)
4. **Use `pp:`**: For complex path patterns with `**` or character classes
5. **Use `rs:`**: Only when you need full regex features

---

## Case Sensitivity

All matchers respect the case sensitivity setting in the UI:
- **Case-sensitive**: Exact character matching
- **Case-insensitive**: Matches regardless of case

---

## Examples by Use Case

### Find all .txt files
```
*.txt
```

### Find files starting with "test"
```
re:^test
```

### Find files in src directory (any depth)
```
pp:src/**/*.cpp
```

### Find files with 3 digits in name
```
pp:**/*\d{3}*.log
```

### Find files matching date pattern
```
rs:\d{4}-\d{2}-\d{2}.*\.log
```

### Find files with specific extension (case-insensitive)
```
*.TXT  (with case-insensitive enabled)
```

---

## Notes

- Empty patterns match everything
- Patterns with prefixes must have content after the prefix (e.g., `rs:` alone is invalid)
- PathPattern (`pp:`) is optimized for file paths and supports Windows path separators (`\` and `/`)
- All matchers work in both filename and path search fields

