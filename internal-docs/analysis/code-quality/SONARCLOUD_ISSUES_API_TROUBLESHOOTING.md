# SonarCloud Issues API Troubleshooting - Complete Analysis

## Problem Summary
- **Measures API works:** Shows 13 bugs, 1,354 code smells, 1 vulnerability, 22 security hotspots
- **Issues Search API returns 0:** All parameter combinations tested return 0 issues
- **Web Interface works:** Issues are visible in SonarCloud web UI

## What We've Tested

### 1. Parameter Variations
- ✅ `componentKeys` parameter
- ✅ `projects` parameter  
- ✅ `resolved=true` / `resolved=false`
- ✅ `statuses=OPEN,CONFIRMED,REOPENED,RESOLVED,CLOSED`
- ✅ `branch=main`
- ✅ `inNewCodePeriod=true/false`
- ✅ Different `ps` (page size) values
- ✅ All severity levels

### 2. MCP Tool Calls
- ✅ `search_sonar_issues_in_projects` with various parameters
- ✅ All return 0 issues despite successful execution

### 3. Direct API Calls
- ✅ All curl-based API calls return 0 issues
- ✅ Token is valid (measures API works)

## Possible Root Causes

### 1. Token Permissions
The token might have permission to read **measures** but not **issues**. SonarCloud has separate permissions:
- **Browse** (for measures)
- **See Source Code** (for issues)

**Solution:** Generate a new token with full project access, or check token permissions in SonarCloud.

### 2. SonarCloud API Limitation
There might be a SonarCloud-specific limitation where:
- Issues are only accessible through the web interface
- The API requires additional authentication/authorization
- Issues are stored in a way that requires different query methods

### 3. Project Configuration
The project might be configured in a way that:
- Issues are only visible through the web UI
- API access to issues is restricted
- Issues are on a different analysis/branch that's not accessible via API

### 4. MCP Tool Implementation
The MCP tool might have a bug or limitation in how it queries SonarCloud.

## Current Workaround

Since the issues search API consistently returns 0, we're using:

### Option 1: Component Measures API (Current)
```json
{
  "bugs": 13,
  "code_smells": 1354,
  "vulnerabilities": 1,
  "security_hotspots": 22
}
```
**Limitation:** Only provides counts, not detailed issue lists.

### Option 2: SonarCloud Web Interface
- **All Issues:** https://sonarcloud.io/project/issues?id=BrunoO_USN_WINDOWS
- **Bugs:** https://sonarcloud.io/project/issues?id=BrunoO_USN_WINDOWS&types=BUG
- **Code Smells:** https://sonarcloud.io/project/issues?id=BrunoO_USN_WINDOWS&types=CODE_SMELL
- **Security Hotspots:** https://sonarcloud.io/project/issues?id=BrunoO_USN_WINDOWS&types=SECURITY_HOTSPOT

### Option 3: Export from SonarCloud
1. Go to SonarCloud web interface
2. Navigate to Issues page
3. Use browser export functionality (if available)
4. Or use browser automation to scrape issues

## Recommended Next Steps

### 1. Verify Token Permissions
1. Go to SonarCloud → My Account → Security
2. Check token permissions
3. Generate a new token with full project access
4. Update `~/.cursor/mcp.json` with new token

### 2. Check Project Settings
1. Go to SonarCloud project settings
2. Verify API access settings
3. Check if there are any restrictions on issue visibility

### 3. Contact SonarCloud Support
If measures show issues but API returns 0, this might be a SonarCloud bug. Report:
- Project: BrunoO_USN_WINDOWS
- Issue: Measures API shows issues, but issues/search API returns 0
- Token permissions: Has access to measures but not issues

### 4. Alternative: Use SonarCloud Export/Webhook
- Set up a webhook to export issues
- Use SonarCloud's export functionality (if available)
- Use browser automation to extract issues

## Test Scripts Created

- `scripts/test_sonar_issues_api.sh` - Tests various API parameter combinations

## Related Documentation

- [SonarCloud Issues Summary](./SONARCLOUD_ISSUES_SUMMARY.md)
- [SonarCloud Issues Search Zero Results Analysis](./SONARCLOUD_ISSUES_SEARCH_ZERO_RESULTS_ANALYSIS.md)
- [SonarCloud Troubleshooting](./SONARCLOUD_TROUBLESHOOTING.md)



