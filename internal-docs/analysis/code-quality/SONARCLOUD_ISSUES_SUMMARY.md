# SonarCloud Issues Summary - USN_WINDOWS Project

**Date:** 2026-01-15  
**Project:** BrunoO_USN_WINDOWS  
**Project Key:** `BrunoO_USN_WINDOWS`

---

## Quality Gate Status

**Status:** ❌ **ERROR**

### Quality Gate Conditions

| Metric | Status | Threshold | Actual Value |
|--------|--------|-----------|--------------|
| **New Reliability Rating** | ✅ OK | ≤ 1 | 1 |
| **New Security Rating** | ✅ OK | ≤ 1 | 1 |
| **New Maintainability Rating** | ✅ OK | ≤ 1 | 1 |
| **New Duplicated Lines Density** | ❌ **ERROR** | ≤ 3% | **12.98%** |
| **New Security Hotspots Reviewed** | ✅ OK | ≥ 100% | 100% |

**Blocking Issue:** New duplicated lines density is **12.98%**, exceeding the threshold of **3%**.

---

## Project Metrics

### Code Statistics
- **Lines of Code (NCLOC):** 22,269
- **Cognitive Complexity:** 3,964
- **Duplicated Lines:** 1,746

### Issue Counts

| Issue Type | Count | Status |
|------------|-------|--------|
| **Bugs** | 21 | ⚠️ Needs attention |
| **Code Smells** | 1,263 | ⚠️ High |
| **Vulnerabilities** | 0 | ✅ None |
| **Security Hotspots** | 17 | ⚠️ Review needed |

---

## Issue Breakdown

### Bugs: 21
**Priority:** High - These are actual bugs that could cause runtime errors.

### Code Smells: 1,263
**Priority:** Medium - These are maintainability issues that don't cause bugs but make code harder to maintain.

### Security Hotspots: 17
**Priority:** Medium - These need security review to determine if they're actual vulnerabilities.

### Vulnerabilities: 0
**Status:** ✅ Good - No known security vulnerabilities.

---

## Quality Gate Failure Analysis

### Duplicated Lines Density: 12.98% (Threshold: 3%)

**Problem:** The project has **1,746 duplicated lines** out of **22,269 total lines**, resulting in **12.98% duplication**.

**Impact:**
- Makes code harder to maintain
- Increases risk of bugs (fixes need to be applied in multiple places)
- Violates DRY (Don't Repeat Yourself) principle

**Recommendation:**
1. Identify duplicated code blocks
2. Extract common functionality into reusable functions/classes
3. Focus on recent changes (new duplicated lines) first
4. Use SonarCloud duplication detection to find specific duplicated blocks

---

## Next Steps

### Immediate Actions
1. ✅ **Address Duplication** - Reduce duplicated lines density from 12.98% to below 3%
   - Focus on new duplicated lines first
   - Extract common patterns into reusable functions

2. ⚠️ **Review Bugs** - Investigate and fix the 21 bugs
   - Prioritize by severity (BLOCKER, HIGH, MEDIUM, LOW)
   - Check if any are false positives

3. ⚠️ **Review Security Hotspots** - Review the 17 security hotspots
   - Determine if they're actual security issues
   - Fix or mark as reviewed

### Medium-Term Actions
4. 📊 **Reduce Code Smells** - Address the 1,263 code smells
   - Prioritize by impact and effort
   - Focus on high-impact, low-effort fixes first

5. 🔍 **Reduce Cognitive Complexity** - Current complexity is 3,964
   - Refactor complex functions
   - Break down large functions into smaller, focused ones

---

## How to View Issues in SonarCloud

1. **Go to your project:** https://sonarcloud.io/project/overview?id=BrunoO_USN_WINDOWS

2. **View Issues by Type:**
   - **Bugs:** https://sonarcloud.io/project/issues?id=BrunoO_USN_WINDOWS&types=BUG
   - **Code Smells:** https://sonarcloud.io/project/issues?id=BrunoO_USN_WINDOWS&types=CODE_SMELL
   - **Security Hotspots:** https://sonarcloud.io/project/issues?id=BrunoO_USN_WINDOWS&types=SECURITY_HOTSPOT

3. **View Duplications:**
   - https://sonarcloud.io/project/duplications?id=BrunoO_USN_WINDOWS

4. **View Quality Gate:**
   - https://sonarcloud.io/project/quality_gate?id=BrunoO_USN_WINDOWS

---

## Notes

- **Issue Search API:** The SonarCloud API search returned 0 issues, but component measures show issues exist. This might be because:
  - Issues are on a specific branch
  - Issues are resolved/closed
  - API parameters need adjustment
  - Use the SonarCloud web interface for detailed issue browsing

- **Quality Gate:** Currently failing due to duplication. All other metrics are passing.

---

## Related Documentation

- [SonarCloud Troubleshooting Guide](./SONARCLOUD_TROUBLESHOOTING.md)
- [AppBootstrap Analysis](./SONARQUBE_APPBOOTSTRAP_ANALYSIS.md)


