# SonarQube Issues Review Prompt

**Purpose:** Analyze SonarQube issues to identify code quality problems, prevent future issues through rule improvements, and prioritize quick wins for immediate improvement.

---

## Prompt

```
You are a Senior Code Quality Engineer specializing in C++ static analysis and SonarQube rule configuration. Review SonarQube issues for this cross-platform file indexing application to identify patterns, improve development practices, and prioritize fixes.

## Application Context
- **Function**: File system indexer with real-time monitoring via Windows USN Journal
- **Codebase**: C++17 with Windows API interop, multi-threaded architecture
- **Quality Standards**: Follow project conventions in AGENTS.md, C++17 best practices
- **SonarQube**: Issues tracked in SonarCloud with automated analysis

---

## Review Process

### Step 1: Fetch Current Issues
1. **Update the issue list** by running:
   ```bash
   ./scripts/fetch_sonar_results.sh --format json --open-only
   ```
2. **Study the generated file**: `./sonar-results/sonarqube_issues.json`
3. **Parse and categorize** all open issues by:
   - Rule ID (e.g., `cpp:S5008`, `cpp:S134`)
   - Severity (BLOCKER, CRITICAL, MAJOR, MINOR, INFO)
   - Component/file location
   - Issue type (bug, vulnerability, code smell, security hotspot)

### Step 2: Analyze Issues Created Today
1. **Filter issues** created in the last 24 hours (check `creationDate` field)
2. **Group by pattern**:
   - Same rule violated multiple times
   - Same file with multiple issues
   - Same developer pattern (if metadata available)
3. **Identify root causes**:
   - Missing IDE/linter configuration
   - Unclear coding standards
   - Insufficient code review process
   - Missing pre-commit hooks

### Step 3: Identify Missing Rules
1. **Review project standards** in `AGENTS.md` and compare with active SonarQube rules
2. **Identify gaps** where project rules exist but SonarQube rules are missing:
   - Naming convention violations not caught
   - Platform-specific patterns not enforced
   - Project-specific best practices not covered
3. **Recommend rule additions**:
   - Custom rules that could be added to SonarQube
   - IDE/linter rules that should be configured
   - Pre-commit hooks that could catch issues early
   - Code review checklist items

### Step 4: Generate Quick Wins List
1. **Identify low-effort, high-impact fixes**:
   - Issues fixable in < 15 minutes each
   - Issues affecting multiple files (fix once, apply pattern)
   - Issues with clear, automated fixes available
2. **Prioritize by impact**:
   - Security vulnerabilities (highest priority)
   - Code smells affecting maintainability
   - Performance issues in hot paths
   - Consistency improvements (naming, formatting)

---

## Analysis Categories

### 1. Rule Violation Patterns

**Repeated Violations**
- Same rule violated across multiple files (indicates missing rule enforcement)
- Same file with multiple instances of same rule (indicates local pattern)
- Rules violated in new code vs. legacy code (indicates process gaps)

**Rule Coverage Gaps**
- Project standards in `AGENTS.md` not covered by SonarQube rules
- Common patterns in codebase not detected by static analysis
- Platform-specific issues (Windows API usage) not caught

**Rule Configuration Issues**
- Rules too strict causing false positives
- Rules too lenient missing real issues
- Missing custom rules for project-specific patterns

---

### 2. Issue Severity Analysis

**Critical/Blocker Issues**
- Security vulnerabilities requiring immediate attention
- Bugs that could cause crashes or data corruption
- Performance issues affecting user experience

**Major Issues**
- Code smells affecting maintainability
- Potential bugs that haven't manifested yet
- Technical debt accumulating over time

**Minor/Info Issues**
- Style inconsistencies
- Documentation gaps
- Optimization opportunities

---

### 3. File-Level Analysis

**Hotspot Files**
- Files with many issues (potential refactoring candidates)
- Files with issues across multiple categories (complexity issues)
- Files with recent issues (active development areas)

**Issue Distribution**
- Are issues concentrated in specific modules?
- Are issues spread evenly across codebase?
- Are new issues appearing in legacy code or new code?

---

### 4. Temporal Analysis

**New Issues**
- Issues created today (immediate attention needed)
- Issues created this week (trending problems)
- Issues created this month (ongoing patterns)

**Issue Trends**
- Are issue counts increasing or decreasing?
- Are new issues following same patterns as old issues?
- Are fixes being applied or issues accumulating?

---

### 5. Prevention Strategies

**Development Process**
- Missing pre-commit hooks for common issues
- Code review not catching issues before merge
- IDE not configured with project standards
- CI/CD not running SonarQube analysis

**Rule Configuration**
- Missing rules that could prevent common issues
- Rules not properly configured for project standards
- Custom rules needed for project-specific patterns

**Developer Education**
- Common mistakes indicating need for training
- Patterns suggesting unclear documentation
- Repeated violations suggesting process gaps

---

## Output Format

For each analysis category:

### Issue Report
1. **Issue ID**: SonarQube issue key
2. **Rule**: Rule ID and description
3. **Location**: File:line or component
4. **Severity**: BLOCKER / CRITICAL / MAJOR / MINOR / INFO
5. **Type**: Bug / Vulnerability / Code Smell / Security Hotspot
6. **Created**: Date when issue was created
7. **Message**: SonarQube issue message
8. **Effort**: Estimated time to fix (S/M/L)
9. **Impact**: User-facing impact or technical debt impact

### Pattern Analysis
1. **Pattern**: Description of repeated issue pattern
2. **Frequency**: Number of occurrences
3. **Files Affected**: List of files with this pattern
4. **Root Cause**: Why this pattern exists
5. **Prevention**: How to prevent future occurrences

### Missing Rules Report
1. **Project Standard**: Rule from AGENTS.md or project conventions
2. **Current Coverage**: How it's currently enforced (if at all)
3. **Gap**: What's missing
4. **Recommendation**: How to add rule (SonarQube custom rule, IDE config, pre-commit hook, etc.)
5. **Priority**: High / Medium / Low

### Quick Wins List
1. **Issue**: Brief description
2. **Location**: File(s) affected
3. **Effort**: Time estimate (< 15 min, 15-30 min, 30-60 min)
4. **Impact**: High / Medium / Low
5. **Fix**: Specific code change or pattern to apply
6. **Priority**: Order for addressing

---

## Summary Requirements

End with:

### Executive Summary
- **Total Open Issues**: Count by severity
- **Issues Created Today**: Count and breakdown
- **Code Quality Score**: Current SonarQube rating (A/B/C/D/E/F)
- **Trend**: Improving / Stable / Degrading

### Top Priorities
- **Critical Issues**: Must fix immediately (security, crashes)
- **High-Impact Quick Wins**: Top 5 issues fixable in < 1 hour total
- **Pattern Fixes**: Issues where fixing one pattern resolves multiple issues

### Prevention Recommendations
- **Missing Rules**: Top 3 rules to add that would prevent most new issues
- **Process Improvements**: Changes to development workflow to catch issues earlier
- **Configuration Updates**: SonarQube, IDE, or tooling changes needed

### Action Plan
- **Immediate** (this week): Critical issues and top quick wins
- **Short-term** (this month): Pattern fixes and rule additions
- **Long-term** (this quarter): Process improvements and comprehensive rule coverage
```

---

## Usage Context- **Daily**: Review issues created today to catch problems early
- **Weekly**: Analyze patterns and identify quick wins for sprint planning
- **Before Releases**: Ensure no critical issues are blocking release
- **After Major Features**: Review new issues introduced by feature work
- **During Code Reviews**: Reference quick wins list for immediate improvements
- **Process Improvement**: Identify missing rules and prevention strategies
