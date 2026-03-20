# Development Workflow

## Feature Branches

All changes must use feature branches:
```bash
git checkout -b feature/<name>
```

## Code Review Process

1. **Create branch** from main
2. **Implement changes** with focused commits
3. **Push for review**: `git push origin feature/<name>`
4. **Subagent reviews** against:
   - ✅ Exact match to original requirements
   - ✅ Code quality and style
   - ✅ Test coverage
   - ✅ No regressions
5. **Merge** after approval:
```bash
git checkout main
git pull
git checkout feature/<name>
git rebase main
git checkout main
git merge --ff-only feature/<name>
git branch -d feature/<name>
```

## Requirements

- No forward declarations (use proper includes)
- Minimal static variables
- Pure functions where possible
- Tests for all new functionality
- CI must pass (format, build, test, lint)

## Review Checklist

**Subagent verifies:**
- Changes match exact original requirements
- Code follows existing patterns
- Tests added/updated
- Documentation updated
- No compilation warnings
- CI passes completely

## Example

```bash
# Start feature
git checkout -b feature/control-flow
# ... make changes ...
git push origin feature/control-flow
# After approval
git checkout main
git pull
git merge --ff-only feature/control-flow
```