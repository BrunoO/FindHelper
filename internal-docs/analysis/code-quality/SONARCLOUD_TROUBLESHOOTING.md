# SonarCloud Connection Troubleshooting Guide

## Problem
SonarCloud MCP server returns: **"Not authorized. Please check server credentials."**

## Root Cause
The MCP configuration file (`~/.cursor/mcp.json`) contains placeholder values instead of actual SonarCloud credentials.

## Current Configuration
```json
{
  "mcpServers": {
    "sonarqube-cloud": {
      "command": "docker",
      "args": [
        "run", "-i", "--rm",
        "-e", "SONARQUBE_TOKEN=your_token",      // ❌ Placeholder
        "-e", "SONARQUBE_ORG=your_org",          // ❌ Placeholder
        "mcp/sonarqube"
      ]
    }
  }
}
```

## Solution: Configure SonarCloud Credentials

### Step 1: Get Your SonarCloud Token

1. **Log in to SonarCloud:**
   - Go to https://sonarcloud.io
   - Sign in with your account

2. **Generate a User Token:**
   - Click on your profile icon (top right)
   - Go to **"My Account"** → **"Security"**
   - Scroll to **"Generate Tokens"** section
   - Enter a name (e.g., "Cursor MCP Integration")
   - Click **"Generate"**
   - **IMPORTANT:** Copy the token immediately (you won't be able to see it again!)

### Step 2: Find Your Organization Name

1. **From SonarCloud Dashboard:**
   - Go to https://sonarcloud.io
   - Your organization name appears in the URL: `https://sonarcloud.io/organizations/YOUR_ORG_NAME`
   - Or check the organization dropdown in the top navigation

2. **From Your Project:**
   - If you know your project key (e.g., `BrunoO_USN_WINDOWS`), the organization is typically the part before the underscore: `BrunoO`

### Step 3: Update MCP Configuration

Edit the file: `~/.cursor/mcp.json`

**Replace:**
```json
{
  "mcpServers": {
    "sonarqube-cloud": {
      "command": "docker",
      "args": [
        "run", "-i", "--rm",
        "-e", "SONARQUBE_TOKEN=your_token",
        "-e", "SONARQUBE_ORG=your_org",
        "mcp/sonarqube"
      ]
    }
  }
}
```

**With:**
```json
{
  "mcpServers": {
    "sonarqube-cloud": {
      "command": "docker",
      "args": [
        "run", "-i", "--rm",
        "-e", "SONARQUBE_TOKEN=YOUR_ACTUAL_TOKEN_HERE",
        "-e", "SONARQUBE_ORG=YOUR_ORG_NAME_HERE",
        "mcp/sonarqube"
      ]
    }
  }
}
```

**Example (with actual values):**
```json
{
  "mcpServers": {
    "sonarqube-cloud": {
      "command": "docker",
      "args": [
        "run", "-i", "--rm",
        "-e", "SONARQUBE_TOKEN=squ_abc123def456ghi789jkl012mno345pqr678stu901vwx234yz",
        "-e", "SONARQUBE_ORG=BrunoO",
        "mcp/sonarqube"
      ]
    }
  }
}
```

### Step 4: Restart Cursor

After updating the configuration:
1. **Save** the `mcp.json` file
2. **Restart Cursor** completely (quit and reopen)
3. The MCP server will reconnect with the new credentials

### Step 5: Verify Connection

After restarting, test the connection by running:
```
list_enterprises
```

Or try:
```
search_my_sonarqube_projects
```

If successful, you should see your organizations/projects listed.

---

## Alternative: Environment Variables

If you prefer not to store credentials in the config file, you can use environment variables:

1. **Set environment variables:**
   ```bash
   export SONARQUBE_TOKEN="your_token_here"
   export SONARQUBE_ORG="your_org_here"
   ```

2. **Update MCP config to use environment variables:**
   ```json
   {
     "mcpServers": {
       "sonarqube-cloud": {
         "command": "docker",
         "args": [
           "run", "-i", "--rm",
           "-e", "SONARQUBE_TOKEN",
           "-e", "SONARQUBE_ORG",
           "mcp/sonarqube"
         ]
       }
     }
   }
   ```

   Note: Without `=value`, Docker will read from the host environment.

3. **Restart Cursor** (make sure environment variables are set in the shell where Cursor is launched)

---

## Security Best Practices

1. **Never commit tokens to version control**
   - The `mcp.json` file should be in your home directory (`~/.cursor/mcp.json`), not in the project
   - If you accidentally commit it, **regenerate the token immediately**

2. **Use token with minimal permissions**
   - SonarCloud tokens inherit your user permissions
   - Consider creating a dedicated account for automation if needed

3. **Rotate tokens periodically**
   - Regenerate tokens every 90 days or if compromised

4. **Use environment variables for CI/CD**
   - Store tokens as secrets in your CI/CD system
   - Never hardcode in scripts or config files

---

## Troubleshooting

### Issue: "Docker not found"
**Solution:** Install Docker Desktop or Docker Engine

### Issue: "Image mcp/sonarqube not found"
**Solution:** The Docker image should be pulled automatically. If not:
```bash
docker pull mcp/sonarqube
```

### Issue: "Still getting 'Not authorized' after updating"
**Checklist:**
- ✅ Token is correct (no extra spaces, copied completely)
- ✅ Organization name is correct (case-sensitive)
- ✅ Cursor was restarted after config change
- ✅ Token hasn't expired or been revoked
- ✅ Token has proper permissions for your organization

### Issue: "Connection works but can't find project"
**Possible causes:**
- Project key might be different (check in SonarCloud project settings)
- Organization name might be incorrect
- Token might not have access to that project

**Solution:** Verify project key in SonarCloud:
1. Go to your project in SonarCloud
2. Project Settings → General
3. Check the "Project Key" field

---

## Quick Reference

| Item | Location | Example |
|------|----------|---------|
| **Token** | SonarCloud → My Account → Security → Generate Tokens | `squ_abc123...` |
| **Organization** | URL: `https://sonarcloud.io/organizations/ORG_NAME` | `BrunoO` |
| **Project Key** | Project Settings → General → Project Key | `BrunoO_USN_WINDOWS` |
| **Config File** | `~/.cursor/mcp.json` | `/Users/brunoorsier/.cursor/mcp.json` |

---

## Next Steps

After configuring:
1. ✅ Test connection with `list_enterprises`
2. ✅ Verify project access with `search_my_sonarqube_projects`
3. ✅ Try analyzing a file: `analyze AppBootstrap.cpp with sonar cloud`

---

## Need Help?

- **SonarCloud Documentation:** https://docs.sonarcloud.io
- **MCP Documentation:** Check Cursor MCP documentation
- **Token Management:** https://sonarcloud.io/account/security


