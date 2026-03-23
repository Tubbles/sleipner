# Sleipner TODO

## Engineering Goals

- **One arena** (`gamedata_arena`) — no scratch arena, no second arena
- **No libc heap** in engine code (except `arena.c` bootstrap and TOML vendor `free()`)
- **No naked `char *` / `char[]` strings** — `Str` for owned strings, `Strv` for non-owning views
- **Vec types for all linear data** — no hand-rolled pointer+count arrays

No static/global data — already done.

---

## No libc heap — remaining sites

All allocations are already routable through `Allocator *`. These are the remaining
raw `malloc`/`realloc`/`free` calls that still bypass it.

- [x] **`particle.c`** — `malloc`/`realloc` for pool growth → convert `ParticlePool` to
  use `vec_particle` backed by gamedata_arena (thread `Allocator *` through
  `particles_init` / `particles_free`)

- [x] **`audio.c`** — `malloc` for wave synthesis sample buffers → stack-local fixed arrays
  (sample counts are compile-time bounded, well within stack budget)

- [x] **`input.c`** — `malloc` for gamepad mappings copy → arena alloc (one-time init,
  tiny, can stay in arena since it's not reclaimed between reloads)

- [x] **`main.c`** / **`game.c`** — `malloc` for file read buffer and TOML copy → arena
  alloc with `arena_save` / `arena_restore` so the temporary buffer is reclaimed
  immediately after the parse step completes

- [x] **`rule.c`** — `vec_trigger_event` with `NULL` alloc in `rules_evaluate_batch` →
  arena alloc with save/restore so per-batch event queue memory is reclaimed after
  each call (flags/attrs set during the batch are written before the restore point)

---

## No naked char[]/char* strings — remaining sites

All persistent string fields in blueprint/entity/level/attribute/flags were already
migrated to `Str`. These are the remaining `char[MAX_ARG]` fixed buffers in rule
structs, all of which are parse-time data stored in the arena-backed rule set.

- [x] **`Trigger.argument`** — `char[MAX_ARG]` → `Str`
- [x] **`Condition.argument`** — `char[MAX_ARG]` → `Str`
- [x] **`TriggerEvent.argument`** — `char[MAX_ARG]` → `Str`
- [x] **`ActionNode.argument`** / **`ActionNode.second_argument`** — `char[MAX_ARG]` → `Str`

---

## Promote vec types — remaining hand-rolled arrays

- [x] **`Level.entities`** — `Entity entities[MAX_LEVEL_ENTITIES]; int entity_count;`
  → `vec_entity entities` with gamedata_arena alloc

- [x] **`ActionTree.nodes`** — raw `ActionNode *nodes; int count;` → `vec_action_node nodes`

- [x] **`Rule.conditions`** — `Condition conditions[MAX_CONDITIONS]; int condition_count;`
  → `vec_condition conditions`

- **`RuleSet`** (in `blueprint.h`) — raw `Rule *entries; int count;` → `vec_rule entries`
  — **Blocked**: circular dependency (blueprint.h defines RuleSet using a forward-declared
  `struct Rule *`, and including rule.h from blueprint.h creates a cycle via
  entity.h → blueprint.h). Unblocked after "Entity/Blueprint decoupling" Phase 2 below.

- **`ActionNode.children`** / **`ActionNode.else_children`** — raw `ActionNode *` +
  count fields → `vec_action_node`
  — **Blocked**: self-referential struct constraint. `VEC_DECL(action_node, ActionNode)`
  requires `ActionNode` to be complete, but `ActionNode` can't embed `vec_action_node`
  before the vec type is declared.

---

## Entity/Blueprint decoupling

**Goal:** Entity holds `const AttrSet *defaults` (pointer to blueprint's AttrSet) instead of
`const Blueprint *blueprint`. This severs the entity.h → blueprint.h → rule.h → entity.h
circular dependency and also unlocks `RuleSet → vec_rule` above. All blueprint properties
(name, texture, collision geometry, etc.) live in a single flat AttrSet populated at parse
time. Any attribute is mutable at runtime — no parse-time-only distinction.

Blueprint retains `rules` and `children` as structural fields (behavioral configuration, not
key-value attribute data).

### Phase 1: Flatten Blueprint's dedicated fields into its AttrSet

Blueprint currently has both dedicated typed fields (`name`, `texture_name`, `source`,
`collision_offset`, `collision_size`) and a separate `AttrSet attrs`. Merge everything into
one flat AttrSet using these key names:

- `"name"` (string), `"texture"` (string)
- `"src_x"`, `"src_y"`, `"src_w"`, `"src_h"` (float)
- `"collision_offset_x"`, `"collision_offset_y"` (float)
- `"collision_w"`, `"collision_h"` (float)
- All existing custom attrs (already in AttrSet — no change needed)

`extends_name` is discarded after inheritance resolution — the only truly parse-time-only
field; it has no runtime meaning and is never looked up after load.

Blueprint struct after: `AttrSet attrs`, `RuleSet rules`, `vec_blueprint_child children`.

Affected: blueprint parsing (`parse_optional_strings`, `parse_geometry` fold into attr sets),
`blueprint_find` (searches by `attr_get_string(&bp->attrs, "name")`), inheritance resolution
(`inherit_rendering_fields` and `inherit_attributes` merge into one `inherit_attrs` pass),
all consumers of dedicated Blueprint fields in level.c, game.c, main.c, rule.c, toml_emitter.c.

### Phase 2: Replace Entity's Blueprint pointer with AttrSet pointer

- `Entity`: `const Blueprint *blueprint` → `const AttrSet *defaults`
- `entity_init_from_blueprint` signature: `const Blueprint *` →
  `(const AttrSet *defaults, Texture2D *texture)`. Caller (level.c) resolves texture from
  `attr_get_string(blueprint->attrs, "texture")` before calling.
- `entity_get_attr`: already two-level; update to dereference `entity->defaults`.
- `entity.h`: remove `#include "blueprint.h"` — depends only on `attribute.h` (already present).

Side effect: `RuleSet → vec_rule` is now unblocked (cycle broken).

### Phase 3: Update rule evaluation to not use entity->blueprint

`evaluate_entity_rules` currently accesses `entity->blueprint->rules`. After Phase 2 this
pointer is gone.

- `rules_evaluate_batch` gains a `const BlueprintTable *blueprints` parameter.
- Inside `evaluate_entity_rules`, look up rules via
  `blueprint_find(blueprints, entity->blueprint_name.ptr)`.

### Phase 4: Migrate Entity's dedicated typed fields into AttrSet

Remove typed fields from Entity; read them through the attr system:

- Remove: `int health`, `int max_health`, `bool visible`, `bool active`, `bool solid`,
  `float opacity`
- Remove: `Rectangle source` — decompose into `"src_x/y/w/h"` float attrs (same keys as
  blueprint, so defaults fallthrough works for free)
- Keep: `Vector2 position` — updated every frame by movement logic, not a logical attribute
- Keep: `Texture2D *texture` — resolved runtime handle; re-resolved when `"texture"` attr
  changes (e.g. via `change_sprite` action)
- Keep: `Rectangle collision` — derived cache, recomputed by `entity_update_collision` from
  position + collision attrs

`entity_update_collision` reads collision offset/size from attrs instead of blueprint fields.
Render code reads source rect from attrs. `change_sprite` sets `"texture"` attr (string name);
the render path resolves `Texture2D *` from it each frame.

`entity_get_int`, `entity_get_float`, etc. already go through `entity_get_attr` — most
callsites need no changes.

---

Phase order: 1 → 2 → 3. Phase 4 depends on Phase 2 and can be done independently of Phase 3.
