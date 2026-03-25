# Sleipner TODO

## Engineering Goals

- **No opaque cross-module forward declarations** — `struct EngineContext;` is
  the last one; eliminate it by splitting the logging/error concern into a
  lightweight header with no upward dependencies (see Coding Style in
  CLAUDE.md). Also investigate cppcheck for a built-in check; if none exists,
  write a small cppcheck Python plugin (lives in this repo) to enforce it
  automatically.
- **Vec types for all linear data** — `ActionNode.children` /
  `ActionNode.else_children` still use raw pointers (blocked, see below)

---

## Promote vec types — remaining

- **`ActionNode.children`** / **`ActionNode.else_children`** — raw
  `ActionNode *` + count fields → `vec_action_node` — **Blocked**:
  self-referential struct constraint. `VEC_DECL(action_node, ActionNode)`
  requires `ActionNode` to be complete, but `ActionNode` can't embed
  `vec_action_node` before the vec type is declared.

## misc

- remove allocator fallback of NULL -> libc heap from all our data types
- change all NULL -> nullptr since this is C23
