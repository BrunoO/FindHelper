---
description: Project repos (public FindHelper vs private USN_WINDOWS) and publish flow via scripts/publish.sh
globs: "**/*.sh,**/.github/**/*.yml,**/.github/**/*.yaml,**/Makefile,**/Justfile,**/scripts/publish.sh"
alwaysApply: true
---

# GitHub: Public vs Private Repos (This Project)

## Repo mapping

| Repo | Visibility | Purpose |
|------|-------------|---------|
| **BrunoO/FindHelper** | Public | Published app; what users and contributors see. |
| **BrunoO/USN_WINDOWS** | Private | Main development repo (this workspace). |

**Publish flow:** We publish from the **private** repo (USN_WINDOWS) to the **public** repo (FindHelper) using **`scripts/publish.sh`**.

**Don't:** Push `main` or any internal branch directly to the `findhelper` remote; use `scripts/publish.sh` instead.

## Publish script: `scripts/publish.sh`

- **Usage:** `scripts/publish.sh` (snapshot to `findhelper` remote) or `scripts/publish.sh v1.2.0` (snapshot + versioned release).
- **Remotes:** Private repo uses `origin` (BrunoO/USN_WINDOWS). Public repo is the `findhelper` remote (BrunoO/FindHelper).
- **Branch:** Script uses orphan branch `findhelper-public`, overlays `main`, strips internal-only paths (e.g. `internal-docs/`), then force-pushes to `findhelper`.

When suggesting `gh` or git commands that touch either repo, distinguish:

- **Private (USN_WINDOWS, `origin`):** PRs, issues, clones with auth — this is the day-to-day repo.
- **Public (FindHelper, `findhelper`):** Only updated via `scripts/publish.sh`; no direct push of internal state.

## gh CLI: visibility checks (for scripts/workflows)

```bash
# Current repo visibility (here: private)
gh repo view --json visibility -q .visibility

# List only public / only private (owner BrunoO)
gh repo list BrunoO --visibility public
gh repo list BrunoO --visibility private
```

When writing scripts that use `gh`, use `--visibility public` or `--visibility private` as needed so behavior is correct for BrunoO/FindHelper vs BrunoO/USN_WINDOWS.
