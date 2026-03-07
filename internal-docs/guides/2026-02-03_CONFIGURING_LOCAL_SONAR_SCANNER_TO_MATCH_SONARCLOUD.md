# Configuring the Local Sonar Scanner to Match SonarCloud

**Date:** 2026-02-03  
**Purpose:** Align local SonarScanner runs with SonarCloud so that the same configuration is used and issues are consistent (no surprise differences between local and cloud).

---

## 1. Why Local and SonarCloud Can Show Different Issues

Even when both use the same project key and organization, local and SonarCloud can report different issues because of:

| Cause | What happens | How to align |
|-------|----------------|---------------|
| **Different analysis parameters** | Local script passes `-Dsonar.sources`, `-Dsonar.exclusions`, etc. on the command line; SonarCloud project may have different values set in the UI (Project Settings → Analysis Scope). | Use a **single source of truth**: put all analysis parameters in `sonar-project.properties` and use that file for every run (local and CI). |
| **Different compile database** | Local uses `compile_commands.json` from your machine (e.g. macOS); CI or another run might use one from Linux/Windows with different paths or files. | Generate and use the same layout when possible (e.g. build from repo root, use `build/compile_commands.json`). Document that “matching CI” may require the same OS or a CI-generated artifact. |
| **Different branch** | SonarCloud can have branch-specific quality profiles or settings. If you run locally without passing the branch, you may be analyzing a different branch than CI. | Pass `sonar.branch.name` when running locally if CI does (e.g. `main`), or use the same branch name. |
| **Quality profile** | The set of rules (e.g. which cpp:S* rules are active) is defined **on the server** (SonarCloud) per project, not in the scanner. | You cannot set the quality profile from the scanner. Ensure the project’s **Quality profile** in SonarCloud (Project → Project Analysis → Quality profile) is the one you want; local and CI both use it. |
| **Scanner / analyzer version** | Different SonarScanner or C++ analyzer versions can yield different results. | Use the same SonarScanner version locally as in CI when possible; SonarCloud uses its own analyzer version. |

So: **same project key + same analysis parameters + same scope and compile database** give the closest match. The quality profile is fixed by the SonarCloud project settings.

---

## 2. Single Source of Truth: `sonar-project.properties`

To avoid drift between local and SonarCloud:

1. **Put all analysis parameters in `sonar-project.properties`** in the repo (sources, exclusions, inclusions, encoding, C/C++ cache, etc.).
2. **Run the scanner from the project root** so it loads that file.
3. **Do not override** those parameters in the script or CI unless necessary (e.g. token, project key, organization, host URL). If the script passes `-Dsonar.exclusions=...`, it overrides the file and can desync from SonarCloud if SonarCloud uses the file or different UI settings.

This repo does the following:

- **`sonar-project.properties`** defines:
  - `sonar.sources`
  - `sonar.exclusions` (and optional `sonar.inclusions` if needed)
  - `sonar.sourceEncoding`
  - `sonar.cfamily.cache.enabled`, `sonar.cfamily.cache.path`
  - `sonar.python.version` (for any Python under analysis)
- **`scripts/run_sonar_scanner.sh`** passes only:
  - Connection/auth: `sonar.host.url`, `sonar.organization`, `sonar.projectKey`, `sonar.token`
  - C++ input: `sonar.cfamily.compile-commands` or `sonar.cfamily.build-wrapper-output` (discovered from the build)
  - Optional overrides only when no compile database is found (to disable C++ and avoid errors)

That way, **every run (local or CI) that uses this repo’s `sonar-project.properties`** gets the same scope and C++/Python settings. SonarCloud will use the same parameters when the analysis is run via the scanner (CLI or CI).

---

## 3. Quality Profile (Rules) Are Server-Side

- The **quality profile** (which rules are active, e.g. cpp:S125, cpp:S3229) is configured in **SonarCloud** for the project, not in the scanner.
- To change which rules run: SonarCloud → Your project → **Project Analysis** → **Quality profile** → choose the profile for C/C++ (e.g. “Sonar way” or a custom one).
- Local scanner and CI do **not** send a profile; they use whatever profile is attached to the project (and branch, if applicable). So local and cloud use the same rules as long as they use the same project (and branch).

---

## 4. Matching CI Exactly (When You Have CI Running Sonar)

If you have a CI job that runs SonarScanner (e.g. GitHub Actions):

1. **Same parameters:** Have CI use the same `sonar-project.properties` (no extra `-D` that override scope/exclusions). If CI currently passes parameters in the workflow, move those into `sonar-project.properties` and remove them from the workflow.
2. **Same branch:** In CI, `sonar.branch.name` is often set from the git branch. For local runs that should match `main`, run for example:  
   `sonar-scanner -Dsonar.branch.name=main`  
   (or add `sonar.branch.name=main` to `sonar-project.properties` for that use case).
3. **Same compile database:** If CI builds and then runs the scanner, it uses that build’s `compile_commands.json`. To match, use a similar build (e.g. same CMake config and build directory layout) and the same path (e.g. `build/compile_commands.json`).

---

## 5. Checklist for “Local Same as SonarCloud”

- [ ] All analysis scope and C++/Python settings are in `sonar-project.properties`; script/CI do not override them.
- [ ] You run the scanner from the project root so `sonar-project.properties` is loaded.
- [ ] You use the same `sonar.projectKey` and `sonar.organization` as on SonarCloud.
- [ ] If you care about branch: pass `sonar.branch.name` (or set it in the properties file).
- [ ] You use a `compile_commands.json` (or build-wrapper output) produced by a comparable build; path is consistent (e.g. `build/compile_commands.json`).
- [ ] On SonarCloud, the project’s **Quality profile** for C/C++ is the one you expect (no extra/custom profile elsewhere that would explain different rules).

---

## 6. Automatic Analysis vs Manual Scanner

If you see:

```text
You are running manual analysis while Automatic Analysis is enabled.
Please consider disabling one or the other.
```

then the project (or organization) has **Automatic Analysis** enabled. SonarCloud then runs analysis automatically (e.g. on push) and blocks runs of the SonarScanner CLI.

To run the local scanner and compare with SonarCloud:

- **Option A:** Disable Automatic Analysis for the project (or organization), then run `scripts/run_sonar_scanner.sh` locally. After the run, re-enable Automatic Analysis if you want it back for CI/push.
- **Option B:** Keep Automatic Analysis on and do not run the scanner manually; use SonarCloud’s automatic runs and fetch open issues via the API or Sonar MCP to verify counts (e.g. 10 open issues).

Disabling Automatic Analysis (if available on your plan): SonarCloud → **Administration** → **Organization Settings** → **Analysis** → **Analysis method** → turn off **Enabled for new projects**, or override at project level if your plan allows.

---

## 7. References

- SonarCloud analysis parameters: [Analysis parameters](https://docs.sonarsource.com/sonarcloud/advanced-setup/analysis-parameters/)
- SonarCloud C/C++: [Running the analysis](https://docs.sonarsource.com/sonarcloud/advanced-setup/languages/c-family/running-the-analysis), [Customizing the analysis](https://docs.sonarsource.com/sonarcloud/advanced-setup/languages/c-family/customizing-the-analysis)
- Quality profile (server-side): SonarCloud → Project → **Project Analysis** → **Quality profile**
- This project: `sonar-project.properties`, `scripts/run_sonar_scanner.sh`
- Automatic vs manual: see §6 above
