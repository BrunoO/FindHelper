# 2026-02-02 — Open-source publish workflow (private history, no internal-docs)

**Goal:** Publish the repo as open-source with **current state only** (no historical git commits) and **no internal-docs**, while keeping your full history and internal material in a private repo.

---

## Principle

- **Private repo (this one):** Full git history, `internal-docs/`, everything. You develop here.
- **Public repo:** Fresh history (e.g. one initial commit or a short linear history), no `internal-docs/`, only what contributors need. You push to it when you want to “release” or sync.

---

## One-time setup: create the public repo with no history

Do this once when you first publish.

### 1. Create the public remote

- Create a new **empty** repo on GitHub/GitLab (e.g. `yourname/FindHelper`). Do not add a README or .gitignore.

### 2. Create an orphan “public” branch (no parent = no history)

In your **private** repo:

```bash
# Create a branch with no history, using current tree
git checkout --orphan public

# Remove internal-docs from the index (so it won’t be in the first commit)
git rm -rf --cached internal-docs 2>/dev/null || true

# Optional: add internal-docs to .gitignore on this branch so it never gets added
echo "internal-docs/" >> .gitignore
git add .gitignore

# One commit with “current state” only
git add -A
git commit -m "Initial open-source release"

# Add the public remote (replace with your public repo URL)
git remote add public https://github.com/yourname/FindHelper.git

# Push; public repo now has one commit, no history, no internal-docs
git push -u public public:main
```

- If your public host uses `master` as default branch, use `public:master` instead of `public:main`, or change the default branch in the host’s settings.

### 3. Go back to normal development

```bash
git checkout main
```

- Your `main` (or `master`) keeps full history and `internal-docs/`. The `public` branch is a “snapshot” branch used only for pushing to the public remote.

---

## Ongoing workflow: sync public repo when you want to publish

Whenever you want the public repo to match your current state (still without history and without internal-docs):

```bash
# 1. Make sure main is up to date (you’ve committed everything you want to publish)
git checkout main

# 2. Update the public branch to match main’s tree, minus internal-docs
git checkout public
git reset --soft main

# 3. Remove internal-docs from the index only (so it won’t be in the commit)
git rm -rf --cached internal-docs 2>/dev/null || true

# 4. Commit the new “current state” (index = main’s tree minus internal-docs)
git commit -m "Sync with upstream"

# 5. Push to public remote
git push public public:main

# 6. Back to main for daily work
git checkout main
```

- **Result:** Public repo gets a linear history of “sync” commits (e.g. “Initial open-source release”, then “Sync with upstream”, etc.). It still has **no** old private history and **no** `internal-docs/`.

---

## Alternative: public = always one commit (squash every time)

If you prefer the public repo to have **only one commit** that always reflects latest state:

```bash
git checkout public
git reset --hard main
git rm -rf --cached internal-docs 2>/dev/null || true
rm -rf internal-docs
git add -A
git commit --amend -m "Initial open-source release"
git push public public:main --force
git checkout main
```

- Use `--force` only on the public branch; never force-push `main`. This rewrites the single public commit each time.

---

## Checklist

- [ ] Public remote added (`git remote add public <url>`).
- [ ] `public` branch created from orphan, first commit has no `internal-docs/`.
- [ ] Pushed once: `git push -u public public:main`.
- [ ] When syncing: unstage/remove `internal-docs/` before committing on `public`.
- [ ] Optional: `.gitignore` on `public` contains `internal-docs/` so it never gets added by mistake.

---

## Summary

| Where       | History              | internal-docs/ |
|------------|----------------------|----------------|
| **Private** (main) | Full git history     | Yes            |
| **Public** (public branch → public remote) | None or short “sync” history | No             |

You develop on `main`; when you want to publish, update the `public` branch (current tree minus `internal-docs/`), then push to the public remote.
