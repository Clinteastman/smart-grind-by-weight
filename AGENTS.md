# Agent working environment

The canonical development checkout for this repository is:

`/home/cmossom/src/smart-grind-by-weight`

It lives on the native WSL2 ext4 filesystem in the `Ubuntu-24.04` distribution.
Do not develop or build this project from a Windows checkout, OneDrive, `/mnt/c`,
or another Windows-mounted path. If a task starts from Windows, run repository
commands through WSL2 and change to the canonical path first.

From PowerShell, use:

```powershell
wsl.exe -d Ubuntu-24.04 --cd /home/cmossom/src/smart-grind-by-weight
```

Firmware commands must use the project virtual environment:

```bash
tools/venv/bin/python3 tools/grinder.py build --hardware v1 --jobs 8
tools/venv/bin/python3 tools/grinder.py build --hardware v2 --jobs 8
```

The V1 and V2 PlatformIO caches are deliberately separate. Do not override them
with one shared cache. The Windows desktop simulator is the exception: it uses
Windows build tools, but its source of truth remains this WSL checkout.

Before changing code, read `CLAUDE.md` and the relevant complete source files.
After changes, run the appropriate simulator tests, both firmware builds when
shared code changed, `git diff --check`, and update user-facing documentation.

## GitHub write verification

When posting or editing pull request descriptions, issue comments, review
comments, or release notes:

- Do not pass multiline text through nested PowerShell, WSL, and Bash `$'...'`
  quoting. The outer shell can reduce the body to `$` or remove substitutions.
- Prefer structured GitHub connector or API fields for the body text.
- Immediately read the published object back from GitHub and compare its visible
  body with the intended text. An API success response is not publication proof.
- Repair malformed text before continuing with the wider task.

## Completion protocol

Before ending a coding session or reporting that a task is finished:

1. Re-read the user's current request and the active task plan.
2. Reconcile every agreed item as completed, deliberately deferred with a
   reason, or blocked by a specific external dependency.
3. Check every relevant worktree and branch for uncommitted or unpushed work,
   and check any open PRs or CI runs that are part of the task.
4. Run the required formatting, tests, V1/V2 builds, documentation checks, and
   hardware or OTA validation appropriate to the changed scope.
5. Report the complete outcome and list anything that remains. Never treat an
   answered side question, an interruption, a successful compile, or one
   completed subtask as completion of the wider task.

If work is interrupted by a user question, answer it and then resume the active
plan unless the user explicitly replaces or cancels that plan.
