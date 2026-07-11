# Sleipner TODO

## Architecture audit findings

- **Editor test coverage — Phase 4+5** — 176 unit tests across 6 files (draw 14,
  core 30, attr 26, widgets 64, child 20, blueprint 22). Still missing:
  integration tests for full editor workflows (select → edit → confirm), draw
  function testing.

## Type safety cleanup

- **Replace pre-resolved `entity_defaults[]` parallel array** — rule.c currently
  receives a `const AttrSet *const *entity_defaults` array (indexed by vec
  position) that game.c pre-builds before each rule evaluation. This couples
  rule contexts to vec indices and forces `resolve_target` callers to do pointer
  subtraction. Two approaches, no preference between them — pick whichever fits
  each case: **(a) Decompose at the boundary (option 4):** game.c builds an
  enriched view struct (e.g.
  `EntityView { Entity *entity; const AttrSet *defaults; }`) and passes an array
  of those to rule.c. Resolution happens once at the call boundary, rule.c just
  reads plain data. **(b) Callback / ops struct (option 1):** pass a resolver
  struct (function pointer + `void *` state) that rule.c calls through without
  knowing about GameState or blueprints. Better when resolution must happen
  lazily or the set of entities isn't known up front. Note: handles (option 3 —
  storing a lookup key on the object) only make sense when the association is
  intrinsic to the type. Entity→blueprint is not guaranteed (dynamic entities
  may have no blueprint), so a null handle would be a code smell. Prefer options
  1 or 4 here.
- **AttrSet value type rethink** — consider what AttrValue should support beyond
  float/int/bool/string: entity handles, blueprint handles? (D15:
  `attr_get_scoped_int` is now strict, returning the fallback on a float-typed
  attribute instead of truncating; `attr_get_scoped_float`'s int-to-float
  promotion is intentional and documented in `attribute.c`. Deferred per D15:
  per-key `debug_log` on mismatch, since the getter is a pure hot-path
  function with no `DebugState` to thread through.)

## Editor: missing top-level modes

Phase 6 of the keybinding audit (`work/keybinding-audit.md`) enumerated five
editor modes that DESIGN.md specifies but the engine did not yet implement.
Level mode (§30), Tile mode (§26), Atlas mode (§27), Animation mode (§28),
and Rule mode (§29) are now done — see the follow-up bullets below for the
gaps each one left open (Level mode's music picker, Tile mode's autotiling,
Atlas mode's fuzzy-finder integration, Rule mode's if_else/for_each
predicate rows and MOVE reparenting).

- **Tile mode autotiling** (DESIGN.md §26, D36) — S5.3b shipped manual
  concrete-tile-id painting (`EDITOR_SUB_TILE_PAINT`/`_PALETTE`,
  `editor/tile.c`): a cursor over the ground/overlay grid, CONFIRM paints
  the palette-picked tile id, EDITOR_DELETE erases. D36's "Autotiling
  (automatic edge/corner sprite selection) is an editor feature" line is
  still open — the file format already stores plain concrete tile ids, so
  autotiling would live entirely in the editor's placement logic with no
  further engine-side changes. Not attempted in S5.3b.
- **Atlas mode fuzzy-finder integration** (§27, D37) — S5.4a shipped the
  engine-side named-atlas-region system; S5.4b shipped the editor mode
  (`EDITOR_SUB_ATLAS_BROWSE`, `EDITOR_SUB_ATLAS_REGION_EDIT`,
  `editor/atlas.c`): browse textures from the runtime registry, list/create
  named regions per texture, drag-set a region's `src` via the same
  dual-stick math as HANDLES. Authoring `sprite = "name"` on a blueprint
  already works today through the generic string-attribute system (ADD ->
  word builder, no new code needed) but the fuzzy finder
  (`fuzzy_finder_build_items`, `editor/widgets.c`) doesn't yet suggest
  existing atlas region names as candidates when picking that value, so
  there's no autocomplete/browse-by-name for it — purely additive UX, not a
  round-trip gap.
- **Rule mode** (§29, §100-104) — S5.6a shipped the read-only foundation;
  S5.6b shipped leaf editing (`begin_rule_edit_for_row`/
  `dispatch_rule_radial_confirm`/`rule_edit_argument_step_complete`/
  `finalize_rule_edit`, `editor/rule.c`); S5.6c shipped structural editing:
  ACTION_EDITOR_PLACE inserts a new action node (appended into a
  control-flow node's own children when that's what's focused, otherwise
  spliced in right after the focused sibling) and immediately reuses
  S5.6b's ACTION_TYPE radial so the new node gets a real type/argument;
  ACTION_EDITOR_DELETE drops the focused node's index from its containing
  list (roots/children/else_children) without compacting the flat pool —
  the orphaned subtree just stops being emitted; ACTION_EDITOR_MOVE_UP/DOWN
  (new chords, `[Ctrl/L1, Up/Down]`) reorder a node among its siblings
  (`insert_rule_action_node`/`delete_rule_action_node`/
  `move_rule_action_node`, `editor/rule.c`); S5.6d shipped subroutines: the
  blueprint picker's trailing "Subroutines" row switches
  `EDITOR_SUB_RULE_LIST` to a list over `gamedata.subroutines` (name + action
  count, "+ NEW SUBROUTINE", `ACTION_EDITOR_DELETE`), and each subroutine's
  action tree reuses the exact same `EDITOR_SUB_RULE_TREE` editor as a rule's
  — `rule_tree_flatten`/`rule_tree_row_count` and every S5.6b/c leaf/
  structural-edit function were generalized to a `RuleTreeTarget`/
  `RuleTreeTargetConst` (trigger+conditions+ActionTree for a Rule, ActionTree
  alone for a Subroutine) resolved by `rule_current_target`/
  `rule_current_target_mut` (`editor/rule.c`, `editor/internal.h`). Rule mode
  is now feature-complete per the S5.6 brief. Remaining gaps: (1)
  `RuleTreeRow` (`editor/internal.h`) still only flattens rule-level
  conditions, not a control-flow node's own predicate
  (`ActionNode.conditions`) — an if_else row's CONFIRM is a no-op, and
  for_each's CONFIRM only edits its bind name, since there's no row yet to
  edit either node type's predicate through; (2)
  `create_timer`/`create_timer_periodic`'s duration and
  `set_attr`/`add_attr`'s value are edited as free strings (word builder +
  gamepad keyboard for digits) rather than through the numeric adjuster,
  since the brief for S5.6b only called out repeat-count and condition
  compare_value as adjuster fields; (3) S5.6c's MOVE only reorders a node
  among its existing siblings — promoting a node out of an if_else's
  then/else list into its parent's list (or the reverse) once the cursor
  runs off the end of its current siblings needs a design call (which
  branch is on the receiving/donating end isn't determined by "direction"
  alone) that the S5.6c brief left for a future slice to resolve.
- **Animation mode's sparse-attr round trip** (§28, D20) —
  `emit_animation_if_present` (`engine/src/toml_emitter.c`) emits all four
  `animation = {...}` fields as soon as any one of
  `anim_frames`/`anim_size`/`anim_speed`/`anim_row` exists, defaulting the
  untouched ones to 0 for emission. Bumping a single field via the
  Animation editor (`editor/anim.c`, S5.5) therefore writes `size = 0` /
  `speed = 0` / `row = 0` for the sibling fields into the saved TOML, and
  `parse_animation` re-reads them as real 0-valued attrs on reload — below
  the editor's own `>= 1` floor for `anim_size`, since parsing bypasses
  `anim_edit_clamp`. Pre-existing S3.4 emitter/parser gap, not introduced
  by S5.5, but the row-by-row adjuster is the first UI path that naturally
  invites touching only one of the four fields. Not attempted in S5.5;
  would need `emit_animation_if_present` to track which fields were
  authored vs defaulted, or the editor to eagerly set all four attrs on
  first touch.
- **Level mode music picker** — S5.2b's detail-row editor lets you type an
  arbitrary string into `music` via the word builder (`editor/level.c`
  `enter_level_detail_string_edit`); the master plan (S5.2, S6.13) always
  intended a fuzzy-finder over an embedded music-name registry once that
  registry exists. The registry now exists as of S6.13b (`map_strv_music`
  on `AssetRegistry.music`, `audio.h`/`game.h`, populated by `main.c`'s
  `music_registry_add`), so this is now actionable: swap the free-text
  word builder for a fuzzy-finder over the registry keys, mirroring the
  Atlas-mode texture picker. Blocked only on the registry having more than
  one entry to pick from (currently just `bgm.mp3`), which is content, not
  engine work.
- **Spatial SFX falloff** — deferred out of S6.13b (D32) as explicitly out
  of scope for the per-level-music slice. `sfx_alias_pool_play`
  (`audio.h`/`.c`) currently plays every `play_sound:` at a flat
  `preferences_effective_sfx_volume` with no positional attenuation. A
  spatial pass would scale (and potentially pan) that volume by distance
  from the player/camera to the sound's source entity, which means routing
  a source position through `SoundEffectRequest` (`effect.h`) and the
  `apply_sound_effects` call site (`frame.c`). See DESIGN.md § "Audio
  Design" ("Spatial sound — volume based on distance to entity?").
- **DESIGN.md's "Editor Controls (Draft)" table is stale for multi-select
  and grid-snap (DESIGN.md:121-122).** S5.7 (D38) implemented multi-select-
  add as L1+CONFIRM / Ctrl+Enter and the grid-snap toggle as L1+Up /
  Ctrl+Up (`default_editor_multiselect_add`/`default_editor_grid_snap_toggle`,
  `input_func.c`), not the draft table's "LT (hold)" / "RT + D-pad up" — R1
  and L2 were already independently bound to actions checked every frame in
  Scene Browse (`ACTION_EDITOR_PLACE`, `ACTION_EDITOR_WATCH`), so chording
  either would fire that action alongside the new gesture. The Undo/
  Duplicate rows in the same table were already stale before this slice.
  Revisit the whole table if it's ever promoted out of "Draft."

## Rule VM follow-ups

- **Numeric action arguments aren't validated for non-numeric garbage.**
  `execute_change_sprite_action` (`rule.c`) only checks structurally
  that `x,y,w,h` has all four comma-separated parts (mirroring
  `execute_transition_action`'s comma check); like `set_attr`/`add_attr`,
  a non-numeric token (e.g. `change_sprite:16,oops,48,64`) silently resolves
  via `strtof` to `0` rather than erroring. S6.5's `camera_pan`/`camera_shake`
  and S6.6's `spawn` (`execute_camera_pan_action`/`execute_camera_shake_action`/
  `execute_spawn_action`, `rule.c`) break from this: they validate every
  numeric token via `parse_strict_float` and `error_set` on garbage, since a
  bad pan/shake duration or spawn position should fail loudly rather than
  silently pan/shake for zero seconds or spawn at the origin. S6.7b's `wait:`
  (`handle_wait_action`, `rule.c`) joins the strict group too. The VM is now
  inconsistent on this axis (four actions strict, the rest permissive) —
  worth a shared pass to pick one behavior and apply it everywhere, whichever
  direction that goes.
- **A `for_each` suspended mid-wait resumes its scan by raw view index, not
  by re-deriving "who hasn't been visited yet."** `ExecFrameSnapshot.
  for_each_entity_index` (`rule.h`, S6.7b) records the view-array index the
  scan had reached at suspend time; `restore_exec_frame` (`rule.c`) trusts
  it as-is when the frame's own bound entity (`bound_entity_id`) is still
  found. If an entity is spawned or destroyed while a `for_each` containing
  a `wait` is suspended, the resumed scan can skip a newly-inserted match or
  (harmlessly) re-scan past a removed one, since spawns/destroys don't
  reorder the array today but nothing guarantees that stays true. Not
  reachable by any current spawn path (`spawn:` is drain-phase, entities
  only append) and not covered by a test; worth a scan-by-id rewrite if a
  future feature spawns/removes entities while a suspended `for_each` is in
  flight.
- **Editor Place path never resizes the overlap/solid edge vecs after
  spawning.** `handle_place_input` (`frame.c`) hand-patches `entity_blueprints`
  and `rule_table` for the newly-placed entity but, unlike S6.6's spawn path
  (`respawn_rebuild_tracking`, `frame.c`) and S5.7's paste
  (`setup_current_level_runtime` call), never grows `prev_player_overlaps`/
  `prev_solid_collisions` to the new entity count. Harmless today: `game_update`
  (`game.c`) skips all overlap detection while `editor_mode` is on, so the
  under-sized vecs are never read during placement, and the next full reload/
  transition rebuilds them anyway. Becomes a real bug if overlap detection ever
  runs in editor mode, or if a Place happens without a following reload before
  play resumes. Fix by routing Place through the same rebuild the spawn/paste
  paths use.
- **Only one `dialogue:` can be active at a time.** `handle_dialogue_action`
  (`rule.c`, S6.7c) drops (logs) a second `dialogue:` open that fires while
  another dialogue is already active -- e.g. two `on_spawn` rules both
  opening dialogue in the same frame -- rather than queuing it. The
  dropped rule still suspends with `WAKE_DIALOGUE_CLOSED` so it resumes
  once the already-open dialogue closes, but its own text is never shown.
  Fine for a single-NPC-at-a-time game; would need a `vec` of pending
  dialogue requests if simultaneous triggers become common.
- **Inventory core, pickup toast, and pause-menu grid landed (S6.8a/S6.8b/
  S6.12b, D25); equipment/categories still pending.** `give_item`/
  `remove_item`/`has_item` are real (`ItemSet`, `ProgressionState.items`,
  `rule.c`), `give_item` shows a "Got <item>" toast via S1.2's toast
  surface (`effect.c`'s `ToastRequest`, `frame.c`'s `apply_toast_effects`),
  and the pause-menu Inventory entry opens a gamepad-navigable grid of the
  snapshot (`engine/src/inventory_screen.h`/`.c`). Equipment/categories,
  attribute-modifying equipment, and per-item actions from the grid
  (use/equip/drop) are still open, per DESIGN.md's Inventory & Items open
  questions.

## Collision system follow-ups

- **Editor cannot author or edit composite collision shapes.** S4.5 added
  `[[blueprint.collision]]` TOML parsing/emission and deep-copy into
  `entity->collision_region`, but the in-game editor's collision handles
  (`draw_collision_handles`) still only drag primitive 0 of the one-rect
  fallback. Authoring a multi-primitive composite (rect+circle+triangle) is
  TOML-only today. A visual editor for this would need a primitive list UI
  (add/remove/select kind) plus per-kind drag handles.
- **Hitbox/hurtbox have no `[[blueprint.hitbox]]`/`[[blueprint.hurtbox]]`
  TOML composite authoring (S6.10a).**
  `entity_hitbox_region`/`entity_hurtbox_region` (`entity.c`) only support
  the attr-based one-rect fallback (`hitbox_offset_x/y`+`hitbox_w/h`,
  `hurtbox_*`) plus the `entity->hitbox`/`entity->hurtbox` composite
  fields for future authoring -- mirrors `trigger_region`'s own
  still-open composite gap (the bullet above only covers
  `collision_region`). Would need the same `[[blueprint.collision]]`
  treatment (parse + emit + deep-copy on instantiate)
  `blueprint.c`/`level.c`/`toml_emitter.c` already give `collision_region`.

## Animation system follow-ups

- **Editor cannot author `[[blueprint.animation]]` clips (S6.11a, D31).**
  The new state -> row/frames/speed clip table is TOML-only today --
  `data/gamedata.toml` was hand-edited to give the "player" blueprint its
  `walk`/`idle` clips. The existing Animation mode editor
  (`EDITOR_SUB_ANIM_EDIT`/`_FRAMES`, `editor/anim.c`) still only edits the
  older single-clip `anim_frames`/`anim_size`/`anim_speed`/`anim_row`
  attrs (S3.4/S5.5/D20) and has no UI for the new per-state clip list. A
  visual editor for this would need a clip list UI (add/remove/select
  state name) alongside the existing frame scrubber.
- **No per-entity animation clip override.** Clips are looked up live via
  an entity's blueprint (`entity_resolve_blueprint`/`blueprint_find_anim_clip`),
  never copied onto the instance, so there is no way for one entity to
  play a different clip set than its blueprint siblings. Adding one later
  is a straightforward extension of the same scoped-lookup pattern every
  other attr already uses (check instance attrs/overrides before falling
  back to the blueprint's `[[blueprint.animation]]`).

## Combat follow-ups

- **`behavior_projectile` does not resolve obstacles or destroy on wall
  hit (S6.10d).** A projectile flies straight through solid geometry
  instead of stopping or vanishing at a wall -- explicitly deferred as
  optional polish per D26. Would need a `resolve_entity_obstacles`-style
  solid check in `behavior_projectile` (`game.c`) that soft-destroys the
  projectile (sets `active` false) on the first solid it touches, rather
  than pushing it back out like a walking mover.
- **`detect_melee_damage`'s attacker loop has no `active` gate (S6.10a).**
  Discovered while adding `destroy_on_hit` to `detect_contact_damage`
  (S6.10d), which needed the equivalent gate (`entity_can_deal_contact_damage`)
  to stop a soft-destroyed projectile from resuming contact damage once
  the target's i-frames lapse. `detect_melee_damage` (`game.c`) has the
  same gap on its attacker side: an entity destroyed (`ACTION_DESTROY` or
  otherwise) while `entity_hitbox_is_active` says its hitbox is live keeps
  landing melee hits for the rest of that window. Low impact today (the
  window is `attack_state_timer`'s duration -- typically the attacker's
  `attack` clip length, or `ATTACK_STATE_DEFAULT_SECONDS` = 0.15s with no
  such clip, per S6.11b/D31), but the same one-line fix `detect_contact_damage`
  got would close it.
- **`draw_entities_depth_sorted`/`draw_entity`/`draw_animated_entity`
  (`main.c`) don't gate on the `active` attr at all.** Discovered while
  adding the death state (S6.11b, D31): a soft-destroyed entity (defeat
  with no `death` clip, an expired projectile, `destroy_on_hit`, etc.)
  keeps rendering every frame even though nothing updates its
  collision/damage/rules anymore -- harmless for a fast-moving expired
  projectile that's easy to miss, more visible for a defeated enemy that
  should vanish. `entity_is_active`/`entity_is_visible` (`entity.c`)
  already exist and are unit-tested, but nothing in `main.c`'s render path
  calls either one yet -- wiring one into the depth-sort draw loop would
  close this.
- **Player has no `hurt`/`death` clip (S6.11b, D31).** `assets/sprites/player.png`
  row 9 has a candidate collapse sequence (stand -> stumble -> fully down,
  4 frames) but only ONE row -- no side/up variants the way the walk (rows
  3-5) and new attack (rows 6-8) clips have. Authoring it as-is via the
  existing `anim_row_offset_for_direction` scheme would read past the
  bottom of the sheet (rows 10/11) for side/up facing. Needs either
  direction-variant art drawn for `hurt`/`death`, or a per-clip opt-out of
  the direction row offset (e.g. a clip flag meaning "always row as
  authored, ignore direction") before this can be wired into
  `data/gamedata.toml`. Also moot in practice today since no enemy
  blueprint exists yet to defeat the player at all.

## HUD follow-ups

- **Player blueprint has no `health`/`max_health` attrs (S6.12a, D34).**
  `data/gamedata.toml`'s "player" blueprint authors no `health` field at
  all today (the only `health = [100, 100]` in the file belongs to the
  unrelated "tree" blueprint). `hud_compute_hearts` correctly renders
  zero hearts when both values are 0 -- not a code bug -- but it means
  the new HUD hearts row is invisible in an actual play session until
  the player blueprint gains a `health = [current, max]` pair. Picking
  that starting/max value is a game-balance decision (interacts with
  existing `damage`/`defense`/`iframes` tuning), not made as part of
  S6.12a; needs a decision, then a `data/gamedata.toml` edit through the
  Syncthing sync procedure (CLAUDE.md's "Gamedata Sync Workflow").
- **No heart sprite asset (S6.12a, D34).** `hud_draw_hearts`
  (`engine/src/hud.c`) draws placeholder rectangles (filled/half-filled/
  outlined squares). A real heart sprite would replace `draw_heart`'s
  raylib calls with a texture draw once art exists -- no change needed
  to the pure `hud_compute_hearts`/`hud_heart_screen_position` layer.
- **Minimap not started (S6.12a covered hearts, S6.12b covered the
  inventory grid; D34).** The pause-menu inventory grid landed in S6.12b
  (`engine/src/inventory_screen.h`/`.c`, see the "Inventory core, pickup
  toast, and pause-menu grid" bullet above). A minimap/map screen is a
  separate, still-deferred slice.

## Audio / SFX follow-ups

- **`map_strv_sound` has no generic iterator.** `unload_sfx_registry`
  (`main.c`) walks `.entries[]`/`.capacity` directly, checking
  `MAP_ENTRY_OCCUPIED` by hand, since `map.h`'s `MAP_DECL`/`MAP_IMPL`
  macros don't emit a for-each helper. Fine for the two-entry S6.4
  registry; worth a generic `map_<name>_for_each` if a bigger map ever
  needs the same teardown-by-value pattern.

## Level transition follow-ups

- **A hot-reload or pause-menu RESTORE landing mid-fade races the
  pending swap.** `handle_hot_reload` (`main.c`) polls unconditionally
  every `HOT_RELOAD_POLL_FRAMES`, and RESTORE's dispatch both end up
  calling `game_load_gamedata`, which rewinds and re-parses
  `gamedata_arena` without checking `state->fade.phase` (S6.14, D27).
  `state->transition.level` is a `Str` allocated in `gamedata_arena` by
  `execute_transition_action` (`rule.c`) and is only copied out at the
  fade's midpoint (`run_transition_swap`, `frame.c`). If either reload
  path fires while `state->fade.phase != TRANSITION_FADE_NONE`, that
  `Str` can be rewound/overwritten before the swap reads it, producing
  a garbled level name. Narrow window (the fade is 0.6s total end to
  end) but real given the project's live hot-reload workflow. Needs a
  decision (defer the reload until the fade finishes, cancel/reset the
  fade on reload, or re-validate the pending transition against the
  freshly-loaded gamedata) before fixing.
- **Spawned entities (`spawn:`, S6.6) don't persist across
  transitions (S6.15b, D33).** The per-level entity delta
  layer (`progression_capture_level_delta`/`_apply_level_delta`,
  `progression.c`) keys each `EntityDelta` on `Entity.id`, assigned by
  `Level.next_entity_id` in TOML parse order -- stable for AUTHORED
  entities across a reparse, but a runtime-spawned entity's id is only
  valid for that session. `level_find_entity_by_id` simply won't find it
  again after the level reloads, so a spawned entity's captured delta
  is silently dropped on apply. Would need spawned entities to carry
  a stable cross-session identity (e.g. a blueprint+spawn-site key)
  before they could round-trip a transition too.
- **Hot-reload clearing the entity delta store isn't integration
  tested (S6.15b, D33).** `progression_clear_level_deltas` is called
  from `main.c`'s `reset_editor_after_reload`, the shared hook both
  `poll_hot_reload` and `menu_dispatch_restore` funnel through --
  but `test_trigger_hot_reload` (`test_helpers.c`) bypasses that
  function entirely and drives `game_load_gamedata` directly, the
  same way it already skips `undo_history_clear` for the same reason
  (the mtime polling/disk read that trigger it in production are I/O
  plumbing already excluded from headless tests). No test currently
  proves a captured delta is dropped on hot-reload; would need
  `reset_editor_after_reload` (or equivalent) reachable from test
  infrastructure.
- **No cross-level "player" section in the save format yet (S6.15c,
  D33).** `SaveState` (`save.h`) bundles `current_level_name` plus
  the whole `ProgressionState` (flags/vars/items/level_deltas), but
  there is no dedicated `[[player]]`-style table the way DESIGN.md's
  original multiplayer-era Save Format sketch has. The player's own
  state (health, position, any instance attrs) rides inside whichever
  level's `EntityDelta` it occupies at save time, via S6.15b's
  `progression_capture_level_delta` -- correct for a save taken
  mid-level (the caller must run that capture for the current level
  first), but there is no cross-level home for player state independent
  of which level it happens to be standing in. The pause-menu Save/Load
  UI (S6.15d2) has now landed without forcing the question; revisit once
  multiplayer forces it instead.
- **`save_load` doesn't pre-seed overlap tracking after loading a save
  (S6.15d1, D33).** `frame.c`'s `run_transition_swap` pre-seeds
  `prev_player_overlaps` after positioning the player at a transition's
  spawn point specifically so `enter`/`collide` triggers don't refire
  for entities the player already overlaps at that position;
  `save_load` (`save.c`) has no equivalent step after
  `progression_apply_level_delta` restores the player's saved position.
  A save taken while the player stood inside a trigger's region could
  cause that trigger to fire again immediately after loading. Narrow
  in practice (most triggers are one-shot or state-gated) but not
  proven safe. The pause-menu Load UI (S6.15d2) now makes this
  reachable from real play (`frame.c`'s `handle_save_screen_confirm` ->
  `save_load`), not just from direct `save_load` calls -- still not
  fixed, just no longer only a theoretical path.

## Input system future work

- **Plugin-declared actions.** When a plugin/engine system arrives,
  bindings need a parallel registry alongside the central enum.
- **Per-gamepad binding.** AtomicInput.int_b stores a gamepad id but
  the entire codebase still assumes gamepad 0. Settings UI does not
  expose a gamepad selector. Defer until the engine actually supports
  multiple gamepads.

## Distribution follow-ups

- **Android on-device embedded-gamedata verification is a manual step
  (S7.2, D40).** `gamedata_source_read` (`engine/src/gamedata_source.h`/`.c`)
  and its `engine_tests` coverage (`gamedata_source_test.c`) prove the
  file-first/embedded-fallback load order headlessly, but nothing in CI
  can exercise the real Android install path. Before shipping an APK
  built with `SLEIPNER_EMBED_GAMEDATA` ON, delete
  `/storage/emulated/0/Sync/sleipner/gamedata.toml` on a device (or a
  fresh install that never synced one) and confirm the game still boots
  into the embedded copy rather than showing a load error.
- **The `embed_asset`-generated `.S` files carry no `.note.GNU-stack`
  marker (pre-existing, not introduced by S7.2).** Every asset embedded
  via `embed_asset` (`engine/sources.cmake`) lacks the section, so GNU ld
  2.44 warns `missing .note.GNU-stack section implies executable stack`
  at link time for whichever embedded-asset object happens to be last on
  the link line -- adding `gamedata_toml` just changed which object's
  name shows up in the warning (confirmed by configuring with
  `SLEIPNER_EMBED_GAMEDATA=OFF`, where the warning re-attaches to
  `gamecontrollerdb_txt` instead). Harmless today (a warning, not an
  error; the resulting `PT_GNU_STACK` defaults conservatively rather than
  actually executing anything off the stack) but would need
  `embed_asset`'s generated template to append a
  `.section .note.GNU-stack,"",%progbits` marker to silence it for good.

## Multiplayer follow-ups

- **`discovery_host_tick`'s `listen_port` argument is a placeholder
  (S8.3b), still true after S8.4a.** `game.c`'s `tick_network` passes
  `DISCOVERY_PORT` itself as the beacon's advertised
  `BeaconMessage.listen_port` because no separate game-session
  socket/port exists yet -- S8.4a's JOIN/INPUT flow (`network.c`'s
  `network_client_send_join`/`_send_input`/`network_host_receive`)
  reuses whatever transport `network_start_hosting`/`_discovering`
  already bound to `DISCOVERY_PORT`, rather than opening a dedicated
  one. Once a real session socket exists, `tick_network` needs to
  advertise its real port instead -- otherwise a joining client's
  `DiscoveredHost.addr` (net_discovery.c's `discovery_client_tick`,
  assembled from the beacon's source IP plus this `listen_port`) points
  at the discovery socket, not the session socket S8.4a's `join_target`
  actually dials.
- **S8.4b's DELTA broadcast sends full synced state every tick, not a
  diff.** `network_host_broadcast_delta` (net_session.c) re-serializes
  every entity's position + attrs on every hosting tick regardless of
  whether anything actually changed since the last tick -- correct
  (a client's view can only lag by a tick, never permanently drift)
  but wastes bandwidth that scales with level size. Real per-attr
  diffing (track the last value sent per entity/attr, only emit
  a record when it changed) is deferred; fine for the LAN-only
  pre-alpha this targets, revisit once bandwidth actually matters
  (real UDP across a real LAN, or much larger levels).
- **S8.4b's multi-packet SNAPSHOT/DELTA splitting is unexercised by
  any test.** `send_sync_records` (net_session.c) chunks a level's
  sync records into `NETWORK_SYNC_RECORDS_PER_PACKET`-sized (20)
  packets, but neither `test_integration_delta_converges_client_view`
  nor `test_integration_join_snapshot_equivalence` has enough
  entities/attrs to force a second packet -- both fixtures stay well
  under the cap. Overflow safety doesn't depend on the split actually
  being exercised (`protocol_encode_snapshot_packet`/`_delta_packet`
  bounds-check and fail closed, and a failed chunk is silently dropped
  rather than written out of bounds), but the chunking logic itself
  has no direct test. A fixture with enough entities to force more
  than 20 records would close this gap.
- **Real LAN discovery across two physical machines is unverified by
  any automated test (S8.3b).** A headless test cannot observe a UDP
  broadcast actually reaching another host on the LAN -- `net_test.c`
  and `network_test.c` only prove the `SO_BROADCAST`/`SO_REUSEADDR`/
  `SO_REUSEPORT` setsockopt calls succeed and that two sockets can
  coexist on one machine via port reuse. Manual on-hardware QA (host on
  one machine, join from another) is needed before relying on this in
  practice.
- **S8.4c's reliable event channel carries a session notification
  (`NETWORK_EVENT_PLAYER_JOINED`), not a real `rule.h` gameplay
  trigger.** The brief's own suggested candidates (`TRIGGER_DEFEAT`, a
  level-transition notification) need `game_update`'s `trigger_events`
  vec (`game.c`) to be observable outside that one function call --
  today it's built into a `scratch_arena`-backed `vec_trigger_event`
  and fully drained by `rules_evaluate_batch` in the same call, with no
  hook any external caller (like `net_session.c`'s `network_host_tick`)
  can reach. Routing a real trigger over `net_reliable.h`'s
  `ReliableChannel` needs that hook added first -- e.g. an out-parameter
  on `game_update`, or a `GameState`-level accumulator the trigger
  pipeline appends fired events into for the network layer to drain
  after each hosting tick -- a broader change than the reliable-channel
  wiring itself (`net_reliable.{h,c}`, `network.{h,c}`,
  `net_session.{h,c}`).
- **Only the host's own local:0 player triggers interact/enter-overlap
  events (S8.6).** `collect_trigger_events` (`game.c`) is still keyed to
  `game_get_player`/`state->gamedata.player_index` -- the single
  first-authored player entity -- not every connected player, so a
  joining client's character can stand on a trigger zone or press
  interact next to a chest and nothing fires; only the host's own
  movements/input reach `detect_interact_targets`/`detect_enter_targets`.
  `prev_player_overlaps` (`game.c`) would need to become per-player (not
  one flat per-entity vec) to track enter/exit edges against more than
  one player at once before this can be fixed.
- **S8.6's per-join spawn always places a new player at the host's own
  current position, so multiple joining clients stack exactly on top of
  the host and each other.** `spawn_network_player` (`net_session.c`)
  picks this over an authored player-start attr or a level spawn-point
  system as the simplest always-valid choice (documented in its own doc
  comment) -- fine for the LAN-only pre-alpha this targets, revisit once
  a level wants to separate spawn points.
- **A client materializing a host-spawned entity (S8.6's
  `NETWORK_ATTR_BLUEPRINT_NAME` convention, `net_session.c`) assumes that
  record arrives before any other record for the same `entity_id` in the
  same drain pass.** True by construction over `net_loopback.h`'s FIFO
  (`push_entity_sync_records` always pushes blueprint_name first, and
  packets are consumed in send order) and true within a single UDP
  datagram, but a real lossy/reordering UDP path could in principle
  deliver a later chunk (e.g. `pos_x`) before an earlier one carrying
  `blueprint_name`, if a large sync happens to straddle a
  `NETWORK_SYNC_RECORDS_PER_PACKET` chunk boundary right at that entity.
  `ensure_synced_entity_exists` silently no-ops on an out-of-order
  `pos_x`/`pos_y` for a still-unmaterialized entity (same "eventually
  converges" contract every other v1 sync record already has, since the
  very next full-state DELTA resends everything) rather than crashing or
  misapplying it, so this is a convergence-latency gap, not a
  correctness one -- unexercised by any test, since loopback never
  reorders.

## misc
- for some reason, when running against the walls significantly warps the
  sprite. could be related to float position not scaling up correctly or other
  scaling issue.

## Pause menu follow-ups

Carried over from the pause-overlay menu landing:

- **`blur_resize` is implemented but never called (moot today).**
  Verified 2026-07-03: the window is created non-resizable (no
  `FLAG_WINDOW_RESIZABLE`; fullscreen or fixed-size) and `game_bounds`
  plus the scene render texture are computed once at startup and never
  recomputed, so `IsWindowResized()` cannot fire and `blur_resize`
  would be a no-op (the blur is `game_bounds`-sized and scales with the
  scene). This only becomes meaningful once a resizable-window feature
  exists, at which point the resize path should recompute
  `game_bounds`/the render texture, set `menu->blur_captured = false`
  (and the settings sibling), then call `blur_resize`. Function kept
  for that future.

## Possible action items

Carried over from the Settings tabs + path picker series (commits
d890de8 through 3641352).

- **`preferences_load` doesn't normalize trailing slash on
  `data_dir`.** The Settings UI commit path appends `/` when missing
  (see `path_edit_commit` in `engine/src/settings.c`), but
  `preferences_load` in `engine/src/preferences.c` does not. A user
  manually editing `preferences.toml` to `data_dir = "data"` will
  make `gamedata_path()` compose `datagamedata.toml`. One-line fix
  at the end of `preferences_load`.
- **No path validation on commit.** `path_edit_commit` accepts any
  string the user types; failure surfaces only at the next gamedata
  reload via the existing fopen/stat error path. Out of scope in
  v1, but the path picker could flag obviously-bad paths
  (non-existent, not a directory, not writable) before exiting the
  screen and offer the user a chance to fix it.
- **TestGame can't verify on-disk preferences write.**
  `frame_ctx.preferences_save_fn = nullptr` in
  `engine/test/test_helpers.c::test_game_setup_with_level`, so
  `test_integration_settings_path_edit_commit` can only assert
  `save_preferences_requested` was raised + consumed. Add a
  settable fake save fn (mirror of how `KeybindingsSaveFn` would
  need similar treatment) so tests can assert that the actual
  preferences.toml write happened.
- **Browse mode swallows `LoadDirectoryFilesEx` failures.** A
  permission-denied or stat-failed target directory shows an empty
  list with no explanation (`path_edit_refresh` in
  `engine/src/settings.c`). Surface a toast or an explicit "(no
  permission)" row.
- **Hardcoded filenames in path helpers.** `gamedata_path` /
  `keybindings_path` / `trace_log_path` in `engine/src/main.c` bake
  the filename into the helper. If per-file overrides are wanted
  later (separate `gamedata_dir`, `keybindings_dir`, `trace_dir`),
  that's where to start.
- **Desktop `trace.log` path migrated.** Pre-refactor the desktop
  trace lived at cwd; post-refactor it resolves to
  `<data_dir>/trace.log` (so `data/trace.log` by default). Anyone
  with tooling pointed at the old location needs to update or wire
  up a CLI/env override that reads the boot-stage path.
- **Future top-level `.c` files using `M_PI` will break MinGW.**
  The engine library's `_GNU_SOURCE` opt-in is gated on `!WIN32`,
  so MinGW does not see `M_PI` from `<math.h>`. `keyboard_widget.c`
  hit this in commit 626b0e4 and was hot-fixed with a local
  `KB_PI` constant in 2642e6d. `engine/src/math_consts.h` now
  provides a shared `SLEIPNER_PI` constant for this;
  `keyboard_widget.c` has been migrated to it. `editor/widgets.c`
  still uses `M_PI` via the fallback `#define` in
  `editor/internal.h`, which is fine for editor code, but new
  top-level `.c` files should use `SLEIPNER_PI` instead of `M_PI`.
- **DRIVE_SELECT on POSIX shows a single `/` entry.** The user
  picker is functional but degenerate on Linux/Android, where the
  OS has no drive concept. On Android in particular, useful roots
  are `/storage/emulated/0/`, `/sdcard`, and the app-specific
  `getExternalFilesDir()` path. Populate those into
  `path_edit_populate_drives` (POSIX branch in
  `engine/src/settings.c`) so the row also serves as a "jump to
  common Android root" shortcut, gated on `__ANDROID__`.
- **Host move creates two undo records in a session (S8.7h2a).** A
  host's own drag move now logs both a snapshot entry ("Move
  entity", `handle_drag_input`) and a per-author op-log pair
  (`network_editor_commit_move`). A host undo pops the op log
  (reverting via the op stream), leaving the snapshot entry
  unstepped, so the two histories diverge. A second undo then hits
  the snapshot fallback and reverts to an older state plus a
  (harmless, non-corrupting) spurious resync. Not fixed here
  because the clean fix (suppress the drag's snapshot push while in
  a session) is a behavior decision outside this slice's MOVE
  scope. Revisit when set-attr/remove-attr/delete/place inverses
  land, since those seams have the same snapshot/op-log overlap.
