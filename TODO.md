# Sleipner TODO

## Engineering Goals

- **One arena** (`gamedata_arena`) — no scratch arena, no second arena
- **No opaque cross-module forward declarations** — `struct EngineContext;` is the last one; eliminate it by splitting the logging/error concern into a lightweight header with no upward dependencies (see Coding Style in CLAUDE.md). Also investigate cppcheck for a built-in check; if none exists, write a small cppcheck Python plugin (lives in this repo) to enforce it automatically.
- **Vec types for all linear data** — `ActionNode.children` / `ActionNode.else_children` still use raw pointers (blocked, see below)

---

## Promote vec types — remaining

- **`ActionNode.children`** / **`ActionNode.else_children`** — raw `ActionNode *` +
  count fields → `vec_action_node`
  — **Blocked**: self-referential struct constraint. `VEC_DECL(action_node, ActionNode)`
  requires `ActionNode` to be complete, but `ActionNode` can't embed `vec_action_node`
  before the vec type is declared.

---

## Entity/Blueprint decoupling — remaining phases

Phases 2 and 3 are done (entity holds `const AttrSet *defaults` + `int id`; rule
evaluation uses `map_entity_ruleset` keyed on entity ID). Remaining:

### Phase 1: Flatten Blueprint's dedicated fields into its AttrSet

Blueprint currently has both dedicated typed fields (`name`, `texture_name`, `source`,
`collision_offset`, `collision_size`) and a separate `AttrSet attrs`. Merge everything into
one flat AttrSet using these key names:

- `"name"` (string), `"texture"` (string)
- `"src_x"`, `"src_y"`, `"src_w"`, `"src_h"` (float)
- `"collision_offset_x"`, `"collision_offset_y"` (float)
- `"collision_w"`, `"collision_h"` (float)
- All existing custom attrs (already in AttrSet — no change needed)

`extends_name` is discarded after inheritance resolution — it has no runtime meaning and
is never looked up after load.

Blueprint struct after: `AttrSet attrs`, `vec_rule rules`, `vec_blueprint_child children`.

Affected: blueprint parsing (`parse_optional_strings`, `parse_geometry` fold into attr
sets), `blueprint_find` (searches by `attr_get_string(&bp->attrs, "name")`), inheritance
resolution (`inherit_rendering_fields` and `inherit_attributes` merge into one
`inherit_attrs` pass), all consumers of dedicated Blueprint fields in level.c, game.c,
main.c, rule.c, toml_emitter.c.

### Phase 4: Migrate Entity's dedicated typed fields into AttrSet

Depends on Phase 1 (so blueprint defaults carry the same keys as entity instance attrs,
and the two-level fallthrough in `entity_get_attr` works for free).

Remove typed fields from Entity; read them through the attr system:

- Remove: `int health`, `int max_health`, `bool visible`, `bool active`, `bool solid`,
  `float opacity`
- Remove: `Rectangle source` — decompose into `"src_x/y/w/h"` float attrs
- Keep: `Vector2 position` — updated every frame by movement logic, not a logical attribute
- Keep: `Texture2D *texture` — resolved runtime handle; re-resolved when `"texture"` attr
  changes (e.g. via `change_sprite` action)
- Keep: `Rectangle collision` — derived cache, recomputed by `entity_update_collision`

`entity_get_int`, `entity_get_float`, etc. already go through `entity_get_attr` — most
callsites need no changes.
