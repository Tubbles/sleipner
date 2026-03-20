# Development Workflow

## Feature Branches

All changes must use feature branches. Create a new branch for each feature or bug fix.

## Code Review Process

1. **Create branch** from main
2. **Implement changes** with focused commits
3. **Push for review**
4. **Subagent reviews** against:
   - ✅ Exact match to original requirements
   - ✅ Code quality and style
   - ✅ Test coverage
   - ✅ No regressions
5. **Merge** after approval using rebase and fast-forward

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

## Example Workflow

1. Create feature branch from main
2. Implement changes with incremental commits
3. Push branch for review
4. Address review feedback
5. After approval: rebase on main and fast-forward merge