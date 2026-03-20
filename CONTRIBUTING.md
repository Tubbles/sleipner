# Development Workflow

**IMPORTANT:** This document is specifically for Mistral LLM (devstral-2). Anthropic LLMs do not need to read or follow CONTRIBUTING.md.

## Feature Branches

All changes must use feature branches. Create a new branch for each feature or bug fix.

## Code Review Process

1. **Create branch** from main
2. **Implement changes** with focused commits
3. **Spin up subagent** for code review:
   - Provide original prompts as requirements
   - Subagent verifies exact match to requirements
   - Subagent checks code quality and style
   - Subagent validates test coverage
   - Subagent ensures no regressions
4. **Merge** after approval using rebase and fast-forward
