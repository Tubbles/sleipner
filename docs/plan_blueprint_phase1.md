# Blueprint Phase 1 Implementation Plan

**Goal**: Flatten Blueprint's dedicated typed fields into its `AttrSet`. Blueprint struct shrinks to:
```c
typedef struct {
    AttrSet attrs;
    vec_blueprint_child children;
    vec_rule rules;
} Blueprint;
```

---

## New attr key names

| Removed field       | Attr key(s)                                              |
|---------------------|----------------------------------------------------------|
| `name`              | `"name"` (string)                                        |
| `texture_name`      | `"texture"` (string)                                     |
| `extends_name`      | `"extends"` (string, parse-time only — removed after inheritance resolution) |
| `source`            | `"src_x"`, `"src_y"`, `"src_w"`, `"src_h"` (float)     |
| `collision_offset`  | `"collision_offset_x"`, `"collision_offset_y"` (float)  |
| `collision_size`    | `"collision_w"`, `"collision_h"` (float)                 |

The TOML input format is **unchanged** (`src = [x,y,w,h]`, `collision_offset = [x,y]`, etc.) — only internal storage changes.

---

## Changes by file

### `attribute.h` / `attribute.c`
Add `attr_remove(alloc, set, name)`: swap-and-decrement removal. Needed to strip the
`"extends"` attr from each blueprint after inheritance resolution.

### `blueprint.h`
- Remove the 6 dedicated fields from Blueprint.
- Add three small geometry helper functions to avoid repeating `attr_get_float` at every consumer:
```c
Rectangle blueprint_get_source(const Blueprint *bp);
Vector2   blueprint_get_collision_offset(const Blueprint *bp);
Vector2   blueprint_get_collision_size(const Blueprint *bp);
```

### `blueprint.c` (biggest change)
- **`is_known_key`**: remove `"texture"` and `"extends"` — they are plain string values so
  `parse_custom_attrs` already handles them correctly. Array keys (`"src"`,
  `"collision_offset"`, `"collision_size"`) stay.
- **`parse_single_blueprint`**: parse `"name"` explicitly and store via `attr_set_string`.
  Remove call to `parse_optional_strings` (texture/extends now fall through to
  `parse_custom_attrs`).
- **`parse_geometry`**: instead of setting `blueprint->source = ...`, call `attr_set_float`
  ×4 for src, ×2 for offset, ×2 for size.
- **Delete** `parse_optional_strings` and `inherit_rendering_fields` entirely.
- **`inherit_from_parent`**: use `attr_get_string(&child->attrs, "extends")` to find parent.
  The generic `inherit_attributes` already copies all missing attrs, so it covers
  texture/source/collision inheritance for free — no separate rendering-field logic needed.
- **After `resolve_inheritance`**: sweep blueprints calling `attr_remove` on `"extends"`.
- **`blueprint_find`**: use `attr_get_string(&bp->attrs, "name")` instead of
  `strv_eq_cstr(bp->name, ...)`.
- **`blueprint_cleanup`**: drop the three `str_free` calls for removed fields.

### `level.c`
- `blueprint->texture_name.ptr` → `attr_get_string(&blueprint->attrs, "texture")`
- `blueprint->source`, `blueprint->collision_offset`, `blueprint->collision_size` →
  geometry helpers
- Blueprint name in error strings → `attr_get_string(&blueprint->attrs, "name")`

### `main.c` / `toml_emitter.c`
Same texture/geometry field → attr lookup substitutions.

### `test_helpers.c`
Drop `str_free` calls for removed Blueprint fields.

### `test_blueprint.c`
Update assertions:
- `.texture_name.ptr` → `attr_get_string(&bp->attrs, "texture")`
- `.collision_size.x` → `attr_get_float(&bp->attrs, "collision_w", 0)`
- etc.

### `test_entity.c`
- `spec_from_blueprint` helper reads collision/source from attrs instead of direct fields.
- Direct `blueprint.collision_offset = ...` assignments become `attr_set_float` calls.

### `test_toml_emitter.c`
Same field-access updates as above.

---

## Key simplification

Removing `inherit_rendering_fields` is the biggest win. Once texture/source/collision live
in `AttrSet`, the existing generic `inherit_attributes` — which already copies all missing
attrs from parent to child — handles inheritance of those fields automatically. Two functions
collapse into one.
