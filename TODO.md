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
  entity.h → blueprint.h).

- **`ActionNode.children`** / **`ActionNode.else_children`** — raw `ActionNode *` +
  count fields → `vec_action_node`
  — **Blocked**: self-referential struct constraint. `VEC_DECL(action_node, ActionNode)`
  requires `ActionNode` to be complete, but `ActionNode` can't embed `vec_action_node`
  before the vec type is declared.
