# Sleipner Refactoring Tasks

## Architecture Improvements

### Eliminate Static Variables
- [ ] Refactor font preview system to use explicit state instead of static variables
  - Create `FontPreview` struct to hold fonts and count
  - Pass struct explicitly to `draw_font_preview()` instead of using globals
  - Remove static `font_preview_entries` and `font_preview_count`

- [ ] Refactor texture registry to use explicit state
  - Create `TextureRegistry` struct to hold textures
  - Pass registry explicitly to functions that need texture lookup
  - Remove static `texture_registry` and `texture_registry_count`

- [ ] Audit all static variables and eliminate where possible
  - Search for `static` in codebase
  - Convert to explicit state passing pattern
  - Document any remaining static variables as necessary exceptions

### Improve Function Purity
- [ ] Identify functions that rely on static state
- [ ] Convert to pure functions by passing dependencies explicitly
- [ ] Add `// IMPURE:` comments for functions that must remain impure

## Code Quality

### Follow New Conventions
- [ ] Apply init function pattern to all modules with static state
- [ ] Ensure all init functions are called during game startup
- [ ] Document lifetime assumptions when taking pointers to data for caching, performance, or any other reason
=======
