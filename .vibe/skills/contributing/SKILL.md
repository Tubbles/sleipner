---
name: contributing
description: Best practices when contributing to the project and editing files.
license: MIT
---

# Contributing Workflow

## Feature Branches

All changes must use feature branches. Create a new branch for each feature or bug fix.

## Code Review Process

1. **Create branch** from main
2. **Implement changes** with focused commits
3. **Spin up subagent** for code review:
   - Make sure the subagent is an expert code reviewer in the fields relevant to the project
   - Provide original prompts as requirements along with any other necessary information
   - Seed the subagents prompt with ~/.vibe/prompts/my-custom-prompt.md
   - Subagent looks at the whole feature branch
   - Subagent verifies exact match to requirements
   - Subagent checks code quality and style
   - Subagent validates test coverage
4. **Merge** after approval using rebase and fast-forward
