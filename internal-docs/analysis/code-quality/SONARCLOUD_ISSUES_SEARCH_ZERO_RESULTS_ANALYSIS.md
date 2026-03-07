# SonarCloud Issues Search Returns 0 Results - Analysis

## Problem
The SonarCloud MCP server's `search_sonar_issues_in_projects` tool returns 0 issues, even though the `get_component_measures` tool shows that issues exist:
- **13 Bugs**
- **1,354 Code Smells**
- **1 Vulnerability**
- **22 Security Hotspots**

## MCP Log Analysis

From the MCP log, we can see:
```
19:12:38.085 [boundedElastic-1] INFO  o.s.sonarqube.mcp.log.McpLogger - Tool called: search_sonar_issues_in_projects
19:12:38.730 [boundedElastic-1] INFO  o.s.sonarqube.mcp.log.McpLogger - Tool completed: search_sonar_issues_in_projects (execution time: 643ms)
```

The tool completes successfully (no errors), but returns 0 issues.

## Root Cause Analysis

Based on SonarCloud API documentation and common issues, the likely causes are:

### 1. **Branch Specification Missing**
SonarCloud projects with multiple branches require explicit branch specification. If issues are on a specific branch (not the main branch), they won't appear in the default query.

**Solution:** The MCP tool might need a `branch` parameter to specify which branch to query.

### 2. **Issue Status Filtering**
The SonarCloud API may filter issues by status by default. Issues that are:
- **RESOLVED** (fixed)
- **CLOSED** (won't fix)
- **FALSE-POSITIVE**

...might be excluded from the default search.

**Solution:** Explicitly include all statuses: `statuses=OPEN,CONFIRMED,REOPENED,RESOLVED,CLOSED`

### 3. **Component Keys vs Projects Parameter**
The SonarCloud API accepts both `projects` and `componentKeys` parameters. There might be a difference in how they work, especially with branches.

**Solution:** Try using `componentKeys` instead of `projects` parameter.

### 4. **API Default Behavior**
The SonarCloud issues search API might have default filters that exclude certain types of issues or only show issues from the latest analysis.

## Current Workaround

Since the issues search API returns 0 results, we're using the **component measures API** to get issue counts:

```json
{
  "bugs": 13,
  "code_smells": 1354,
  "vulnerabilities": 1,
  "security_hotspots": 22
}
```

However, this only provides **counts**, not the **detailed issue list**.

## Recommended Solutions

### Option 1: Use SonarCloud Web Interface
View detailed issues directly in the web interface:
- **All Issues:** https://sonarcloud.io/project/issues?id=BrunoO_USN_WINDOWS
- **Bugs:** https://sonarcloud.io/project/issues?id=BrunoO_USN_WINDOWS&types=BUG
- **Code Smells:** https://sonarcloud.io/project/issues?id=BrunoO_USN_WINDOWS&types=CODE_SMELL

### Option 2: Modify MCP Tool Call Parameters
Try calling the MCP tool with additional parameters:
- Add `branch` parameter if the project uses branches
- Add `statuses` parameter to include all statuses
- Try `componentKeys` instead of `projects`

### Option 3: Use Direct API Calls
Use curl with explicit parameters:
```bash
curl -u TOKEN: "https://sonarcloud.io/api/issues/search?organization=BrunoO&componentKeys=BrunoO_USN_WINDOWS&statuses=OPEN,CONFIRMED,REOPENED,RESOLVED,CLOSED&ps=500"
```

### Option 4: Check Project Branch Configuration
Verify which branch the issues are on:
```bash
curl -u TOKEN: "https://sonarcloud.io/api/project_branches/list?project=BrunoO_USN_WINDOWS"
```

Then query that specific branch:
```bash
curl -u TOKEN: "https://sonarcloud.io/api/issues/search?organization=BrunoO&componentKeys=BrunoO_USN_WINDOWS&branch=BRANCH_NAME&ps=500"
```

## Next Steps

1. **Check if the project uses branches:**
   - Query the project branches API
   - If branches exist, try querying the main branch explicitly

2. **Try different API parameters:**
   - Use `componentKeys` instead of `projects`
   - Add explicit `statuses` parameter
   - Add `branch` parameter if applicable

3. **Verify token permissions:**
   - Ensure the token has "Browse" permission for the project
   - Check if the token can access issues (not just measures)

4. **Contact SonarCloud Support:**
   - If the measures show issues but the search returns 0, this might be a SonarCloud API issue
   - Report the discrepancy between measures and issues search

## References

- [SonarCloud API Documentation](https://sonarcloud.io/web_api/api/issues)
- [SonarCloud Issues Search API](https://sonarcloud.io/web_api/api/issues/search)
- [SonarCloud Component Measures API](https://sonarcloud.io/web_api/api/measures/component)



