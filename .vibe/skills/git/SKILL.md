---
name: git
description: Working with git best practice
license: MIT
---

# Git workflow skill

## General

- **Always commit and push when you're done with a task.** Do not wait to be asked — committing and pushing is part of completing the work. This applies to all changes, including documentation updates, unless explicitly instructed otherwise. Every type of finished "increment" is worthy of its own commit.
- Never rewrite history, fixes and changes shall go into new commits.
- Create small, focused commits so changes are easy to review and revert.
- Each commit should address a single concern (one bug fix, one feature, one refactor).
- Use a succinct imperative commit title (e.g. "Add retry logic for API calls") with max 72 characters. The lines of the body shall be also max 72 characters.
- Include gotchas, caveats, or non-obvious side effects in the commit message body.
- Never add "Co-Authored-By" lines or email addresses to commit messages.
- **Keep all documentation up to date.** When changing behavior, update your .md files and code comments in the same commit. Stale docs are worse than no docs.
- **ALWAYS** format the codebase before doing a commit.
- **ALLOWED** git commands: add, commit, log, reflog, checkout, and so on. Only vanilla commands. Any "read only" commands that doesn't change the state or contents of the git repo are always allowed as well.
- **NEVER** do the following git commands: stash, revert, any force pushing, resetting to an older commit.
- If a git command is not in the allowed set of commands, ask for it and it might be added.

## Git Commit & Push Workflow

### Rules:
1. Every commit MUST be followed by an immediate push
2. No "I'll push later" - push is part of commit
3. If push fails, stop and report the error

### Steps:
1. Make code changes
2. Run necessary tests and checks
3. `git add <files>`
4. `git commit -m "<message>"`
5. **IMMEDIATELY** `git push origin <branch>`
6. Verify push succeeded

### Error Handling:
- If push fails: `git status`, check network, ask for help
- Never continue without resolving push failure
- Never assume "I'll push later"

### Exceptions:
- NONE. Even WIP commits get pushed.
