# SonarQube NOSONAR Comment Guide

**Last Updated:** 2026-01-06

## Overview

SonarQube **DOES recognize** `//NOSONAR` comments to suppress issues. However, there are important requirements and considerations.

## Supported Formats

### Standard Format (Recommended)
```cpp
//NOSONAR: cpp:S1854
```

### Alternative Formats (May Work)
```cpp
// NOSONAR: cpp:S1854          // Space before NOSONAR (optional)
//NOSONAR(cpp:S1854)            // Parentheses instead of colon (may work)
// NOSONAR(cpp:S1854)           // Space + parentheses
```

### Suppress All Issues on a Line
```cpp
//NOSONAR                       // Suppresses ALL issues on that line
```

## Critical Requirements

### 1. **Must Be on the Same Line**
The `//NOSONAR` comment **MUST** be on the same line as the code it's suppressing:

```cpp
// ✅ CORRECT - On same line
float font_size = settings.fontSize;  // NOSONAR(cpp:S1854) - ImGui pattern

// ❌ WRONG - On different line (won't work)
float font_size = settings.fontSize;
// NOSONAR(cpp:S1854) - This won't suppress the issue!
```

### 2. **Re-analysis Required**
SonarQube **MUST re-analyze** the code to recognize new `//NOSONAR` comments. The comments won't take effect until:
- A new SonarQube scan is run
- The file is re-analyzed by SonarQube

**This is why we still see issues in `sonarqube_issues.json` even after adding NOSONAR comments!**

### 3. **Format Verification**
The standard format according to SonarQube documentation is:
- `//NOSONAR: rule-key` (with colon)

However, our codebase uses:
- `// NOSONAR(rule)` (with parentheses) - 94 instances
- `// NOSONAR: rule` (with colon) - 23 instances  
- `// NOSONAR` (no rule specified) - 60 instances

**Recommendation:** Verify which format works with your SonarQube instance. The colon format (`//NOSONAR: cpp:S1854`) is the documented standard.

## Current Usage in Codebase

Based on analysis:
- **Total NOSONAR comments:** ~177 instances
- **Format with parentheses:** 94 instances (`// NOSONAR(rule)`)
- **Format with colon:** 23 instances (`// NOSONAR: rule`)
- **Format without rule:** 60 instances (`// NOSONAR`)

## Examples from Our Codebase

### ✅ Good Examples (On Same Line)
```cpp
// SettingsWindow.cpp
float font_size = settings.fontSize;  // NOSONAR(cpp:S1854) - ImGui pattern: modified by reference, then used after if

// FileIndex.cpp
double ElapsedMs() const {  // NOSONAR(cpp:S5817) - Already const, SonarQube false positive

// GeminiApiUtils.cpp
// NOSONAR(cpp:S1103) - Example pattern in documentation
```

### ⚠️ Potential Issues
```cpp
// FileIndex.cpp - This format may not work
// NOSONAR cpp:S107: This API is intentionally explicit...
std::vector<std::future<std::vector<uint64_t>>> FileIndex::SearchAsync(...)
```

**Issue:** The NOSONAR comment is on a **separate line** before the code. This may not suppress the issue on the function declaration line.

## Verification Steps

1. **Add NOSONAR comment** to suppress an issue
2. **Commit and push** the change
3. **Trigger SonarQube re-analysis** (or wait for scheduled scan)
4. **Verify** the issue is no longer reported

## Why Issues Still Appear

If you see issues in `sonarqube_issues.json` after adding NOSONAR comments:

1. **SonarQube hasn't re-analyzed yet** - Most common reason
2. **Wrong format** - Comment format may not be recognized
3. **Wrong line** - Comment must be on the same line as the code
4. **Rule key mismatch** - The rule key in NOSONAR must match exactly

## Best Practices

1. **Use standard format:** `//NOSONAR: cpp:S1854`
2. **Always include justification:** `//NOSONAR: cpp:S1854 - Brief reason`
3. **Place on same line:** Comment must be on the line with the code
4. **Be specific:** Use rule key to suppress only that rule, not all rules
5. **Document why:** Always include a brief justification

## Monitoring NOSONAR Usage

SonarQube can track NOSONAR usage:
- Rules exist to detect excessive NOSONAR usage
- Teams can monitor and review NOSONAR comments
- Use sparingly - only for legitimate false positives

## References

- [SonarQube FAQ - NOSONAR](https://docs.sonarsource.com/sonarcloud/appendices/frequently-asked-questions/)
- [SonarQube Server 2025.4 - Enhanced NOSONAR](https://www.sonarsource.com/blog/sonarqube-server-2025-4-faster-analysis-stronger-security-better-coverage/)

## Action Items

1. ✅ Verify NOSONAR format works with our SonarQube instance
2. ⚠️ Consider standardizing to `//NOSONAR: rule-key` format
3. ⚠️ Re-run SonarQube analysis after adding NOSONAR comments
4. ⚠️ Verify issues are actually suppressed after re-analysis

