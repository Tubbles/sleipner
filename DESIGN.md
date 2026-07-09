# Game Design & Architecture

## Vision

Sleipner is a top-down Zelda-like action RPG (Link to the Past / Link's Awakening style). Controller-first, pixel art, runs on Linux and Android.

The game is designed from within itself — an integrated editor mode allows building levels, composing entities, tuning collision, and testing gameplay without leaving the running game.

## In-Game Editor

The editor is a mode within the game, not a separate tool. Toggle between play mode and editor mode at any time. Everything is gamepad-driven.

### Core Principles

- **Edit what you see.** The editor operates on the live game world. Changes are immediately visible.
- **Gamepad-first.** Every editor action is reachable from a controller. No mouse/keyboard required (though keyboard shortcuts are fine as secondary input).
- **Always show the controls.** Every editor screen displays the current button mappings on-screen — what each button does in the current context. The user should never have to memorize or guess. Button hints update dynamically as the context changes (e.g. different hints when an entity is selected vs when nothing is selected).
- **Data, not code.** Levels, entity blueprints, collision shapes, and behavior parameters are data that the editor reads and writes. Game logic consumes this data at runtime.
- **The editor is the only tool.** Every single aspect of game data — without exception — must be editable from within the editor. If it's in `gamedata.toml`, it has an editor mode. There is no workflow that requires hand-editing the TOML file. The editor is the complete authoring environment for the game.
- **Round-trip.** Play, pause, edit, resume. The editor and the game share the same world state.

### Editor Modes

1. **Scene mode** — The main level editing mode. Free camera, select/place/move/delete entity instances, inspect properties, compose scenes.
2. **Blueprint mode** — Create and modify entity blueprints. Set texture, source rect, collision box, behavior, attributes, rules, children. All blueprints are listed and searchable. Changes propagate to all instances (unless overridden).
3. **Tile mode** — Paint terrain tiles on the grid. Ground and overlay layers. Tile palette with preview. Autotile placement.
4. **Atlas mode** — Set up sprite atlases. View the full texture, define named regions (source rects), preview individual sprites. This is how you tell the engine which rectangle of a sprite sheet corresponds to which sprite.
5. **Animation mode** — Define and preview animations. Set frame count, frame size, speed, row. Scrub through frames with the gamepad. Preview the animation playing on the entity. Link animation states (idle, walk, attack) to directional rows.
6. **Rule mode** — Build and test game logic. Visual rule editor with trigger/condition/action pickers. Test rules by switching to play mode and back.
7. **Level mode** — Manage levels. Create new levels, set level size, music, spawn points, transitions between levels. View all levels as a list or map.

### Editor UI

- Cursor controlled by left stick, snaps to grid (toggle-able).
- Color-coded overlays: collision boxes (green), spawn points (blue), trigger zones (yellow), rule indicators (orange).
- All input flows through the widget system described below — no raw text entry during normal editing.

### Gamepad Input Widgets

**Radial picker** — primary selection widget (4-12 items per ring):
- Tilt left stick to highlight, A to confirm, B to cancel.
- Nested rings for subcategories (e.g. action type → outer ring = category, inner ring = specific action).
- Used for: mode switching, tool selection, trigger/condition/action type picking, behavior type, etc.

**Scroll picker** — for longer lists:
- D-pad up/down to scroll, bumpers to page jump.
- Live fuzzy filter via the word builder — list narrows as you compose.
- Visual previews where applicable (texture thumbnails, sprite previews).
- Used for: blueprint palette, texture selection, flag/item references, level selection.

**Word builder** — composing names without a keyboard:
- Picks words one at a time from a vocabulary via scroll picker.
- Vocabulary is seeded from: (1) a built-in dictionary of common RPG words, (2) every name/flag/item/blueprint in the current gamedata.
- Most recently used and most frequent words bubble to the top.
- Underscore separator is automatic between words.
- "Done" (A) confirms, "Back" (B) removes last word.
- Built-in seed vocabulary includes: `chest`, `locked`, `magic`, `key`, `door`, `open`, `closed`, `hidden`, `secret`, `boss`, `enemy`, `spawn`, `trigger`, `zone`, `north`, `south`, `east`, `west`, `bridge`, `gate`, `switch`, `lever`, `fire`, `ice`, `water`, `stone`, `wood`, `gold`, `silver`, `sword`, `shield`, `bow`, `arrow`, `heart`, `potion`, `fairy`, `dark`, `light`, `cave`, `forest`, `dungeon`, `castle`, `village`, `temple`, `tower`, `path`, `wall`, `floor`, `roof`, `big`, `small`, `red`, `blue`, `green`, and more.

**Fuzzy finder** — for referencing existing things:
- When a field expects a blueprint name, flag, item, or level name, shows a fuzzy-matched list of everything that already exists in the gamedata.
- Stick navigates, matches re-rank in real time.
- 90% of the time the user is referencing something that already exists — this avoids typing entirely.

**Value adjuster** — for numbers:
- D-pad left/right for ±1, triggers for ±10, bumpers for ±100.
- Hold for auto-repeat with acceleration.
- Visual feedback: collision box edges move live, position updates in real time.

**Gamepad keyboard** — last resort for truly novel words:
- Radial character groups (vowels in one wedge, common consonants clustered).
- Predictive suggestions from the vocabulary as letters are entered.
- Only surfaces when the word builder can't find what you need.

### What You Can Edit

The editor provides full control over every aspect of the game data:

**Blueprint editing** — create and modify entity blueprints:
- Texture and source rect (visual sprite picker with preview).
- Collision box offset and size (drag handles, live visualization).
- Behavior type and params (radial picker + value adjusters).
- Animation settings (frames, size, speed, row).
- Rules: add/remove/edit triggers, conditions, and actions.
- Duplicate an existing blueprint as a starting point.

**Entity instance editing** — modify placed entities in the level:
- Position (left stick to drag, with optional grid snap).
- Per-instance overrides of any blueprint field.
- Collision box nudging with visual handles (corners and edges).
- Rule overrides (add instance-specific rules beyond the blueprint).

**Scene composition** — design entire levels:
- Place entities from the blueprint palette.
- Multi-select and group move.
- Copy/paste entities and groups.
- Level size adjustment.
- Tile painting (ground and overlay layers).
- Set level music, spawn point, transitions.

**Rule authoring** — build game logic visually:
- Pick trigger type (radial picker).
- Add conditions (fuzzy finder for existing flags/items).
- Chain actions (scroll through action types, fill params with pickers).
- Test rules immediately by switching to play mode.

### Editor Controls (Draft)

| Action | Gamepad | Keyboard |
|---|---|---|
| Toggle editor | Select + Start | F2 |
| Move cursor | Left stick | Arrow keys |
| Select / confirm | A (south) | Space |
| Cancel / back | B (east) | Escape |
| Open palette | Y (north) | Tab |
| Cycle tools | Bumpers | Q / E |
| Adjust value | D-pad / triggers | +/- |
| Delete selected | X (west) | Delete |
| Save level | Start (in editor) | Ctrl+S |
| Undo | LB + B | Ctrl+Z |
| Duplicate selected | RB + A | Ctrl+D |
| Multi-select toggle | LT (hold) | Shift (hold) |
| Grid snap toggle | RT + D-pad up | G |

## Entity System

Composition over inheritance. An entity is an ID plus a bag of attributes.

### Attributes

Every entity has **attributes** — typed key-value pairs. Some are built-in (the engine knows about them), others are custom (user-defined per blueprint). All attributes participate in the same system — rules can read/write any of them.

**Built-in attributes** (always present on every entity):

| Attribute | Type | Purpose |
|---|---|---|
| `position` | vec2 | World position (or offset from parent) |
| `health` | int, int | Current and max health |
| `visible` | bool | Whether the entity renders |
| `active` | bool | Whether rules and behavior run |
| `solid` | bool | Whether collision is enabled |
| `opacity` | float | Render opacity (0.0–1.0) |

**Custom attributes** — defined freely per blueprint, any name and type:
- Types: float, int, bool, string.
- Examples: `speed = 80.0`, `patrol_radius = 50`, `is_locked = true`, `loot_table = "common"`.
- No fixed cap — stored in the arena, variable count per blueprint.

### Attribute Scoping (Blueprint vs Instance)

Blueprints define default attribute values. Instances override only what differs. Reading an attribute checks the instance first, then falls back to the blueprint.

```toml
[[blueprint]]
name = "chest"
texture = "chest.png"
src = [0, 0, 16, 16]
collision_offset = [0, 0]
collision_size = [16, 16]
behavior = "static"
health = [1, 1]
is_locked = true
loot_table = "common"

[[level.entity]]
blueprint = "chest"
pos = [300, 100]
# inherits is_locked = true, loot_table = "common"

[[level.entity]]
blueprint = "chest"
pos = [160, 280]
is_locked = false          # override: this one is unlocked
loot_table = "rare"        # override: better loot
```

The editor shows overridden attributes highlighted differently from inherited ones (attribute diff view).

### Blueprint Inheritance

A blueprint can extend another blueprint, inheriting all its attributes and rules. The child blueprint overrides or adds what differs.

```toml
[[blueprint]]
name = "locked_chest"
extends = "chest"
is_locked = true

[[blueprint.rule]]
trigger = "interact"
conditions = ["self.attr:is_locked", "has_item:key"]
actions = [
  "remove_item:key",
  "set_attr:self.is_locked,false",
  "dialogue:Unlocked!",
]
```

### Entity Composition and Children

An entity can contain child entities. Children are positioned relative to their parent and have their own attributes, rules, and behaviors.

```toml
[[blueprint]]
name = "wagon"
texture = "wagon_body.png"
src = [0, 0, 64, 32]
speed = 30.0

[[blueprint.child]]
blueprint = "lantern"
tag = "lantern"
offset = [56, -8]

[[blueprint.child]]
blueprint = "wheel"
tag = "front_wheel"
offset = [8, 28]

[[blueprint.child]]
blueprint = "wheel"
tag = "rear_wheel"
offset = [48, 28]
```

Use cases:
- NPC with equipment slots (child entities with own sprites and stats).
- Complex objects assembled from parts.
- A boss with independently targetable limbs.
- Particle emitters attached to entities.

Children inherit parent `visible` and `active` state but can override. Destroying the parent destroys all children.

### Tags

Tags let any entity in a composition tree reference any other entity in the same tree by name, regardless of nesting depth.

**Implicit tags** (always available):

| Tag | Refers to |
|---|---|
| `self` | This entity |
| `parent` | Immediate parent |
| `root` | Top-level entity in the tree |

**Custom tags** — assigned to children via the `tag` field. Must be unique within a tree. The editor enforces uniqueness and the fuzzy finder shows existing tags.

**Referencing attributes across the tree:**

```
main_weapon.attr:damage       # tagged child's attribute
parent.attr:health            # parent's attribute
root.attr:active              # root entity's attribute
shield.attr:defense           # any tagged family member
```

**Example — cursed sword that drains the wielder:**

```toml
[[blueprint]]
name = "cursed_sword"
damage = 15
drain = 2

[[blueprint.rule]]
trigger = "timer:1"
conditions = ["self.attr:equipped"]
actions = ["add_attr:root.health,-2"]
```

### Behaviors

Behaviors are coded in C and referenced by ID. They implement runtime logic that can't be pure data — physics, input, animation state machines. Examples:

- `static` — Does nothing (trees, rocks, decorations).
- `player` — Reads input, moves, animates.
- `npc_walk` — Walks a patrol path, stops, turns.

The behavior params array holds floats that the behavior interprets (e.g. patrol speed, interaction radius). The editor exposes these as named fields based on the behavior_id.

Most game logic that used to require dedicated behaviors (chests, doors, triggers) is now handled by the rule system instead.

### Rules (Visual Scripting System)

Rules are the data-driven scripting system — inspired by the Warcraft 3 World Editor trigger system. Each entity can have zero or more rules. Simple rules stay simple (flat string lists). Complex rules use structured control flow, variables, and entity queries. The goal is that rules should never feel limiting — anything you'd want game logic to do should be expressible without writing C.

#### Triggers

| Trigger | Fires when |
|---|---|
| `interact` | Player presses A near the entity |
| `enter` | Player steps into the entity's zone |
| `collide` | Entity touches another entity |
| `defeat` | Entity health reaches 0 |
| `timer:N` | N seconds elapsed (one-shot) |
| `timer_periodic:N` | Every N seconds (repeating) |
| `event:name` | A custom named event is fired (by another rule) |
| `on_spawn` | Entity is created/instantiated |
| `on_destroy` | Entity is about to be destroyed |
| `attr_changed:X` | Attribute X on self changes value |

#### Conditions

Conditions support AND/OR/NOT grouping. A flat list is implicit AND (backwards compatible). For complex logic, use `{ or = [...] }` and `{ not = "..." }`.

| Condition | True when |
|---|---|
| `has_item:X` | Player has item X |
| `flag:X` | Global flag X is set |
| `not_flag:X` | Global flag X is not set |
| `attr:X` | Attribute X on self is truthy |
| `attr:X<N` / `attr:X>N` / `attr:X==N` | Numeric comparison |
| `not_attr:X` | Attribute X on self is falsy |
| `tag.attr:X` | Attribute on tagged family member |
| `var:X` | Variable X is truthy |
| `var:X<N` / `var:X>N` / `var:X==N` | Variable comparison |
| `entity_count:blueprint<N` | Count of active entities of a blueprint type |
| `{ or = [...] }` | Any sub-condition is true |
| `{ and = [...] }` | All sub-conditions are true (explicit) |
| `{ not = "..." }` | Sub-condition is false |

#### Actions

| Action | Effect |
|---|---|
| `give_item:X` | Add item to inventory |
| `remove_item:X` | Consume item from inventory |
| `set_flag:X` | Set a global flag |
| `clear_flag:X` | Unset a global flag |
| `set_attr:target.X,V` | Set attribute on self or tagged entity |
| `add_attr:target.X,V` | Add to numeric attribute |
| `toggle_attr:target.X` | Flip a boolean attribute |
| `change_sprite:x,y,w,h` | Swap source rect (e.g. open chest) |
| `play_sound:file` | Play a sound effect |
| `dialogue:text` | Show dialogue box (blocks the rule until the box closes) |
| `transition:level,x,y` | Go to level at position |
| `spawn:blueprint,x,y` | Create entity at position |
| `destroy` | Remove the entity |
| `camera_pan:x,y,duration` | Pan camera over time |
| `camera_shake:magnitude,duration` | Screen shake effect |
| `fire_event:name` | Fire a custom named event (other rules can trigger on it) |
| `call:subroutine_name` | Execute a named subroutine |
| `set_var:name,value` | Set a local or global variable |
| `wait:seconds` | Pause execution for N seconds (async, doesn't block the game) |
| `create_timer:name,seconds` | Create and start a named timer |
| `destroy_timer:name` | Stop and remove a named timer |

#### Variables

Variables store typed values (int, float, bool, string, entity reference). Two scopes:

- **Local variables** — scoped to a single rule execution. Created with `set_var`, vanish when the rule finishes. Used for intermediate calculations, loop counters, temporary references.
- **Global variables** — persist across rule executions and save/load. Accessed with `global.var_name`. More powerful than flags (which are just boolean globals) — globals can store numbers, strings, entity references.

Variables can be used as action parameters via `$` prefix: `add_attr:$.health,-$damage`.

#### Control Flow

Actions can include structured control flow, turning the action list into a visual program:

**If/else:**
```toml
actions = [
  { if = "has_item:key", then = [
    "remove_item:key",
    "call:unlock_door",
  ], else = [
    "dialogue:It's locked.",
  ]},
]
```

**Loops:**
```toml
actions = [
  # Repeat N times
  { repeat = 3, do = [
    "spawn:enemy_bat,$random_x,$random_y",
  ]},
]
```

**For-each (entity queries):**
```toml
actions = [
  # For each entity matching a condition within a radius
  { for_each = "entity_in_radius:100", condition = "attr:is_destructible", do = [
    "add_attr:$.health,-$damage",
    "spawn:smoke_particle,$.position",
  ]},
]
```

The `$` inside a for-each body refers to the current entity being iterated. `$.health` is that entity's health, `$.position` is its position.

#### Subroutines

Named, reusable action sequences — callable from any rule via `call:name`. Avoids duplicating logic across rules. Defined at the top level of gamedata:

```toml
[[subroutine]]
name = "unlock_door"
actions = [
  "play_sound:door_unlock.wav",
  "set_attr:self.is_locked,false",
  "change_sprite:16,0,16,16",
  "dialogue:The door opens.",
]
```

#### Custom Events

Events decouple complex interactions across entities. One rule fires an event, any number of rules in any entities can trigger on it:

```toml
# Boss entity fires event on defeat
[[blueprint.rule]]
trigger = "defeat"
actions = ["fire_event:boss_defeated"]

# Gate entity in a different part of the level reacts
[[blueprint.rule]]
trigger = "event:boss_defeated"
actions = [
  "set_attr:self.solid,false",
  "change_sprite:0,16,32,32",
  "play_sound:gate_open.wav",
]

# Music entity reacts too
[[blueprint.rule]]
trigger = "event:boss_defeated"
actions = ["play_sound:victory_fanfare.wav"]
```

#### Complete Example

```toml
# Explosion trap with area damage, hard mode scaling, and cleanup
[[blueprint]]
name = "explosion_trap"
behavior = "static"
damage = 50

[[blueprint.rule]]
name = "explode_on_enter"
trigger = "enter"
actions = [
  "set_var:damage,50",
  { if = "flag:hard_mode", then = [
    "set_var:damage,100",
  ]},
  "play_sound:explosion.wav",
  "camera_shake:8,0.5",
  { for_each = "entity_in_radius:100", condition = "attr:is_destructible", do = [
    "add_attr:$.health,-$damage",
  ]},
  "spawn:fire_zone,self.position",
  "set_flag:trap_triggered",
  "destroy",
]
```

#### Implementation

The C side has one function per action type and one per condition type. The rule engine walks the action tree: flat strings are simple actions, inline tables are control flow nodes. Adding a new action/condition/trigger type is one C function — automatically available in the editor.

The editor renders rules as a visual tree (like WC3's trigger view), navigated with the gamepad. Each node is selectable, expandable, and editable with radial pickers and the word builder.

**Flags** are syntactic sugar for boolean global variables. `set_flag:X` is equivalent to `set_var:global.X,true`. They persist with save data and gate progression.

### Live Edit/Play Flow

- **Toggle is instant** — shared world state, no reload delay.
- **Edit mode**: game paused, free camera, select and modify attributes. Changes are visible immediately — move a collision box and see it update live.
- **Play mode**: game runs with current state. Changes made in edit mode are in effect.
- **Play-from-here**: resume without resetting state (test a specific scenario mid-action).
- **Restart level**: reload from gamedata, reset all runtime state.
- **Attribute watcher**: pin attributes to the debug overlay — see them update in real time during play. Pick entities, pick attributes, they display as a live HUD.

## Multiplayer

### Network Model

- **LAN-based** — optimized for couch co-op on the same WiFi/subnet. Low latency assumed.
- **Host-authoritative** — one player hosts, others join. The host runs the simulation. Clients send inputs, receive state.
- **UDP** for frequent state updates (positions, attribute changes). Reliable channel (TCP or reliable UDP) for important events (rule triggers, item pickups, editor operations).
- **LAN discovery** — host broadcasts presence on the local network. Clients see available games and join without entering an IP address.

### Player Model

- Each player has their own device, own screen, own camera.
- **No player cap** — protocol designed for N players, optimized for 2–4.
- Players can be in the same level or different levels simultaneously.
- Each player entity is **owned by its player** — only that player's inputs move it.
- NPCs and world entities are owned by the host.

### Input Abstraction

- Entity attribute `input_source` identifies where input comes from: `local:0` (gamepad 0), `local:1`, `network:player_id`.
- The player behavior reads from the input source, not a hardcoded gamepad index.
- Adding a new player = spawn a player entity with a new input source. No code changes needed.

### State Sync

- Host sends **attribute deltas** (only what changed since last tick) to all clients.
- Clients send their **input state** each tick to host.
- On join: host sends a **full state snapshot**, client catches up.
- Entity composition trees sync as a unit — parent + all tagged children.
- Rules execute on the host only — clients see the results via attribute deltas.

### Camera

- Each client runs its own camera following its own player entity.
- Camera logic is entirely local — not synced over the network.
- All clients render from the same world state but from their own viewpoint.

### Collaborative Editor

- **Multiple cursors** — each player has their own cursor, colored and labeled with their name.
- **Entity locking** — selecting an entity claims it. Others see it highlighted in the owner's color and cannot modify it until released.
- **Live sync** — edit operations are synced as they happen. All players see changes in real time.
- **Per-player undo** — each player has their own snapshot stack. Undoing only affects that player's changes.
- **Save is host-only** — the host writes `gamedata.toml`. Any player can request a save, but the host executes it.
- All editor widgets (radial picker, word builder, fuzzy finder) work identically for every player.

## Save System

Implemented in S6.15 (slices a-e) per D33 — this section describes the singleplayer system that actually shipped, superseding an earlier multiplayer-era sketch. Multiplayer save sharding (per-player character sections, host-owned saves) is deferred to Stage 8 and is not built; see the Multiplayer section above for the future design, which this section no longer duplicates.

### Two Files, Two Concerns

- **`gamedata.toml`** = the game (level design, blueprints, rules). Shared via Syncthing, versioned in git. Never contains player progress.
- **Save file** = process-lifetime progression (set flags, global vars, item counts) plus a per-level "entity delta" for every level the player has visited (position, instance-attr overrides, active/inactive) — see `SaveState` (`engine/src/save.h`) and `ProgressionState` (`engine/src/progression.h`).

### Storage Location

Save files live in the OS-conventional per-user data directory, resolved by `platform_saves_dir` (`engine/src/platform_paths.c`) — app-local storage, **not** Syncthing (unlike `gamedata.toml`/`keybindings.toml`, which sync to Android through the shared Syncthing folder):

- Linux/BSD: `$XDG_DATA_HOME/sleipner/saves/` (fallback `$HOME/.local/share/sleipner/saves/`)
- Windows: `%APPDATA%/sleipner/saves/`
- Android: `<internalDataPath>/saves/` (raylib `GetApplicationDirectory()`, already app-scoped, no extra `sleipner/` segment)

`platform_ensure_saves_dir` creates the directory (mkdir -p semantics) before every write, so a first-run save doesn't need the directory to pre-exist.

### The Delta Layer Doubles as Cross-Transition Persistence

`progression_capture_level_delta` (`engine/src/progression.c`) snapshots every entity in the level being left — position, instance attrs (an entity's instance `AttrSet` already IS its delta against blueprint defaults, per "Entity–blueprint connection" under Memory Architecture below), and an explicit `active` flag — into `ProgressionState.level_deltas`, a `Strv -> LevelDelta` map keyed by level name; `progression_apply_level_delta` re-applies a level's stored delta onto its freshly re-parsed entities when the level is (re)loaded. Both are wired into `frame.c`'s `run_transition_swap` (capture before the reload, apply after), so within a level the player has actually visited: kill an enemy, it stays dead; open a chest, it stays open; move an entity, it stays moved. `level_deltas` lives in `progression_arena`, the same process-lifetime arena as flags/vars/items — never rewound by a level transition or hot-reload. It is wiped wholesale only by pause-menu RESTORE or a fresh game (`game_reset_progression`, which zeroes flags/vars/items/level_deltas together), or narrowly cleared on hot-reload alone (`progression_clear_level_deltas`, since the gamedata just changed underneath it and any captured delta is stale, while flags/vars/items are deliberately left intact).

A save captures exactly this state: `save_write` runs `progression_capture_level_delta` for the CURRENT level first (so the player's own position and any just-made changes are folded in), then serializes the whole `ProgressionState` — every visited level's stored delta, not just the current one. `save_load` deserializes straight into `progression_arena`, loads the saved `current_level`, and re-applies that level's delta exactly the way a transition would.

Known limitations (see TODO.md's "Level transition follow-ups" section for the full write-ups, not duplicated here):
- Entities spawned at runtime (`spawn:`, S6.6) get session-only ids and are never matched again after a reload — spawned entities do not persist across transitions or saves.
- There is no dedicated cross-level "player" section the way an earlier multiplayer-era sketch of this format had — the player's own state rides inside whichever level's entity delta it occupies at save time, which is correct for a save taken mid-level but gives the player no home independent of the level it happens to be standing in. Revisit once multiplayer forces the question.
- `save_load` does not pre-seed overlap tracking (`prev_player_overlaps`) after restoring the player's position, unlike a transition's spawn-in step — a save taken while standing inside a trigger's region could refire that trigger immediately after loading.

### Save Format

TOML, parsed with the same `tomlc99` vendor library and written by the same kind of hand-rolled emitter as `gamedata.toml` (`toml_emit_save`, `engine/src/toml_emitter.c`). A `[save]` header carries the format version, the current level name, and the set-flag list; `[save.vars]`/`[save.items]` are name -> value / name -> count tables, omitted entirely when empty; one `[[level_delta]]` array-of-tables entry exists per level with a captured delta, each carrying a `name` and a `[[level_delta.entity]]` sub-array of per-entity deltas (`id`, `pos`, `active`, plus any custom instance attrs). `SAVE_FORMAT_VERSION` (currently `1`, `save.h`) is checked on load; an unrecognized version fails cleanly with an error rather than guessing at a migration.

Representative output for a save with two set flags, one var of each typed kind, two stacked items, and captured deltas in two visited levels (field names and nesting verified against `toml_emit_save`/`save_test.c`; `[save.vars]`, `[save.items]`, and `[[level_delta]]` are each simply absent when there's nothing to emit):

```toml
[save]
version = 1
current_level = "field"
flags = ["door_open", "met_wizard"]

[save.vars]
gold = 42
speed_multiplier = 1.5
has_key = true
hero_name = "Zelda"

[save.items]
sword = 1
potion = 3

[[level_delta]]
name = "field"

[[level_delta.entity]]
id = 5
pos = [120.5, 80.25]
active = true
opened = true

[[level_delta.entity]]
id = 7
pos = [40.0, 40.0]
active = false

[[level_delta]]
name = "interior"

[[level_delta.entity]]
id = 12
pos = [200.0, 150.0]
active = true
health = 3
```

### Loading

`save_load` (`engine/src/save.c`): read the file, `save_deserialize` it into `progression_arena`, load the saved `current_level` via the caller's level loader, re-apply that level's delta onto the freshly parsed entities (this restores the player's position too, since apply never special-cases the player's entity id), snap the camera, and reset undo history to a fresh baseline. A missing file or a parse failure leaves `state` untouched; a saved level name no longer present in the current `gamedata.toml` also fails without committing the parsed progression, though `state->gamedata` may already reflect a partial reload attempt in that one case, same as any other reload failure.

### Save Slots and Autosave

`SAVE_SLOT_COUNT` (currently 3) numbered files, `save_1.toml` through `save_3.toml`, plus a separate `autosave.toml` that manual saves never overwrite (the pause-menu Save UI only ever targets a numbered slot). `save_write` backs up an existing file to `<path>.bak` (`file_backup.h`'s `backup_file`) before overwriting it. Autosave (`save_autosave`) fires on every level transition (`frame.c`'s `run_transition_swap`, right after the swap and its own undo baseline) and on quit (`main.c`, right after the game loop exits); both call sites treat a failed autosave as non-fatal — log the error and continue, never abort a transition or block shutdown.

### Pause-Menu Save/Load UI

The pause menu's "SAVE GAME" / "LOAD GAME" entries (`MENU_ENTRY_SAVE_GAME`/`MENU_ENTRY_LOAD_GAME`, `menu.h`) open a modal slot picker (`SaveScreen`, `engine/src/save_screen.h`/`.c`) listing the numbered slots plus the autosave file (shown in LOAD mode only), each marked filled or empty from a `FileExists` probe taken when the picker opens. Confirming a slot calls `save_write`/`save_load` and returns straight to active play rather than back to the pause menu; a failure (bad directory, a failed write/load, or confirming an empty LOAD slot) leaves the picker open with a toast (`Saved`/`Save failed`/`Loaded`/`Load failed`/`Empty`).

**This is a separate mechanism from the editor's gamedata Save/Restore.** The pause menu's own "SAVE" / "RESTORE" entries (`MENU_ENTRY_SAVE`/`MENU_ENTRY_RESTORE`) write or discard changes to `data/gamedata.toml` itself — the level design, not player progress — via `main.c`'s `save_gamedata`/`menu_dispatch_restore`. The two pairs of menu entries sit next to each other in the same list but touch entirely different files and have entirely different call paths.

## Data Architecture

Game data is split into two concerns with separate storage:

### Asset Database (`assets/`)

Binary resources: sprites, music, sound effects. These rarely change, are large, and don't diff well. Checked into git as-is. Embedded in the binary via `.incbin` and loaded at runtime from memory via raylib's `Load*FromMemory` functions.

```
assets/
├── sprites/       # .png sprite sheets and tiles
├── music/         # .mp3/.ogg background tracks
└── sfx/           # sound effects
```

### Game Data (`data/gamedata.toml`)

All game data — blueprints, levels, tile palettes — lives in a **single file**. This is the creative output that the editor reads and writes. Must be:

- **Git-friendly:** Plain text, line-oriented, diffs show meaningful changes.
- **Live-editable:** The game reads/writes this file at runtime.
- **Synced:** Kept in sync with the Syncthing copy via explicit `cp`.

### Syncthing Pipeline

Two separate copies exist — hard links do not work because Syncthing's atomic write
(temp file + rename) breaks them:

- **Repo:** `data/gamedata.toml` — versioned in git, read by the desktop game.
- **Syncthing:** `~/Sync/sleipner/gamedata.toml` — synced to Android, read by the Android game.

Kept in sync by explicit copy (see CLAUDE.md "Gamedata Sync Workflow" for the full procedure).

**Desktop default:** Game reads `<data_dir>/gamedata.toml`, where `data_dir` defaults to `data/`.
**Android default:** `data_dir = /storage/emulated/0/Sync/sleipner/`.

`data_dir` is a runtime preference, overridable via the Settings → General tab. The same `data_dir` controls `keybindings.toml` and `trace.log`. See "Preferences and Path Configuration" below for the full hierarchy and the path picker UX.

### Game Data Format (TOML)

TOML via [tomlc99](https://github.com/cktan/tomlc99) (vendored at `engine/vendor/tomlc99/`). Human-readable, supports comments, clean diffs — each `[[blueprint]]` or `[[level.entity]]` block is a self-contained addition/removal in git.

```toml
# === Blueprints ===

[[blueprint]]
name = "tree"
texture = "tree.png"
src = [0, 0, 64, 80]
collision_offset = [20, 60]
collision_size = [24, 16]
behavior = "static"

[[blueprint]]
name = "chest"
texture = "chest.png"
src = [0, 0, 16, 16]
collision_offset = [0, 0]
collision_size = [16, 16]
behavior = "chest"

[[blueprint]]
name = "house"
texture = "house.png"
src = [0, 0, 96, 128]
collision_offset = [0, 64]
collision_size = [96, 64]
behavior = "static"

[[blueprint]]
name = "fence_v"
texture = "fence.png"
src = [0, 0, 16, 48]
collision_offset = [0, 0]
collision_size = [16, 48]
behavior = "static"

[[blueprint]]
name = "player"
texture = "player.png"
src = [0, 0, 32, 32]
collision_offset = [11, 22]
collision_size = [10, 10]
behavior = "player"
speed = 80.0
animation = { frames = 6, size = 32, speed = 10, row = 0 }

# === Levels ===

[[level]]
name = "overworld"
size = [640, 360]
music = "bgm.mp3"

[[level.entity]]
blueprint = "house"
pos = [40, 20]

[[level.entity]]
blueprint = "tree"
pos = [200, 60]

[[level.entity]]
blueprint = "tree"
pos = [350, 150]

[[level.entity]]
blueprint = "chest"
pos = [300, 100]

[[level.entity]]
blueprint = "player"
pos = [320, 180]
```

### Runtime Loading

- On startup, the game reads `gamedata.toml` via tomlc99.
- `[[blueprint]]` entries are registered in a lookup table (name → component defaults).
- The active `[[level]]` is selected and its `[[level.entity]]` entries are instantiated from their blueprints. Per-instance fields override blueprint defaults.
- The editor modifies in-memory state and writes the entire file back on save via a TOML serializer (tomlc99 is read-only, so we write a simple emitter ourselves).
- Hot-reload: optionally watch the file's mtime and reload when it changes (useful when editing on phone while running on desktop, or vice versa).

## Memory Architecture

All engine memory is arena-backed — no `malloc`/`free` anywhere except tomlc99 vendor internals and the allocator infrastructure's `NULL` fallback. The two arenas in `GameState` have distinct lifetimes, and data is loaded incrementally in layers.

### Data Lifecycle

```
1. Compile time
   Asset bytes (PNG, TTF, MP3) embedded in .rodata via .incbin.
   Zero runtime I/O for assets — they're part of the binary.

2. Startup — game_init()
   Three arenas allocated via mmap(MAP_ANONYMOUS|MAP_NORESERVE).
   1 TiB virtual reservation each; physical pages demand-paged.
   All start empty.

3. Asset registration — once per process
   Raylib decodes raw bytes → GPU VRAM (not in arena).
   Small registry structs (TextureEntry, FontPreviewEntry) pushed
   into gamedata_arena via arena allocator.
   gamedata_base checkpoint saved — marks the floor of this zone.

4. Gamedata load — game_load_gamedata()
   File read into scratch buffer, TOML parsed (tomlc99 uses its
   own heap — vendor exception). Blueprints, level, rules stored
   in gamedata_arena above gamedata_base. tomlc99 tables freed.

5. Game loop — update/render cycle
   Scratch allocations via SCRATCH_SCOPE(&state.scratch_arena).
   Offset auto-restored at scope exit; no syscall, bare rewind.

6. Hot-reload / level transition
   arena_restore(&state->gamedata_arena, state->gamedata_base)
   rewinds gamedata_arena to the checkpoint. Asset registry at the
   bottom is untouched. progression_arena is a separate arena and is
   never touched by this restore, so flags and global vars (rule
   `set_flag:` / `set_var:global.*`) survive both transitions and
   hot-reloads. Step 4 runs again.

7. Shutdown — game_free()
   arena_free() munmaps all three arenas (returns virtual range to OS).
   Raylib unloads GPU textures and fonts.
```

### Arena Zone Layout

```
gamedata_arena:
  [0 .. gamedata_base)    textures + fonts (survive all reloads)
  [gamedata_base .. top)  blueprints, level, rules (rewound on reload)

scratch_arena:
  [checkpoint .. top)     per-scope temporaries (rewound at SCRATCH_SCOPE exit)

progression_arena:
  [0 .. top)              flags, global vars (ProgressionState) — process
                          lifetime, untouched by gamedata_arena's
                          arena_restore. Cleared only by the pause-menu
                          RESTORE action (game_reset_progression), which
                          is the one place outside game_free that calls
                          arena_reset on a live arena.
```

### Entity–blueprint connection

An entity is an instance of a blueprint. The blueprint defines default attribute values; the
entity holds runtime overrides. Attribute lookup is a two-level operation: check instance
attrs first, fall back to blueprint defaults. This two-level scoping is a deliberate design
choice — it preserves the distinction between "what the blueprint says" and "what happened at
runtime," which is essential for hot-reload migration and the editor's separate
instance/blueprint display.

**Do not copy blueprint defaults into instance attrs.** This was tried and reverted. Copying
destroys the override/default distinction, wastes arena memory for attrs that are never
overridden, and makes hot-reload migration impossible (can't tell which instance attrs are
runtime overrides vs copies of defaults).

**Implementation.** Entity is a pure runtime data bag with no stored blueprint pointer:

1. **External map for entity→blueprint tracking.** `map_int_str entity_blueprints` in
   `GameState` maps each entity ID to its blueprint name. Populated during level loading /
   entity spawning. This is the single source of truth for which blueprint an entity came
   from.

2. **Two-level lookup at AttrSet level.** `attr_get_scoped` in `attribute.h` is the
   primitive (NULL-safe — returns instance-only when defaults is NULL). Typed wrappers
   (`attr_get_scoped_float`, `attr_get_scoped_int`, `attr_get_scoped_bool`,
   `attr_get_scoped_string`) include int↔float coercion to match the old `entity_get_*`
   behavior.

3. **Resolution chain.** `entity_resolve_defaults(state, entity_id)` in `game.h` resolves
   the full chain:

   ```
   entity.id → map_int_str_get → blueprint_name → blueprint_find → &bp->attrs
                                                                        ↓
                  attr_get_scoped_float(&entity->attrs, defaults, "speed", 0.0F)
   ```

4. **Pre-resolved defaults for rule evaluation.** `ConditionContext` and `ActionContext`
   carry a `const AttrSet *const *entity_defaults` array (parallel to the entities array),
   built on `scratch_arena` at both `rules_evaluate_batch` call sites. Rule code receives
   pure AttrSet pointers — no blueprint dependency, no circular includes.

   **Planned replacement:** The parallel array couples rule contexts to vec indices and
   forces pointer subtraction to map between entities and their defaults. Replace with one
   of the cross-module dependency patterns described below (decompose at boundary or
   callback resolver) — see "Cross-Module Dependencies" under Memory Architecture.

5. **Hot-reload.** The map stores names (strings). After gamedata reload, names resolve
   against the fresh `BlueprintTable`. Instance attrs are untouched. New blueprint defaults
   apply automatically because the next `attr_get_scoped` call resolves against the
   reloaded blueprint.

**What Entity keeps.** `AttrSet attrs` (instance overrides), `Str blueprint_name` (for
debug display and editor — future work: move to the external map), `Texture2D *texture`,
`Str tag`, `int id`, position/collision/animation fields. No `defaults` pointer.

**Solid auto-derive.** Moved from `entity_init` to `level.c` (the 3 entity creation sites).
If the blueprint does not explicitly set "solid", it is derived from collision size. Tests
that need "solid" set it explicitly via `attr_set_bool`.

### Cross-Module Dependencies

Lower-level modules (rule.c, entity.c) sometimes need data or behavior that lives in
higher-level modules (game.c, blueprint.c). Including the higher-level header would create a
circular dependency. Three patterns for breaking these cycles, chosen per-case:

**Decompose at the boundary.** The higher-level module unpacks its state into a plain struct
that the lower-level module can accept without knowing the source. game.c builds an enriched
view (e.g. `EntityView { Entity *entity; const AttrSet *defaults; }`) and passes an array of
those down. Resolution happens once at the call boundary; the lower module just reads flat
data. This is the preferred approach when the set of data is known up front.

**Callback / ops struct.** A struct containing a function pointer plus `void *` state, defined at the lower level and implemented by the higher level. Lower module calls through the pointer without knowing the concrete state type. Same shape as Linux's `struct file_operations` and this codebase's existing `Allocator`. Use when resolution is lazy or the input set isn't known up front.

**Handle / lookup key.** The object stores an ID or name that a lookup function translates
into a pointer. Already used for entity→blueprint (`blueprint_name` string) and
entity→parent (`parent_index` integer). Only appropriate when the association is intrinsic to
the type — every instance has one. If some instances would carry a null/empty handle, prefer
decompose-at-boundary or callback instead.

**What we never do:** Forward-declare the higher-level struct (`struct GameState;`) in the
lower-level header. This hides the cycle rather than fixing it.

### Key Rules

- `arena_restore` is the only lifecycle operation called at runtime on `gamedata_arena` — a bare pointer rewind, no syscall.
- `arena_reset` (calls `MADV_DONTNEED`) is called at full teardown in `game_free`, and additionally on `progression_arena` alone by `game_reset_progression` (pause-menu RESTORE) — a deliberate "discard progress" reset, not a reload.
- **Never** call `arena_reset` on `gamedata_arena` from game code — it would wipe the texture registry, causing a black screen.
- The asset registry (textures, fonts) uses a dynamic vec backed by the arena. Adding a new asset is one line in `main.c`; no fixed-size arrays to resize.
- **Prefer `vec` over fixed-size arrays with `MAX_*` constants.** If a collection's size isn't known at compile time, use a `vec` backed by the appropriate arena. `MAX_*` constants invite off-by-one bugs, silent truncation, and inflexibility — the arena makes dynamic sizing essentially free.

### Arena growth strategy

The rule that decides whether an allocation belongs in `gamedata_arena` is about
**growth shape**, not absolute size. Allocations whose count grows with frame count `n`
(one per frame, one per game tick) are forbidden — they leak forever and the arena grows
without bound. Allocations whose count is bounded by user actions or events (settings
saves, level loads, blueprint reloads, occasional editor commits) are explicitly fine
even when they orphan a previous block, because total leaked bytes are
`<events> * <size per event>` — kilobytes at most over a session.

Before adding a new dedicated `Arena` field on `GameState`, ask: does this allocation's
count grow with frame count, or is it bounded by user/event count? If bounded, allocate
against `gamedata_arena`. If frame-count-proportional, the issue is the data flow itself
and a new arena is not the right fix either.

`Preferences.data_dir` is the worked example. It lives in `gamedata_arena`. The Settings
UI commits via `str_clear` followed by `str_append_cstr`, which reuses the existing
buffer when the new value fits inside the current capacity. Only an unprecedentedly long
path triggers an arena bump, and even then the orphaned block is bounded by one per
session per longest-yet path. No dedicated `prefs_arena` is justified.

### Vec Growth and Pointer Stability

When a vec backed by an arena needs to grow, the arena allocator checks whether the vec's backing
array is the **topmost allocation**. If yes, it extends in place — no copy, no pointer change. If
anything else was allocated after it, a new block is allocated at the top, the data is copied, and
the old block is **left orphaned in the arena** (valid memory, but unreachable). The vec struct's
`data` pointer is always updated to the new block, so code that accesses through the vec struct is
correct. Code that cached `&vec.data[i]` before the push now holds a stale pointer.

**Rule: never hold a pointer to a vec element across a push to that vec.** Always re-derive via
`vec.data[i]` after any push that could trigger growth.

#### Two-dimensional growth: map of vecs

A `map<K, vec_V>` grows in two independent dimensions:

- **Map growth (rehash):** when the map exceeds 75% load, a new bucket array is allocated at the
  arena top, entries are copied by value (including embedded vec structs — their `data` pointers
  are preserved), and the old bucket array is orphaned. Any `vec_V *` pointer previously returned
  by `map_get` is now stale.

- **Vec growth within a map entry:** a pointer returned by `map_get` points into the map's bucket
  array. Calling `vec_push` through that pointer updates the vec struct in-place — fine as long as
  the map doesn't rehash concurrently.

The two failure modes are independent: a `vec` pointer obtained from `map_get` becomes stale on the next `map_set` that rehashes. The safe pattern is fresh lookup every time (`vec_push(map_get(&groups, "enemies"), …)`) — never cache the inner vec pointer across an outer `map_set`.

#### Mitigations

**Pointer stability via indirection.** Store `vec_int *` (pointer) in the map rather than
`vec_int` (value). Each vec is its own separate arena allocation. Map rehash copies the pointer,
not the vec — the vec stays at a stable address regardless of map growth.

**Two-phase protocol.** Finish all map growth at load time; only push to existing vecs at runtime.
No new keys at runtime means no rehash, so `map_get` pointers remain valid for the duration of a
game frame. This is the correct pattern for named entity groups: groups are defined in TOML at load
time (map frozen), entities are added/removed at runtime (vec push only).

**Index-only access.** Never cache a pointer across a push. Always call `map_get` immediately
before use within a single expression.

#### Named entity group store

The group store (`map<string, vec_int *>`) follows the two-phase protocol:

- Load time: all group names defined (map grows to final size, then frozen). Each group's `vec_int`
  is individually allocated in `gamedata_arena` and pointed to by the map entry.
- Runtime: `add_to_group:X` / `remove_from_group:X` push/pop entity indices into the group's vec.
  No map rehash, no stale pointers.

Growth of a group vec when it is not the topmost arena allocation leaks the old backing block.
This is bounded (one leaked backing per growth event) and reclaimed on level reload via
`arena_restore`.

## Undo System

The editor uses snapshot-based undo/redo. Each editor operation is undoable via a full
gamedata snapshot — no inverse operations needed. The system is implemented in `undo.h` /
`undo.c`.

### Architecture

**Snapshot model.** Each `UndoEntry` captures three things:

1. **`GamedataState` struct copy** — `memcpy` of the typed struct. Captures vec/map headers
   (data pointer, count, capacity), ints, Vector2, etc.
2. **Arena contents copy** — `memcpy` of the gamedata arena bytes from `gamedata_base` to
   the current offset. This is the actual entity data, attribute values, strings, rules —
   everything editor operations can mutate in place.
3. **`arena_data_size`** — bytes of arena content stored. On restore, the arena checkpoint
   is derived: `gamedata_base + arena_data_size`.

**Why arena contents must be copied.** Editor operations like drag and attribute edit modify
data in place within the arena — the vec `.data` pointers don't change, but the bytes at
those addresses do. Without copying the arena contents, restoring the `GamedataState` struct
headers alone would leave mutated bytes in the arena.

**Restore** (undo/redo):
1. `memcpy` saved arena bytes back to `arena_ptr_at(&gamedata_arena, gamedata_base)`
2. `arena_restore(&gamedata_arena, gamedata_base + entry->arena_data_size)`
3. `*gamedata = entry->gamedata_copy`

All vec/map `.data` pointers in the restored struct point into the arena at the same offsets
where the original bytes have been restored — everything is consistent.

### Storage

A dedicated undo arena (mmap, same as gamedata/scratch) holds a doubly-linked list of
`UndoEntry` nodes. Each entry is allocated as `sizeof(UndoEntry) + arena_data_size` bytes —
the fixed struct followed by the variable-length arena snapshot. A new edit after undo
truncates the redo tail by rewinding the undo arena to the current entry's end, reclaiming
memory.

### Snapshot Timing (push-after model)

Entries capture **completed states**, not pre-mutation states. An initial baseline entry
is pushed after game load / hot-reload / level transition. This gives undo a "before any
edits" state to restore to.

**Multi-frame operations** (drag, handles, attr_edit, word_builder) push at confirm (after
the mutation is complete). Cancel restores from `EditorState` saved values — no undo entry
is involved.

**Single-frame operations** (toggle, delete, spawn) push immediately after the mutation.

### Controls and UI

- D-pad left = undo, D-pad right = redo (editor BROWSE mode only)
- Toast at top-center shows the operation description (e.g. "Undo: Move entity"), fades
  after 2 seconds
- `[*]` dirty indicator in the hints bar when the current state differs from the last save

### Lifecycle Integration

- After hot-reload (`poll_hot_reload` returns true): `undo_history_clear` + baseline `new_entry`
- After level transition: `undo_history_clear` + baseline `new_entry`
- After initial `load_gamedata`: baseline `new_entry`
- After successful save: `undo_history_mark_saved`

### Undo Safety Rules

Rules that prevent memory corruption when modifying the codebase:

1. **All `GamedataState` pointers must point into `gamedata_arena`.** Undo restores both
   the struct AND the arena contents — any pointer to heap, scratch, or stack becomes
   dangling after restore. This is the universal safety rule; all pointers (vec `.data`, map
   internals, `Str` data) are safe because their target bytes are restored along with the
   struct.

2. **Never add `malloc`/heap fields to types inside `GamedataState`.** Use `Str`
   (arena-backed), `vec`/`map` with `allocator_arena`.

3. **`Entity.texture` pointers point below `gamedata_base` and survive all rewinds.** New
   asset types must also load below `gamedata_base`.

4. **New arena-backed fields belong in `GamedataState`.** Fields outside the sub-struct are
   NOT snapshotted by undo.

5. **Any code path calling `game_load_gamedata` must also call `undo_history_clear`.** Hot-
   reload and level transitions invalidate all snapshots.

6. **Snapshot after mutating.** Multi-frame ops call `undo_history_new_entry` at confirm.
   Single-frame ops call it inline after the mutation. A baseline entry is pushed at load.

7. **`EditorState` is NOT snapshotted.** Don't store undo-critical data there.

8. **`game_update` mutations are gated by `!editor_mode`.** Don't add editor-mode mutations
   to `game_update`.

9. **Vec growth invalidates pointers.** Never hold a pointer across a push — doubly
   critical for undo since snapshots capture `.data` pointers.

## Bug Investigation Discipline

Three rules, design-level statement of intent. CLAUDE.md § *Bug Investigation Discipline* carries the operational workflow.

1. **Every bug report starts with a failing integration test.** That test is the bug specification — exact state, inputs, assertion — and doubles as the regression guard once the fix lands. No diagnostic logging, hypothesizing, or fix proposals before it fails for the reason the user reported.
2. **User reports are authoritative.** Don't hypothesize "user error" without asking. Visible side effects the user would have mentioned (toasts, sounds, overlay text) but didn't are strong negative evidence against a hypothesis.
3. **Bug-repro tests drive the game as a black box.** Inputs go through the real input layer (synthetic `InputState`, raised through the top-level frame entry point); outputs are observable game state (positions, level name, attribute values, toast text). Internal symbols (`undo_history_*`, `gamedata_arena_*`, `handle_*_input`) in the test body are a smell. Refactors that preserve behavior must keep the test green; fixes in the wrong layer must keep it red.

See § *Test ergonomics for black-box integration testing* below for the infrastructure that makes rule 3 ergonomic.

## Input Architecture

All gameplay, editor, menu, and widget code reads input through a high-level
**function layer** (`engine/src/input_func.h`). Physical inputs (keys,
gamepad buttons, sticks, triggers) map to action and axis enums via a
`BindingStore` loaded once at startup. Game code calls
`input_pressed(in, store, ACTION_EDITOR_UNDO)` or
`input_axis_pair(in, store, AXIS_PRIMARY_X, AXIS_PRIMARY_Y)` and never
touches raylib's input API directly.

The action enum is a hybrid: shared verbs (`ACTION_CONFIRM`, `ACTION_NAV_*`,
`ACTION_PAGE_*`) are reused across handlers; context-specific actions
(`ACTION_EDITOR_OPEN_BLUEPRINTS`, `ACTION_ATTR_INC_100`,
`ACTION_BLUEPRINT_DUPLICATE`) get a context prefix. Multiple handlers can
check the same action — context is implicit in "who's currently running",
not a central registry.

A binding is a list of "physical inputs"; each physical input is a list of
one or more atoms (key, gamepad button, axis, trigger, keyboard-synth
axis). A 1-atom physical input is a single key; a 2+-atom physical input is
a chord (all atoms must be held, one freshly pressed to fire). Chord and
single-key bindings share one evaluation path. See CLAUDE.md § "Input
Function Layer" for the API surface and test pattern.

The function layer is a binding lookup, not a priority resolver. If a chord
shares its trigger key with a single-key binding for a different action,
the caller is responsible for checking the chord first and early-returning.

## Test ergonomics for black-box integration testing

The function-layer overhaul (2026-04) unblocked black-box bug-repro tests: every binding site reads from an `InputState` snapshot via `input_pressed` / `input_axis` — no raylib globals.

**Public frame entry point.** `engine/src/frame.h` exposes `frame_update` + `FrameContext`. Production `main.c` builds the context once per loop iteration; headless tests build a `TestGame` fixture (`engine/test/test_helpers.h`) around the same fields and drive `test_advance_frame` / `test_advance_frames` through the same dispatcher. Render, audio, gamepad polling, hot-reload, gamedata file I/O, and transition handling stay in `main.c` (production-only). Save / restore handlers reach the menu via `MenuSaveFn` / `MenuRestoreFn` function pointers on `MenuDispatchCtx`; tests that don't care about SAVE/RESTORE pass `nullptr` so they close the menu without touching disk. Tests that need to observe RESTORE's actual effect (e.g. progression being cleared) wire `test_restore_fn` (`engine/test/test_helpers.c`) instead — it mirrors `main.c`'s `menu_dispatch_restore` (reload from the fixture's in-memory TOML + `game_reset_progression`) since `main.c` itself is not linked into the test binary. Hot-reload has the same shape: `test_trigger_hot_reload` mirrors `poll_hot_reload`'s `game_load_gamedata` call, skipping only the disk-mtime check.

**Black-box pattern.** Build a `TestGame` with `test_game_setup`, construct `InputState` values via the `input_state_*` helpers, drive `test_advance_frames`, then assert on observable game state. The fixture struct grows over time as new top-level state appears in `main.c`; new sane defaults go in `test_game_setup`.

## Roadmap

### Phase 1 — Foundation (DONE)
- [x] Tiled grass background
- [x] Player avatar with animation and gamepad control
- [x] Static obstacles with AABB collision (hardcoded in main.c)
- [x] Depth-sorted rendering (by entity position.y, tiebroken by x then insertion index)
- [x] Background music (embedded mp3)
- [x] Debug overlay (toggled from the pause menu — collision boxes, info panel, scrolling log)
- [x] Android APK build (signed, sensorLandscape, 1920x1080)
- [x] GitHub Actions CI (format, build, test, lint + Android APK artifact)

### Phase 2 — Data Pipeline (IN PROGRESS)
- [x] Create `data/gamedata.toml` with blueprint and level data
- [x] Vendor tomlc99 in `engine/vendor/tomlc99/`
- [x] Parse and log gamedata.toml on startup
- [x] Platform-conditional gamedata path (repo-relative on desktop, Syncthing on Android)
- [x] Retry gamedata load until Android storage permission is granted
- [x] Headless engine mode (init without window/audio for testing)
- [x] Split game loop into update (pure logic) and render (side effects)
- [x] Integration test harness (load gamedata, feed inputs, assert state)
- [x] Arena allocator for gamedata memory
- [x] Load blueprints into blueprint lookup table
- [x] Instantiate level entities from gamedata (replace hardcoded obstacles)
- [x] Remove hardcoded obstacle data from main.c
- [x] Hot-reload: poll mtime in play mode, reload on change
- [x] TOML emitter for editor save

### Phase 3 — Entity System
- [x] Attribute system (built-in + custom, typed key-value pairs)
- [x] Blueprint/instance scoping (instance overrides, blueprint fallback)
- [x] Blueprint inheritance (extends)
- [x] Entity storage (flat array in arena)
- [x] Convert Player and Obstacle to entity instances with attributes
- [x] Entity composition (children with relative positioning)
- [x] Tag system (named references within composition trees)

### Phase 4 — Rule Engine
- [x] Rule struct (trigger + conditions + action tree)
- [x] Trigger evaluation (interact, enter, event, on_spawn, attr_changed — core triggers)
- [x] Condition evaluation (flag, not_flag, attr, not_attr, attr comparisons, has_item stub, var stub)
- [x] Action execution (set_flag, clear_flag, set_attr, add_attr, toggle_attr, destroy, fire_event — stubs for rest)
- [x] TOML parsing of [[blueprint.rule]] entries
- [x] Evaluation loop with event cascading (max 8 rounds)
- [x] Interact trigger detection (edge-triggered A-button, proximity-based)
- [x] Flag storage (`FlagSet` on `GameState.progression`, a dedicated process-lifetime arena so flags survive level transitions and hot-reloads — see Memory Architecture § Arena Zone Layout)
- [x] Custom events (fire_event / event trigger, cross-entity decoupling)
- [x] Control flow nodes (if/else, repeat)
- [x] for-each control flow node (entity queries)
- [x] Variable system (local per-execution, global persistent, $ references in parameters)
- [x] Subroutines (named reusable action sequences, callable via `call:`)
- [x] Timer management (create, destroy named timers)
- [x] Enter/on_spawn trigger detection (AABB overlap — Phase 9 wires in composable shapes)
- [x] Remaining triggers (collide, defeat, timer, timer_periodic, on_destroy)

### Phase 5 — Editor Mode
- [x] Toggle play/editor mode (instant, shared world state)
- [x] On-screen button hints (context-sensitive, always visible)
- [x] Free camera with cursor
- [x] Save to gamedata.toml (TOML emitter, atomic write)
- [x] Browse mode: select entities, inspect attributes
- [x] Edit mode: move entities, resize collision boxes with visual handles
- [x] Scene mode: place/move/delete entities, inspect properties
- [x] Blueprint mode: create/edit/duplicate/delete blueprints, edit attributes, manage children
- [x] Tile system engine side: Level ground/overlay tile layers, gamedata tileset (id -> texture+src), TOML row-array parse/emit, ground/overlay render with flat-fill fallback for tile-less levels (S5.3a)
- [x] Tile mode: paint ground and overlay layers, tile palette (S5.3b)
- [x] Atlas system engine side: `[[atlas.region]]` named texture regions (name/texture/src), gamedata atlas registry, blueprint `sprite = "name"` resolution to src_*/texture attrs (sprite wins over a raw src/texture on the same blueprint), TOML parse/emit round-trip (S5.4a, D37)
- [x] Atlas mode: browse the texture registry, list/create named regions per texture, drag-set a region's src rect via HANDLES-style dual-stick offset/size (`EDITOR_SUB_ATLAS_BROWSE`/`_REGION_EDIT`, `editor/atlas.c`) (S5.4b)
- [x] Animation mode: pick a blueprint (list picker, or the already-selected scene entity's blueprint), edit `anim_frames`/`anim_size`/`anim_speed`/`anim_row` via value adjusters, and scrub frames with a live src-rect preview (`EDITOR_SUB_ANIM_EDIT`/`_FRAMES`, `editor/anim.c`) (S5.5, D20). Linking directional states to the animation state machine is Phase 8 (D31).
- [x] Blueprint `animation = {frames,size,speed,row}` plumbed through parse and emit into `anim_frames`/`anim_size`/`anim_speed`/`anim_row` blueprint attrs, ahead of the mode UI and the animation state machine (Phase 8)
- [x] Rule mode: read-only rule tree view — pick a blueprint (list picker, or the already-selected scene entity's blueprint), list its rules, and browse one rule as a navigable indented tree over the flat action-node pool (trigger, conditions, nested if/else/repeat/for_each actions) (`EDITOR_SUB_RULE_LIST`/`_TREE`, `editor/rule.c`) (S5.6a)
- [x] Rule mode: leaf editing — a focused rule-tree row's trigger/condition/action type and parameters (argument, second_argument, condition compare_value, repeat count, for_each bind) can be edited via the reused radial/fuzzy-finder/word-builder/adjuster submodes, staged and committed atomically with undo (`begin_rule_edit_for_row`/`dispatch_rule_radial_confirm`/`rule_edit_argument_step_complete`/`finalize_rule_edit`, `editor/rule.c`) (S5.6b)
- [x] Rule mode: structural editing — insert a new action node (ACTION_EDITOR_PLACE, reusing S5.6b's type picker for the new node), delete the focused node's subtree (ACTION_EDITOR_DELETE, orphaned in the flat pool rather than compacted), and reorder a node among its siblings (ACTION_EDITOR_MOVE_UP/DOWN chords), all over the S2.3 flat node pool by index with undo (`insert_rule_action_node`/`delete_rule_action_node`/`move_rule_action_node`, `editor/rule.c`) (S5.6c). Reparenting a node into/out of an adjacent if_else's then/else branch is deferred — sibling reorder plus insert/delete already covers authoring if/else bodies.
- [x] Rule mode: subroutine authoring -- the blueprint picker's trailing "Subroutines" row (rule_viewing_subroutines, editor/rule.c) switches EDITOR_SUB_RULE_LIST to a list over gamedata.subroutines (name + top-level action count) with a "+ NEW SUBROUTINE" row (word builder for the name, `create_new_subroutine`, undo-tracked) and ACTION_EDITOR_DELETE removal (`delete_subroutine`, undo-tracked). Each subroutine's action tree is edited through the SAME tree editor as a rule's (EDITOR_SUB_RULE_TREE): `rule_tree_flatten`/`rule_tree_row_count` and every S5.6b/c leaf/structural-edit function were generalized to take a `RuleTreeTarget`/`RuleTreeTargetConst` (editor/internal.h) -- the trigger + conditions + ActionTree a Rule provides, or just the ActionTree a Subroutine has -- resolved per-call from `rule_viewing_subroutines` by `rule_current_target`/`rule_current_target_mut` (editor/rule.c). Rule behavior is unchanged (S5.6a-c tests stay green). "Linking" (referencing an existing subroutine by name from a `call:` action) already worked before this slice -- `fuzzy_finder_collect_subroutine_names` (editor/widgets.c) already surfaced `gamedata.subroutines` as autocomplete candidates once S5.6b's leaf editing shipped. (S5.6d)
- [x] Level mode: view all levels, switch active level in memory (S5.2a)
- [x] Level mode: create levels (word builder name, 640x360 default) and edit the current level's size/floor/background/tint/music detail rows (S5.2b)
- [x] Radial picker widget (generic N-item; Tab/Select opens tool picker)
- [x] Scroll picker widget
- [x] Word builder (seeded vocabulary + blueprint names; builds underscore-separated strings)
- [x] Fuzzy finder for existing names
- [x] Value adjuster with auto-repeat and ±100 step (hold for acceleration)
- [x] Gamepad keyboard (last resort)
- [x] Attribute editor (built-in + custom, with diff view)
- [x] Child entity editor (composition, tags)
- [x] Undo (snapshot-based, arena memcpy)
- [x] Attribute watcher (pin to debug overlay, live values during play)
- [ ] Instance attribute persistence (serialize per-entity attr overrides to TOML)
- [x] Add blueprint attributes from blueprint mode (and from browse mode via scoped editing)
- [x] Editor spatial editing: multi-select (a modifier chord adds the focused entity to a fixed-cap selection set, `EditorState.multiselect_ids`, always seeded with the current single selection), group move (DRAG applies the same per-frame delta to every multiselect entry, resolved by id each frame, not a cached index), copy/paste (blueprint name, position relative to the first copied entity, and persisted attrs snapshotted into a plain-value copy buffer with no arena/heap pointers, cloned via `level_spawn_entity` at the camera position on paste, with `setup_current_level_runtime` rebuilding rule_table/entity_blueprints/overlap-tracking in one call afterward), and a grid-snap toggle (`EditorState.grid_snap`, `editor_snap_to_grid` rounds PLACE's spawn position and DRAG's commit position to the nearest `TILE_SIZE` cell) (S5.7, D38). Completes Stage 5 of the open-work master plan (`work/open-work-master-plan.md`).

### Phase 6 — Multiplayer
- [x] Input source abstraction (decouple player behavior from hardcoded gamepad) -- pulled forward into S6.9a per D39; see the Phase 8 roadmap entry for `input_for_entity`. Only `local:0` resolves to real input today -- actual multi-source routing (a second local gamepad, network players) arrives with S8.
- [x] Network transport abstraction, completing S8.1 of the open-work master plan (`work/open-work-master-plan.md`): `NetTransport` (`net.h`) is an Allocator-style ops struct (`send`/`recv`/`poll` function pointers + opaque `void *state`), with `NetAddr {uint32_t host; uint16_t port}` (host in HOST byte order everywhere outside the socket boundary) as the shared endpoint identifier, `net_addr_make`/`net_addr_eq`/`net_addr_from_ipv4_string` as the construction API, and `NET_MAX_PACKET_SIZE` (1400, below Ethernet MTU minus IPv4/UDP headers) as the wire-size bound both transports enforce. `net_udp.{h,c}` is the real transport: a non-blocking UDP socket, POSIX (`socket`/`bind`/`fcntl O_NONBLOCK`/`sendto`/`recvfrom`) under `#if !defined(_WIN32)` and winsock2 (`WSAStartup`/`ioctlsocket FIONBIO`/`closesocket`/`WSACleanup`, paired per-transport rather than once-globally since Windows refcounts `WSAStartup`/`WSACleanup` internally, keeping the module free of static/global state) under `#if defined(_WIN32)`, sharing one `send`/`recv`/`poll` body across both platforms since winsock2's socket API was modeled on BSD sockets; `ws2_32` is now linked into `engine` on `WIN32` (`engine/CMakeLists.txt`, next to `kernel32`). `net_loopback.{h,c}` is the in-memory transport for headless tests and, later, S8.4's single-process host+client runs: a `LoopbackNetwork` switchboard where `loopback_transport_create` registers endpoints (each individually arena/heap-allocated and referenced by pointer from a `vec_loopback_endpoint_ptr`, so the vec can reallocate on later joins without invalidating any previously handed-out `NetTransport` -- see "Vec growth and pointer stability"), `send` copies the packet straight into the destination endpoint's fixed-cap ring-buffer inbox (`LOOPBACK_INBOX_CAPACITY` slots of `NET_MAX_PACKET_SIZE` bytes, tail-dropping the incoming packet when full so the already-queued older packets survive, mirroring how a real receive buffer silently drops under congestion), and `recv` drains the calling endpoint's own inbox; `poll` is a no-op on both transports since UDP's `recv` already drains the kernel buffer directly and loopback's `send` routes immediately. `vec.h`'s `VEC_IMPL` macro gained explicit casts through `void *` on its `Allocator` calls (behavior-preserving for every existing vec) to support `vec_loopback_endpoint_ptr`, the codebase's first pointer-element vec, cleanly under `bugprone-multi-level-implicit-pointer-conversion`. Tested in `engine/test/net_test.c` (`net_test`, registered via `add_unit_test`): `NetAddr` construction/equality/parsing, `net_send`/`net_recv`/`net_poll` null-op-safety, loopback round-trip in both directions with FIFO ordering, empty-inbox non-blocking recv, the inbox-full tail-drop policy, duplicate-endpoint rejection, and send-to-unknown-destination silent drop; a UDP create/destroy smoke test and a localhost round-trip guard themselves with `TEST_IGNORE_MESSAGE` if the sandbox blocks real sockets, though real UDP worked end-to-end in this session's environment. `net.c`/`net_udp.c` compile and pass ASan/UBSan/LSan clean on the `linux` preset; the winsock2 branch of `net_udp.c` compiles cleanly under the `windows` mingw cross-compile preset (confirmed via `x86_64-w64-mingw32-nm` showing the expected `__imp_WSAStartup`/`__imp_socket`/`__imp_closesocket`/`__imp_ioctlsocket` import stubs), though nothing calls into `net_udp` from the game loop yet, so `ws2_32.dll` does not appear in `sleipner.exe`'s import table until S8.2+ wires it in. S8.2's protocol layer (packet header, message types, attribute serialization) builds directly on this transport. (S8.1)
- [x] Wire protocol layer, completing S8.2 of the open-work master plan (`work/open-work-master-plan.md`): `net_protocol.{h,c}` is a pure byte-buffer codec -- it never touches `NetTransport` (net.h/S8.1), sockets, or a loopback inbox, only `uint8_t*` buffers in and out; S8.4 is what actually calls `net_send`/`net_recv` and feeds the bytes through here. `PacketWriter`/`PacketReader` (`{data, capacity, cursor}`) provide bounds-checked `write_u8/u16/u32/u64/i32/f32/bytes/string` and matching `read_*`; every field is packed BIG-ENDIAN by hand (`write_be_uint`/`read_be_uint`, a `BITS_PER_BYTE`-derived shift loop rather than per-width hand-written shifts or `htons`/`ntohl`) so encode/decode is deterministic regardless of host endianness, and every write/read bounds-checks against `capacity - cursor` before touching memory, so an overflow/overread returns `false` rather than reading or writing out of bounds -- confirmed by a clean ASan/UBSan run against every malformed-input test in `net_protocol_test.c`. `PacketHeader {magic[4]="SLPN", version, type, length, seq}` (`PACKET_HEADER_SIZE` computed from field widths, never a bare literal) is written/read via `packet_header_encode`/`packet_header_decode`, the latter rejecting a wrong magic, a wrong `PROTOCOL_VERSION` (1), or a truncated buffer with an `ErrorState` reason. `MessageType` covers `MSG_INPUT/SNAPSHOT/DELTA/EVENT/JOIN/BEACON/ACK`. `AttrRecord {entity_id, name: Strv, type: AttrType, value}` (reusing `AttrType` from attribute.h as the wire tag; the value itself is a protocol-local `AttrRecordValue` union of `float/int32_t/bool/Strv` rather than attribute.h's owning `AttrValue`, since encode only ever needs to read bytes, never own them) is the shared unit `protocol_encode_attr_list`/`protocol_decode_attr_list` (a `u16` count + that many records) carries for both `MSG_SNAPSHOT` (full) and `MSG_DELTA` (changed-only) -- identical wire shape, distinguished only by the packet header's `type`; decode fails outright, not truncates, when the wire count exceeds the caller's array cap. `MSG_INPUT` serializes an `InputState` field for field (both key bitsets, gamepad buttons/axes). `MSG_EVENT`'s `EventRecord {event_type: int32, entity_id: int32, argument: Strv}` deliberately does not reuse `rule.h`'s `TriggerEvent` (`{TriggerType, entity_index, Str argument}`) -- pulling the full rule/effect/entity header chain into a pure wire-format layer was the wrong trade, so `event_type` is a raw `int32` a future S8.4 caller casts `TriggerType` into (or any other event id space); mapping real `TriggerEvent`s to/from this minimal shape is deferred to S8.4. `MSG_JOIN {gamedata_hash: uint64, client_name: Strv}`, `MSG_BEACON {host_name: Strv, listen_port: uint16}`, and `MSG_ACK {ack_seq, ack_bitfield: uint32}` round out the set (`PROTOCOL_ACK_BITFIELD_BITS` = 32, documented as bit N acking `ack_seq - 1 - N` -- S8.4's reliable resend channel). Every decoded string is a `Strv` view into the caller's own buffer (zero-copy, per strv.h's ownership contract) -- a caller needing a name/argument to outlive the receive buffer must copy it out via `str_from_strv` itself; S8.2 deliberately takes no `Allocator` anywhere. Seven `protocol_encode_<type>_packet(buffer, capacity, seq, payload..., &out_len)` convenience wrappers write header+payload and back-patch `length` in one call (one per message type rather than a single generic function, since C has no generic payload parameter); `protocol_decode_packet(bytes, len, &out, &err)` validates the header and that `header.length` exactly matches the actual payload byte count, rejecting a mismatch as malformed rather than trusting the declared length, and returns a `DecodedPacket {header, reader}` bounded to exactly the payload so no per-message decode can read past the packet it came from. `gamedata_content_hash` (FNV-1a, 64-bit, its own constants rather than widening `strv_hash` so every existing 32-bit-keyed map keeps its hash unchanged) hashes `GamedataParams.toml_string`'s bytes for `protocol_join_verify(local_hash, remote_hash) -> {ok, reason}`, the pure comparison S8.4 will call after decoding a `MSG_JOIN` to accept or refuse a mismatched build. Tested in `engine/test/net_protocol_test.c` (`net_protocol_test`, registered via `add_unit_test`): header round-trip and magic/version/truncation rejection; attr-record round-trip for each of the four `AttrType`s individually (float verified bit-exact via raw `memcpy` comparison, not Unity's epsilon-based `TEST_ASSERT_EQUAL_FLOAT`); every message type's packet-level encode -> decode -> field-equality round trip (SNAPSHOT/DELTA each carrying three mixed-type attr records, ACK with a non-trivial bitfield); a truncated-string attr-record decode and a truncated `InputState` decode against an exact-size stack buffer (no slack, so ASan's stack-redzone instrumentation would catch any overread) both fail cleanly instead of reading out of bounds; a snapshot decoded into a caller array too small for the wire record count is rejected outright rather than silently truncated; a `protocol_decode_packet` call told one byte fewer than was actually encoded (the header's own `length` field left untouched) is rejected on the length cross-check; `gamedata_content_hash` is deterministic and one-byte-change-sensitive; `protocol_join_verify` accepts a match and refuses a mismatch with a reason. Full suite (40 targets) passes, ASan/UBSan/LSan produce no new report files, and the exact CI `clang-tidy` glob is clean -- the two `bugprone-easily-swappable-parameters` findings that surfaced (`write_be_uint`'s `value`/`byte_count`; `packet_begin`'s `capacity`/`type`) were fixed by reordering parameters so no two same-ish-typed ones are adjacent, mirroring `toml_emitter.c`'s `emit_float_literal` dodge, rather than suppressing either finding. S8.3 (LAN discovery, `MSG_BEACON`) and S8.4 (host-authoritative loop: `MSG_INPUT`/`MSG_SNAPSHOT`/`MSG_DELTA`/`MSG_EVENT`/`MSG_JOIN`/`MSG_ACK` actually sent/received over the S8.1 transport) build directly on this wire format. (S8.2)
- [x] LAN discovery beacon + join-list mechanism, completing S8.3a of the open-work master plan (`work/open-work-master-plan.md`): `network.h`/`.c` adds `NetworkState` (`GameState.network`, not `GamedataState` -- transient session state, not undo-snapshotted, same treatment as `MusicState`/`CameraEffect`) -- `NetMode mode` (`NET_OFFLINE/HOSTING/DISCOVERING/JOINING`, zero-init `NET_OFFLINE` so nothing networked happens until a caller sets a mode), the active `NetTransport transport` plus a `transport_initialized` flag (nothing sets it yet -- no code path in this slice creates a real transport), a `JoinList join_list`, a `NetAddr join_target` (filled by S8.3b/S8.4), and `char host_name[NET_NAME_MAX]`. `JoinList` is a fixed-cap array of `DiscoveredHost {NetAddr addr; char name[NET_NAME_MAX]; float last_seen_seconds;}` (`DISCOVERED_HOSTS_MAX` = 16, the same justified-bounded-set exception `BindingStore` relies on) with four small helpers: `join_list_find`, `join_list_add_or_refresh` (silently drops a genuinely new host past the cap rather than crashing), `join_list_age`, and `join_list_evict_timed_out` -- age and evict are two functions rather than one taking both `delta_time` and `timeout_seconds`, since two adjacent float parameters is exactly the shape `bugprone-easily-swappable-parameters` flags (same dodge `hud.h`'s `HudPlayerHealth` uses). `last_seen_seconds` is an AGE counter (seconds since last beacon), not a wall-clock timestamp -- `join_list_age` increments every entry each tick and `join_list_add_or_refresh` resets an entry's age to 0 on a fresh beacon, so no caller needs a shared clock. `net_discovery.h`/`.c` is the tick logic driven purely over an S8.1 `NetTransport` plus `delta_time`, so it is headless-testable against `net_loopback.h`'s switchboard with zero real sockets: `discovery_host_tick(transport, broadcast_target, host_name, listen_port, beacon_timer, delta_time)` accumulates `delta_time` into the caller-owned `*beacon_timer` and, once it reaches `DISCOVERY_BEACON_INTERVAL_SECONDS` (1.0F), encodes a `MSG_BEACON` (S8.2's `protocol_encode_beacon_packet`) and `net_send`s it to `broadcast_target` before subtracting the interval (leftover phase carries over, no drift); `discovery_client_tick(transport, list, delta_time)` drains every pending packet via `net_recv`, add-or-refreshes the sender in `list` for each one that decodes as a valid `MSG_BEACON` (silently ignoring anything malformed or a different message type -- `protocol_decode_packet`/`protocol_decode_beacon` already bounds-check), then ages and evicts past `DISCOVERY_TIMEOUT_SECONDS` (3.0F). `broadcast_target` is an explicit parameter rather than something `discovery_host_tick` derives internally -- the one real design call this slice made beyond the brief's literal sketch: it is what lets a loopback test point host straight at a client endpoint (since loopback has no broadcast/multicast concept), while S8.3b's real UDP wiring constructs the actual broadcast `NetAddr` from `DISCOVERY_PORT` (7777, also new, unused directly by this slice's pure functions) and passes that instead. A `DiscoveredHost.addr` is assembled from the beacon packet's source IP (`net_recv`'s `out_src.host`) plus the beacon's own advertised `listen_port` field -- NOT the ephemeral discovery-socket source port the datagram arrived from -- so it's already the real address S8.4 would dial for the game session, not just a echo of the discovery transport. Tested in `engine/test/net_discovery_test.c` (`net_discovery_test`, registered via `add_unit_test`): the four `JoinList` helpers unit-tested directly (find-absent, add, refresh-updates-name-and-resets-age, over-capacity silently drops, age accumulates, evict removes only stale entries); `discovery_host_tick` driven with small 0.25s deltas over two full intervals proves exactly one beacon per ~1.0s, not one per frame; `discovery_client_tick` over loopback proves two distinct hosts populate two list entries and the same host beaconing again refreshes rather than duplicates; a timeout test proves a host that stops beaconing is evicted once the client's own tick clock crosses `DISCOVERY_TIMEOUT_SECONDS`. Verified by temporarily stubbing out `join_list_add_or_refresh` in `discovery_client_tick`: both join-list-population tests failed (`Expected 1 Was 0`) while the four direct `JoinList` helper tests kept passing, confirming the tests actually exercise the add-to-list path and not just the helpers in isolation; reverted before committing. Full suite (41 targets) passes, no new ASan/UBSan/LSan report files, and the exact CI `clang-tidy` glob is clean (one `misc-include-cleaner` finding for `size_t` fixed by adding a direct `<stddef.h>` include to `net_discovery.c`). The pause-menu Host/Join entries that flip `NetworkState.mode` and drive these ticks from `game_update`, plus the real `net_udp.h` socket bound to `DISCOVERY_PORT`, are S8.3b; the networked game session itself is S8.4. (S8.3a)
- [x] Host/Join pause-menu UI + real UDP broadcast wiring, completing S8.3b and all of S8.3 (LAN discovery) of the open-work master plan (`work/open-work-master-plan.md`): `net_udp_create` (`net_udp.{h,c}`) gained an `allow_broadcast` parameter (`SO_BROADCAST`, needed before `sendto()` accepts a broadcast destination) and now sets `SO_REUSEADDR` + (POSIX-only, `#if defined(SO_REUSEPORT)`) `SO_REUSEPORT` on every socket it creates, unconditionally and best-effort (a failed `setsockopt` never fails the create) -- what lets S8.3b's fixed `DISCOVERY_PORT` (7777) be bound by host and client sockets simultaneously on the SAME machine (local dev/testing, or two unit tests in one process), not just across separate LAN machines. `network.{h,c}` gained the lifecycle half `NetworkState` was missing: `network_start_hosting(NetworkState*, Allocator*, host_name, ErrorState*)` creates a broadcast-enabled UDP transport bound to `DISCOVERY_PORT` and flips `mode` to `NET_HOSTING`; `network_start_discovering` creates a listen-only transport and flips to `NET_DISCOVERING`; `network_stop` destroys the transport (idempotent, safe on an already-`NET_OFFLINE` state) and zeroes every field back to its `NET_OFFLINE` default. Both `_start_*` functions are best-effort/never-crash: a socket failure (no permission, sandboxed CI, port already bound) returns `false` with `err` populated and leaves `NetworkState` completely untouched, so the pause menu simply logs and stays usable -- the same contract `platform_saves_dir`/`save_write`/autosave already follow. Each `_start_*` function is a thin wrapper around a file-local `network_apply_hosting`/`network_apply_discovering` that only mutates `NetworkState` fields given an already-built `NetTransport` -- split out specifically so a test can drive the mode-transition logic with a fabricated (all-zero, null-op-safe per `net.h`) transport instead of a real socket, reached the same whitebox way `net_discovery_test.c` already reaches `net_discovery.c`'s statics. The transport is allocated from `GameState.progression_arena` (not `gamedata_arena`, which `game_load_gamedata`'s `arena_restore` rewinds on every level transition/hot-reload -- hosting must survive those) via an `Allocator` frame.c builds fresh per call; `game_reset_progression` (game.c, the pause-menu RESTORE action) now calls `network_stop` before its own `arena_reset(&progression_arena)`, since that IS the one path that wipes the arena the transport lives in, and a bare `arena_reset` would otherwise orphan the transport struct without ever calling `close()` on its fd. `main.c`'s shutdown sequence and `test_helpers.c`'s `test_game_teardown` both gained a matching pre-teardown `network_stop` call for the same reason. Two new `MenuEntry`/`MenuAction` pairs, `HOST_GAME`/`JOIN_GAME` (`menu.h`/`.c`), sit right after `LOAD_GAME` (before `INVENTORY`) since the whole Save/Restore/Save-Game/Load-Game/Host-Game/Join-Game run is "session-management" operations, growing the menu from nine entries to eleven -- every existing navigation test whose down-press count assumed a fixed position past `LOAD_GAME` (INVENTORY, SETTINGS ×5, QUIT) was re-timed in `integration_test.c`. `dispatch_menu_action` (frame.c): `HOST_GAME` calls `network_start_hosting` (host name is a placeholder `NETWORK_DEFAULT_HOST_NAME = "Sleipner Host"` -- no player-name preference exists yet) and closes the menu straight back to active play, since a host keeps playing while beaconing; `JOIN_GAME` calls `network_start_discovering` then hands off to the new `discovery_screen.{h,c}` overlay (mirrors `save_screen.{h,c}`'s modal/world-frozen shape and `SaveScreenCursor`'s clamp-not-wrap nav) -- but unlike every other modal screen, its row list (`NetworkState.join_list`) is not a snapshot taken at open time: `frame.c`'s new `run_discovery_screen_frame` calls `discovery_client_tick` every frame the screen is open specifically so newly-beaconing hosts appear and stale ones drop off live, on top of also defensively re-clamping the cursor every call (not just on a NAV press) since the list can shrink from underneath an idle cursor via `join_list_evict_timed_out`. CONFIRM on a row sets `join_target` to that host's `NetAddr` and flips `mode` to `NET_JOINING` (S8.4 completes the actual connection) then resolves straight back to active play, same as `save_screen`'s successful CONFIRM; CANCEL calls `network_stop` and reopens the pause menu, same as every other modal screen's CANCEL. `game.c`'s `game_update` gained a `tick_network` call (unconditional of `editor_mode`, guarded on `mode != NET_OFFLINE` so single-player is byte-for-byte unchanged): `NET_HOSTING` calls `discovery_host_tick` targeted at the IPv4 limited-broadcast address `net_addr_make(0xFFFFFFFF, DISCOVERY_PORT)`; `NET_DISCOVERING`/`NET_JOINING` call `discovery_client_tick` -- this is what keeps the join list current for a client that already picked a host (now `NET_JOINING`, screen closed, world no longer frozen) and is why `run_discovery_screen_frame`'s own tick doesn't double up: exactly one of the two runs per frame, since `frame_update` returns immediately after whichever modal branch it takes. `discovery_host_tick`'s `listen_port` argument is `DISCOVERY_PORT` itself, a placeholder -- S8.4 has not yet introduced a separate game-session port for a beacon to legitimately advertise (tracked in TODO.md). Tested in three new/extended suites: `net_test.c` gained a broadcast-create smoke test and a same-port-twice-via-reuse test proving the `SO_REUSEADDR`/`SO_REUSEPORT` wiring actually works; the new `network_test.c` covers `network_apply_hosting`/`_discovering` (mode/beacon_timer/host_name/join_list, fabricated transport, no socket needed), `network_stop`'s full field-reset (including idempotency on an already-offline state), and the real `network_start_hosting`/`_discovering`/coexist-via-reuse path (`TEST_IGNORE_MESSAGE`-guarded for a sandboxed CI, matching S8.1's own socket-test convention); the new `discovery_screen_test.c` covers nav clamping and `handle_input`'s NAV/CANCEL/CONFIRM/empty-list-noop/shrinking-list-reclamp behavior against an injected `JoinList`, all pure and socket-free. `integration_test.c` gained eight black-box tests driven through the real frame loop: offline never ticks discovery (beacon_timer/join_list/transport_initialized all stay at their zero default across 200 frames -- the regression guard for single-player being untouched); `NET_HOSTING`/`NET_DISCOVERING`/`NET_JOINING` each proven to tick every frame via `game_update` (a pre-seeded stale host's eviction, or a pre-seeded near-due beacon timer's exact post-frame value, neither of which anything else in the engine could produce); the real Host Game and Join Game menu entries driven end to end (tolerant of a sandboxed socket failure, since the mode-transition logic itself is already proven independent of any real socket by `network_test.c`); and the discovery screen's CONFIRM/CANCEL wiring against an injected two-host `JoinList` (CONFIRM verified to fail -- `join_target` stays zero, mode stays `NET_DISCOVERING` -- against a temporary stub of `run_discovery_screen_frame`'s wiring; reverted before committing). Full suite passes, ASan/UBSan/LSan produce no new report files, the `windows` mingw cross-compile preset configures and compiles the winsock2 broadcast/reuse path, and the exact CI `clang-tidy` glob is clean. Real LAN discovery across two physical machines is unverified by any automated test (a headless test cannot observe a broadcast actually reaching another host) -- manual on-hardware QA before shipping, tracked in TODO.md. The networked game session (client input -> host simulation -> broadcast deltas, actually consuming `join_target` to connect) is S8.4. (S8.3b)
- [ ] Multiple player entities with independent input sources
- [ ] Per-player camera (each client follows own player)
- [ ] Host-authoritative game loop (host simulates, clients send inputs)
- [ ] State sync: attribute deltas over UDP
- [ ] Reliable channel for events (rule triggers, item pickups)
- [ ] Full state snapshot on player join
- [ ] Collaborative editor: multiple cursors, entity locking, live sync
- [ ] Per-player undo in editor
- [ ] Host-only save with save requests from clients

### Phase 7 — Distribution
- [x] Assets OBJECT library per D18, completing S7.1: `embed_all_assets` (`engine/sources.cmake`) now defines a `sleipner_assets` CMake `OBJECT` library carrying every embedded `.incbin` asset, linked in via `target_link_libraries(... PRIVATE sleipner_assets)` from `sleipner` (`engine/CMakeLists.txt`), `engine_tests` (`engine/test/CMakeLists.txt`), and the Android `sleipner` shared library (`android/app/src/main/cpp/CMakeLists.txt`) — previously the `.S` objects were attached straight to a single executable target, so `ASSET()` only linked there. The blur fragment shader (`engine/src/blur.c`) moved off its C-string workaround back onto an embedded asset, `assets/shaders/blur.fs` — the first embedded TEXT asset — which needed `embed_asset` to grow an optional `NULL_TERMINATE` flag (appends a trailing `.byte 0` inside the `.incbin` range) since `LoadShaderFromMemory` expects a null-terminated C string rather than an explicit length, unlike the size-aware `gamecontrollerdb_txt` consumer. `engine/test/assets_test.c` asserts `ASSET(blur_fs)` resolves and reads back GLSL from inside `engine_tests` — the proof that engine-lib code can now use `ASSET()` at all, since the old shape would have failed to LINK, not just failed to assert. (S7.1, D18)
- [x] Embedded `gamedata.toml` with file-first/embedded-fallback load order per D40, completing S7.2 and Stage 7's Distribution work: `SLEIPNER_EMBED_GAMEDATA` (`engine/sources.cmake`) is OFF by default (a bare/dev `cmake` build embeds nothing) and ON in the `linux`/`windows` configure presets (`CMakePresets.json`) and the Android build (`android/app/src/main/cpp/CMakeLists.txt`, via a forced cache variable) -- the same `.incbin` pipeline S7.1 built handles Android's `.so` identically, no separate APK-asset plumbing needed. When ON, `embed_all_assets` embeds `data/gamedata.toml` with the `NULL_TERMINATE` flag (`gamedata_toml`, `assets.h`'s `DECLARE_ASSET` guarded behind `#if defined(SLEIPNER_EMBED_GAMEDATA)` so a dev build never declares a symbol nothing would provide) and adds the define as a `PUBLIC` compile definition on `sleipner_assets` so every consumer can gate on it. `gamedata_source_read` (new `engine/src/gamedata_source.h`/`.c`, pulled out of `main.c` specifically so `engine_tests` -- which doesn't link `main.c` -- can reach it) is the single file-first/embedded-fallback resolver: `FileExists` on the resolved `gamedata_path` wins if present (the same stat/fopen/fread pass `main.c`'s `load_gamedata` always ran, just relocated); otherwise, on an embedding build, it returns `ASSET(gamedata_toml).data` directly (no arena copy needed -- `game_load_gamedata` memcpy's the source into its own buffer before parsing either way); with no file and no embed, it's a hard `error_set`. `main.c`'s `load_gamedata` (the sole gamedata content read site -- the other four `gamedata_path()` call sites only stat mtime or log the resolved path, never read content) now calls this helper and logs which source won. One non-obvious wiring gap the build caught: `gamedata_source.c` lives in `ENGINE_SOURCE_FILES`, compiled into the `engine` static library, which never itself linked `sleipner_assets` (only the `sleipner` executable and `engine_tests` did) -- unlike `blur.c`'s unconditional `ASSET(blur_fs)`, whose `embed_*_start`/`_end` symbols only need resolving at the final executable's link step, our `#if defined(SLEIPNER_EMBED_GAMEDATA)` compile-time branch needed the define visible while `engine`'s own translation units build. Fixed with `target_link_libraries(engine PUBLIC sleipner_assets)` (`engine/CMakeLists.txt`) -- Android's build needed no equivalent change since it compiles `gamedata_source.c` straight into the same `sleipner` target that already links `sleipner_assets`. Two `engine_tests` cases (`engine/test/gamedata_source_test.c`) prove the load order end to end through `game_init`/`game_load_gamedata`: one drives a nonexistent path and asserts the embedded copy's blueprints/level/player parse correctly (guarded `#if defined(SLEIPNER_EMBED_GAMEDATA)`, `TEST_IGNORE_MESSAGE` in an OFF build so the suite stays green either way -- confirmed by configuring a scratch `-DSLEIPNER_EMBED_GAMEDATA=OFF` build, which still built and passed, with the ignore firing and no embedded-asset symbol pulled in); the other writes a distinctly-named minimal fixture to a temp file and asserts its content wins over any embedded copy, unconditionally. Android on-device verification (delete the Syncthing `gamedata.toml`, confirm the app still boots into the embedded copy) is a manual QA step for real hardware, tracked in TODO.md. (S7.2, D40)

### Phase 8 — Gameplay Systems
- [x] Proper PRNG (xoshiro256** seeded via splitmix64, replacing `rand()` in particle system) (S6.1, D21)
- [x] EffectQueue scaffold: the channel stub rule actions use to reach the world, generalizing `TransitionRequest`. `EffectQueue` lives on `GameState` (not undo-snapshotted), backed by `progression_arena` (init at `game_init`, re-init at `game_reset_progression`), with typed vecs for sound/camera_pan/camera_shake/spawn requests (`engine/src/effect.h`/`.c`). Pushed strings are non-owning `Strv` into gamedata-arena-or-longer memory, not owning copies, since effects can be pushed every frame. `ActionContext` and `rules_evaluate_batch` (`rule.h`/`rule.c`) now thread an `EffectQueue *` through. `frame.c`'s `apply_effect_queue` runs right after `game_update` returns, then clears the queue: sound (S6.4), camera_pan/camera_shake (S6.5), and spawn (S6.6) have real handlers. The scaffold originally reserved a fifth `dialogues` vec for `dialogue:` too, but S6.7c removed it: dialogue turned out to need blocking semantics (D24) that a fire-and-forget per-frame drain can't express, so it was rebuilt on the continuation mechanism instead (see the S6.7c entry below) and the unused queue slot was deleted along with its test coverage. (S6.2, D22)
- [x] `change_sprite:x,y,w,h` rule action per D23: implemented directly in the VM (`execute_change_sprite_action`, `rule.c`) as `attr_set` of `src_x`/`src_y`/`src_w`/`src_h` on the acting entity's instance attrs. VM-direct, not routed through the EffectQueue; no texture-pointer swap in v1 (that would need a registry channel nobody needs yet). Parse (`parse_action_two_args`) and TOML emit already existed and round-tripped the four comma-separated values; `get_source_rect` (main.c) already derived the draw rect from these same scoped attrs, so no render change was needed. (S6.3, D23)
- [x] `play_sound:file` rule action per D32: the VM only enqueues (`ACTION_PLAY_SOUND` in `rule.c`'s `dispatch_simple_action`, pushing `node->argument` directly, no `$variable` resolution needed, mirrors `ACTION_FIRE_EVENT`). `frame.c`'s `apply_effect_queue` (GameState-aware, replaces the old S6.2 `effect_queue_drain` scaffold that lived in `effect.c` -- that module is a pure push/clear channel again) looks the name up in an embedded name->Sound registry (`map_strv_sound`, `audio.h`/`.c`, keyed on full filename e.g. `"pickup.wav"` mirroring the texture registry's `"player.png"` convention) populated once at startup by `main.c`'s `load_persistent_assets`, below `gamedata_base` so it survives every hot-reload/level-transition the same way textures/fonts do. A hit plays through an 8-slot (`SFX_MAX_CONCURRENT_ALIASES`) drop-oldest alias concurrency cap (`SfxAliasPool`, `sfx_alias_pool_next_slot`/`_play`, `audio.h`/`.c`); a miss (unknown name, or headless -- the registry is only populated by `main.c`, never in tests) is logged and skipped. SFX assets (`assets/sfx/pickup.wav`, `hit.wav`) are generated placeholder tones pending real sound design (U1); `audio_play`'s procedural UI tones are untouched. (S6.4, D32)
- [x] `camera_pan:x,y,duration` / `camera_shake:magnitude,duration` rule actions per D22/D26: the VM (`execute_camera_pan_action`/`execute_camera_shake_action`, `rule.c`) resolves each comma-separated token individually (mirroring `execute_change_sprite_action`'s per-token approach, S6.3) and pushes a `CameraPanRequest`/`CameraShakeRequest` onto the `EffectQueue`, rejecting non-numeric tokens with `error_set` rather than silently coercing to 0 like `change_sprite`/`transition` do. `frame.c`'s `apply_effect_queue` starts (or restarts) a `CameraEffect` on `GameState` -- `pan` captures the current `camera_target` as `from`, `shake` records magnitude/duration; last request of a kind in a frame wins. `CameraEffect` lives on `GameState`, not `GamedataState`: transient, not undo-snapshotted, zeroed inside `game_snap_camera` (the single site covering fresh load, hot-reload, and level transition, since both `game_load_gamedata` and `handle_transition` call it) so a pan/shake in flight never bleeds into a new level. `camera_update_target` (game.c) overrides normal player-follow while `camera_effect.pan.active`, lerping via the pure `camera_pan_position` and clearing `active` once elapsed reaches duration so follow resumes; a parallel `camera_update_shake` decays `camera_effect.shake` via the pure `camera_shake_magnitude` and redraws a fresh `state->rng`-seeded jitter `offset` every active frame, computed in `game_update` (not render) so it stays headless-observable. The gameplay camera assembly (`main.c`, `render_frame`) adds `shake.offset` to `camera_target` only at the point it feeds `render_split_camera_target`, never writing it back into `camera_target` itself. (S6.5, D22/D26)
- [x] `spawn:blueprint,x,y` rule action per D22/F24: the VM (`execute_spawn_action`, `rule.c`) resolves the blueprint name and each coordinate individually (mirroring `execute_camera_pan_action`'s per-token approach, S6.5) and pushes a `SpawnRequest` onto the `EffectQueue` -- it never touches `current_level.entities` itself, which is what makes spawning safe from inside a `for_each` batch: the `EntityView` array a batch iterates is built once at the batch boundary (`game.c`) and its `view_count` is a fixed by-value int, so an enqueued spawn can't grow or reallocate the array the batch is still walking. `frame.c`'s `apply_spawn_effects` looks the blueprint up (`blueprint_find`) and calls `level_spawn_entity` -- the same primitive the editor's PLACE path (`handle_place_input`) and paste (S5.7) use -- after `game_update` returns; an unknown blueprint name is logged and skipped, not a frame error. Because spawning changes entity count, `apply_effect_queue` follows up with one `setup_current_level_runtime` rebuild (rule_table, entity_blueprints, player_index, prev_player_overlaps/prev_solid_collisions -- all count-parallel with the entity list) whenever at least one spawn actually landed, the same fix-up S5.7's paste uses for a batch of new entities, rather than incrementally resizing the entity_count² solid-collision matrix. Fixed the pre-existing spawn TOML-emitter bug (`action_emit_table` marked `spawn:` `ACTION_EMIT_ONE_ARG`, silently dropping the coordinates on re-save) to `ACTION_EMIT_TWO_ARGS`, matching camera_pan/camera_shake's S6.5 fix. (S6.6, D22/F24)
- [x] Rule VM converted to a single explicit execution stack per D24: `IF_ELSE`/`REPEAT`/`FOR_EACH`/`CALL` all used to run on a flat stack of pending `ActionNode*` (`rule.c`'s old `execute_from_stack`), except `FOR_EACH` and `CALL` recursed through the C call stack via `execute_action_nodes` to run each iteration/subroutine body -- a for_each's iteration index and bound `LocalScope`, and a call's pool switch, lived in that recursive call's own C stack frame and so couldn't be snapshotted. Replaced with one array of `ExecFrame` (rule.c, file-local): each frame carries the child-index list it's working through, a cursor into it, a `node_index` (for D24's future `(node_index, child_position)` snapshot key -- unused until S6.7b), a per-frame `ActionContext` copy, and (only for `FOR_EACH`) the current bound entity index plus an embedded `LocalScope` whose address is stable for the frame's life (the array never reallocates) so nested bind-mode `for_each`es chain correctly through it. `action_pool` is still a raw pointer for now (read-only at runtime, so safe today); S6.7b will need a subroutine index instead so a frame can be serialized. No behavior change: the entire existing suite plus three new deep-nesting stress tests (nested bind `for_each` inside `if_else` inside `repeat`; a subroutine's own `for_each` called from inside a rule's `for_each`, proving the pool switch and scope chain both survive; `repeat`'s exact action ordering) pass unmodified. The `misc-no-recursion` NOLINT on the old chain is gone -- there is no more C recursion in action execution. This is the enabling refactor for D24's suspendable continuations; `wait:`/`dialogue:` themselves are still pending S6.7b/c. (S6.7a, D24)
- [x] `wait:seconds` rule action via suspendable continuations per D24: `RuleContinuation` (`rule.h`) snapshots a suspended execution as stable references only, never a live pointer -- `entity_id`/`rule_index` (re-resolved through `map_entity_ruleset_get` at resume, dropped if the entity is gone or has since gone inactive), a `vec_exec_frame_snapshot` of the live `ExecFrame` stack (each frame as `pool_id`/`node_index`/`child_cursor`/`kind`/`else_branch`/`repeat_remaining`/`call_depth`, plus `for_each_entity_index`/`bound_entity_id` for `FOR_EACH` frames -- `pool_id` is what `ExecFrame.action_pool`'s raw pointer couldn't be, a serializable index into `GamedataState.subroutines`, -1 for the rule's own tree), and a value-copy of `local_vars` (safe because its backing `vec_attribute` already lives in the gamedata allocator, not scratch). `ExecFrameKind` moved from file-local in `rule.c` to `rule.h` so the snapshot type can name it. `action_node_execute`/`dispatch_or_expand_node`/`run_frame_stack` gained a third `ExecResult` state (`EXEC_ERROR` = 0 so every existing bool-returning caller, including tests, is unaffected) threaded via a `boundary` parameter: `run_frame_stack`'s error-unwind target, 0 for `action_node_execute`'s historical all-or-nothing single-node semantics, 1 for the new `execute_rule_roots` (replacing S6.7a's one-`action_node_execute`-call-per-root loop in `evaluate_entity_rules` with a single shared stack whose bottom frame is the rule's root list) -- unwinding to boundary 1 on error reproduces S6.7a's per-root isolation exactly (a root's failure aborts only that root, never siblings) while letting a `wait` mid-root suspend the rest, which are simply the bottom frame's not-yet-run children captured by its own `child_cursor`. `rules_evaluate_batch` and the new `rules_resume_continuations` (both `rule.c`/`rule.h`) gained a `vec_rule_continuation *continuations` parameter; `game_update` (`game.c`) now shares one `build_entity_views` call between a resume pass (run before trigger detection, per D24, feeding any events a resumed action fires into the same frame's own trigger cascade) and the normal batch. `GamedataState.continuations` rides `gamedata_arena` and is explicitly reset everywhere `rule_table` already is (`game_load_gamedata`'s early-reset block and `setup_current_level_runtime`, covering hot-reload, level load, and the editor's `level_activate` switch) since the arena rewind alone doesn't zero the vec's own count/data fields. Five black-box integration tests (`engine/test/integration_test.c`, driven through real `game_update` frames, asserting only on flags/attrs): a wait delaying a subsequent action, a wait inside a taken `if` branch (the untaken branch never firing), a wait inside a `for_each` staggering a per-entity effect across frames (proving the bound entity and scan cursor survive suspend/resume), two entities waiting independently, and a waiting entity that gets destroyed by a second on_spawn rule before its wake (continuation silently dropped, no crash). All five fail without the real `ACTION_WAIT` handler (verified by temporarily stubbing it to a no-op). Dialogue's blocking wait (S6.7c) reuses the same `Wake` mechanism -- `WAKE_DIALOGUE_CLOSED` is already an enum value, just unproduced until then. (S6.7b, D24)
- [x] Blocking `dialogue:` rule action per D24, completing S6.7 and Stage 6's `wait`/`dialogue` slice: `DialogueState` (`rule.h`, alongside `TransitionRequest` -- both are runtime state a rule action writes into DIRECTLY, not through `EffectQueue`'s deferred drain) holds `active`, a `vec_str pages` (one dialogue page per entry, split on `DIALOGUE_PAGE_DELIMITER = '|'`, one page if absent), `current_page`, and `reveal_elapsed` (typewriter progress; `dialogue_revealed_char_count(elapsed, chars_per_second, page_length)` is the pure reveal-count function, `DIALOGUE_CHARS_PER_SECOND = 30.0`). `ActionMapping` (`rule.c`) gained a `single_arg` flag -- dialogue text is free prose that may contain commas, so it bypasses the generic two-arg comma split every other `has_args` action uses (`parse_single_arg_action`); the editor already anticipated this (`action_argument_is_prose`, `editor/rule.c`) but the parser hadn't followed through until now. `handle_dialogue_action` (`rule.c`) opens `ActionContext.dialogue` directly (copying the text into the caller's allocator, `dialogue_open`) then suspends via a `suspend_rule` helper factored out of `handle_wait_action` -- the two differ only in the `Wake` value (`WAKE_TIMER` vs `WAKE_DIALOGUE_CLOSED`). If a dialogue is already active when a second `dialogue:` fires (two rules in one frame), the second open is dropped (logged) but that rule still suspends with `WAKE_DIALOGUE_CLOSED` so it resumes once the already-open dialogue closes -- see TODO.md for this single-dialogue-at-a-time limitation. `rules_resume_continuations` now treats `WAKE_DIALOGUE_CLOSED` as due the instant `dialogue->active` goes false (no countdown, unlike `WAKE_TIMER`). World-pause: `frame_update` (`frame.c`) gained a `state->dialogue.active` branch mirroring the settings-open branch -- it calls a new `run_dialogue_frame` (ticks the typewriter, and on `ACTION_CONFIRM` calls `dialogue_confirm`: skip-to-full if mid-reveal, advance-and-reset-typewriter if there's a next page, else close) and returns without ever calling `game_update`, so the world (player movement, rules, timers) is completely frozen while a dialogue is open, and `ACTION_MENU_TOGGLE`/`ACTION_QUIT` are ignored too (dialogue never opens over the menu in the first place, since rules only fire from `run_active_frame`). Render (`main.c`, production only): `draw_dialogue_box` fills a bar across the screen bottom and draws the current page's revealed substring via `DrawTextEx`/`ui_font`, matching the visual weight of the existing debug/toast overlays. The now-unused `EffectQueue` dialogue slot from S6.2's scaffold (`DialogueRequest`/`vec_dialogue_request`/`effect_queue_push_dialogue`, `effect.h`/`.c`, plus `frame.c`'s `log_dialogue_effects` stub) was removed -- blocking dialogue belongs with the continuation mechanism, not the fire-and-forget queue (see the S6.2 entry above). Three black-box integration tests (`engine/test/integration_test.c`, driven through real `frame_update` frames): a dialogue blocking a post-dialogue `set_flag` until the box closes (and only on the frame AFTER close, matching the resume pass's timing), world-freeze (held movement input and a running periodic timer both stall while a dialogue is open), and multi-page/typewriter behavior (mid-reveal CONFIRM skips to full, next CONFIRM advances page and resets the typewriter, reveal count grows with elapsed time). Unit tests (`engine/test/rule_test.c`) cover `dialogue_revealed_char_count`/`dialogue_open`/`dialogue_confirm` directly and the single-arg comma-preserving parse, mirroring how `wait:`'s suspend mechanism itself stays integration-only. (S6.7c, D24)
- [x] Inventory core: `give_item`/`remove_item`/`has_item` per D25: `ProgressionState.items` (`ItemSet`, `progression.h`/`rule.h`) is a name -> count `map_strv_int` (`rule.h`/`rule.c`), keyed on `Strv` and reusing the FNV-1a hash `map_strv_sound` (audio.h, S6.4) already used -- pulled out into a shared `strv_hash` (`strv.h`/`.c`) so the two Strv-keyed maps in the codebase agree on distribution instead of duplicating the algorithm. `items` lives in `progression_arena` alongside `flags`/`vars` so it survives level transitions and hot-reloads the same way (rides S2.5): `item_give` (`rule.c`) copies the item-name key into `progression_alloc` on first insert and only mutates the existing entry's value (never its key) on every repeat give, since the action argument it reads from is a `Str` parsed into gamedata_arena at load time and would dangle across the next transition/reload if borrowed directly as the stored key. `item_remove` decrements and removes the map entry entirely at count 0 (never allocates). `item_count`/`item_has` read the map and back the `has_item:X` condition, replacing the old `case COND_HAS_ITEM: return true;` stub (rule.c). `ConditionContext`/`ActionContext` (`rule.h`) and `rules_evaluate_batch`/`rules_resume_continuations` gained an `ItemSet *items` parameter threaded the same way `flags`/`global_vars` already are, at every construction/call site. Four black-box integration tests (`engine/test/integration_test.c`, driven through real `frame_update` frames via `interact`-triggered rules): give-then-has_item (verified to fail against the old always-true stub), remove-at-zero count semantics (give once/remove once clears has_item; give twice/remove once leaves it true), survival across a level transition (mirrors `test_integration_progression_survives_transition`), and RESTORE clearing items the same way it already clears flags/vars (`game_reset_progression` needed no code change -- zeroing the struct already zeroes a valid empty `ItemSet`). Equipment/categories and the pause-menu inventory grid UI are still deferred; the pickup toast is S6.8b. (S6.8a, D25)
- [x] Pickup toast for `give_item` per D25, completing S6.8: `ACTION_GIVE_ITEM` (`rule.c`'s `dispatch_simple_action`) pushes a `ToastRequest` (`effect.h`/`.c`, `EffectQueue`'s fifth vec, mirroring `SpawnRequest`) carrying the raw item name after `item_give` succeeds -- fire-and-forget, guarded on `context.effects` like the other effect pushes, and a missing channel or failed push only logs rather than turning a successful give into a failing action (unlike `ACTION_PLAY_SOUND`, where the push itself is the whole action). `apply_toast_effects` (`frame.c`, called from `apply_effect_queue` which now also takes an `EditorState *`) formats `"Got <item>"` into a new `EditorState.toast_msg_buf` (`TOAST_MSG_BUF_SIZE` = 64, `editor/editor.h` -- a distinct name/constant from `settings.c`'s `TOAST_MSG_BUF_CAP` since the two live in independent structs) and points `toast_text`/`toast_timer` at that owned buffer, reusing S1.2's existing toast surface (`draw_toast`, already rendered in both editor and play mode). Fixed a latent gating bug this feature exposed: `run_active_frame`'s toast countdown used to tick only inside `if (state->editor_mode)`, which was harmless before since every prior toast source (editor actions, menu save/reload) was only reachable from a path that also ticked -- `give_item` is the first toast source reachable from pure play mode (no editor, no menu), so the tick is now unconditional, mirroring `frame_update`'s menu branch (which already ticked regardless of `editor_mode`). Two black-box integration tests (`engine/test/integration_test.c`, driven through real `frame_update` frames via an `interact`-triggered `give_item:key` rule): the toast text/timer are set after a give (verified to fail with the push removed), and the toast actually decays to zero in pure play mode past `TOAST_DURATION` (verified to fail against the old editor_mode-gated tick). `engine/test/effect_test.c` gained matching push/init/clear unit coverage for the fifth vec. Equipment/categories and the pause-menu inventory grid UI remain deferred (TODO.md). (S6.8b, D25)
- [x] Behavior dispatch table + input-source seam per D30/D39, completing S6.9a: `BehaviorContext` (`game.c`, file-local) carries `GameState *`, `entity_index`, the frame's local `InputState *`, `BindingStore *`, and `delta_time`; a `behavior_table[]` (`{name, BehaviorUpdateFn}`, sentinel-terminated, mirrors `action_mappings`' lookup style from `rule.c`) maps an entity's `behavior` attr to `behavior_static` (no-op) or `behavior_player`, with `behavior_lookup` falling back to `behavior_static` for a null or unrecognized name -- an entity with no `behavior` attr is inert by default. `game_update`'s old single hardcoded `update_player`+`resolve_player_obstacles` call is now a loop dispatching every current-level entity through `behavior_lookup`; camera follow still runs once after the loop, resolved via `game_get_player_const` exactly as before. `resolve_player_obstacles` was renamed/generalized to `resolve_entity_obstacles(state, entity_index)` so any moving behavior can reuse the same solid-collision push-out (S6.9b's `chase` calls it too, see below). D39's multiplayer seam: `input_for_entity` reads an entity's `input_source` attr (default `"local:0"`) and returns the real local `InputState` only for `"local:0"`; every other source (a second local gamepad, or a future networked player) yields idle input, since no other input providers exist until S8. Two black-box integration tests (`engine/test/integration_test.c`, driven through real `frame_update` frames): a `static`-behavior entity and a no-behavior entity both stay put while the player moves under held input; two `player`-behavior entities driven by the same input, only the `local:0`-sourced one responds, proving input is routed through the seam rather than read directly. Behavior-preserving: the entire existing suite (including `test_game_update_player_moves_right`/`_left`, camera-follow, and obstacle-collision tests) passes unmodified. (S6.9a, D30/D39)
- [x] `npc_patrol` and `chase` behaviors per D30, completing S6.9: `npc_patrol` oscillates along `(patrol_dx, patrol_dy)` over `patrol_period` seconds (out and back = one period) using an INCREMENTAL model so collision resolves every frame of travel, not just at the two endpoints -- `entity->patrol_phase`, a new runtime-only `Entity` field (`entity.h`, alongside `frame_timer`/`frame_index`, zeroed by `entity_init`'s `memset` and never touched by `toml_emitter.c`'s `[[level.entity]]` writer, so it never bloats saved gamedata) advances by `delta_time` and wraps at `patrol_period`; the first half of the cycle steps toward `+patrol_delta` and the second half steps back. `patrol_period <= 0` or a non-positive `delta_time` is a no-op, matching a static entity. `chase` reads `aggro_radius` and `speed`, finds the player via `game_get_player_const`, and while within `aggro_radius` steers straight toward it via a new pure `chase_step_toward` helper (moves `speed * delta_time` pixels per frame, returns zero once within a named `CHASE_STEER_EPSILON` to avoid normalizing a near-zero vector) -- idle (no movement) with no player entity or outside `aggro_radius`. Both new behaviors call `resolve_entity_obstacles` after moving, and both share a new `animate_walking_entity` helper (`game.c`, file-local) for the moving/anim_row/flip/frame_timer/frame_index walk-cycle bookkeeping, factored out so the two don't duplicate `update_player`'s frame-cycling math. Registered in `behavior_table` as `{"npc_patrol", behavior_npc_patrol}` and `{"chase", behavior_chase}`. Four black-box integration tests (`engine/test/integration_test.c`, driven through real `frame_update` frames): a patrol entity (no player in the level, so nothing else can move it) moves in `+patrol_dx` during the first half-period, reverses in the second half, and lands back near its start position after one full period; a chase entity with the player inside `aggro_radius` closes the distance; the same entity idles (no movement) with the player farther than `aggro_radius`; and a chase entity blocked by a solid wall directly between it and the player stops at the wall's collision edge instead of passing through, locking in the "reuse solid resolution" requirement (mirrors `test_integration_walk_and_collide`'s player-vs-rock collision-rect assertion). A* pathfinding, flee/guard, and group AI remain deferred per D30. (S6.9b, D30)
- [x] Combat damage core (hitbox/hurtbox regions, damage pass, i-frames, defeat wiring) per D26/D28, completing S6.10a: `Entity` gains `hitbox`/`hurtbox` `CollisionShape` fields (`entity.h`) alongside `collision_region`/`trigger_region`, same "empty means absent" contract -- `entity_hitbox_region`/`entity_hurtbox_region` (`entity.c`) mirror `entity_collision_region`/`entity_trigger_region`: an authored composite wins, else a one-rect shape from `hitbox_offset_x/y`+`hitbox_w/h` (resp. `hurtbox_*`) attrs, and `entity_hurtbox_region` falls further back to `entity_collision_region` when neither a composite nor the attrs are authored -- the physical body is the default damageable area. No `[[blueprint.hitbox]]`/`[[blueprint.hurtbox]]` TOML composite authoring yet, mirroring `trigger_region`'s own still-open gap (TODO.md). Two new runtime-only `Entity` fields, `iframe_timer` and `hitbox_active_timer`, same footing as `patrol_phase` (not emitted to TOML), decremented every frame and clamped at 0 by a new `tick_combat_timers` pass (`game.c`) that runs before the frame's trigger detection, so a timer set as scenario setup (or, in S6.10b, by the attack activator) reads active the same frame it's set. `entity_apply_damage` (`entity.c`) is the per-hit helper: a no-op while `iframe_timer > 0`, else it deducts `max(1, raw_damage - defense)` from the target's `health` attr (`attr_set_float`, mirroring how `execute_set_attr_action`/`execute_add_attr_action`, `rule.c`, already write health) and resets `iframe_timer` to the target's scoped `iframes` attr (default `ENTITY_DEFAULT_IFRAME_SECONDS = 0.8`) -- it does not check the resulting health or push `TRIGGER_DEFEAT` itself. That's `detect_melee_damage` (`game.c`, mirrors `detect_solid_collisions`'s pairwise-entity-loop shape): every entity with a live `hitbox_active_timer` calls `entity_apply_damage` on every OTHER active entity with a `health` attr whose hurtbox its hitbox overlaps (`composite_overlap`); a hit that drops health from > 0 to <= 0 pushes `TRIGGER_DEFEAT` into the same frame's `trigger_events`, gating on "old health was > 0" (not just "new health <= 0") so an already-defeated entity hit again once its i-frames lapse doesn't re-fire defeat, exactly mirroring the existing `rule.c` gate. Render flicker (`main.c`, production only): `entity_flicker_visible` blinks the sprite on/off every `COMBAT_FLICKER_INTERVAL_SECONDS` while `iframe_timer > 0`, driven by `GameState.elapsed` rather than a per-entity accumulator. Three black-box integration tests (`engine/test/integration_test.c`, driven through real `game_update` frames via `test_advance_frame`, with a hitbox activated directly as scenario setup since `ACTION_ATTACK` itself is S6.10b): the `max(1, damage - defense)` formula including the floor case, i-frames blocking a repeat hit within the window and allowing one after it elapses, and defeat firing exactly once even under repeated post-defeat hits -- all three verified to fail with `detect_melee_damage` temporarily no-op'd. Contact damage via the existing `collide` trigger, knockback, and the `ACTION_ATTACK` player-attack activator itself are the next slices. (S6.10a, D26/D28)
- [x] `ACTION_ATTACK` player attack input + directional melee hitbox per D26, completing S6.10b: `ACTION_ATTACK` (`input_func.h`) is a new discrete action in the "Gameplay" group alongside `ACTION_INTERACT`, defaulting to keyboard `KEY_J` / gamepad west face (`GAMEPAD_BUTTON_RIGHT_FACE_LEFT`) as a single-atom (non-chord) binding (`default_attack`, `input_func.c`) -- `KEY_J` is a new `keyboard_codes` table entry, since nothing bound it before. Wired into `action_toml_names`/`default_action_atoms` the same way every other action is, so it round-trips through `keybindings.toml` and shows up in the Settings rebind list automatically (both are driven by `ACTION_COUNT` loops, no per-action switch to extend). `Entity` gains a `Vector2 facing` runtime field (`entity.h`), same footing as `patrol_phase`: zeroed by `entity_init`'s `memset`, then explicitly set to `{0, 1}` (down) so an attack fired before any movement still has a defined direction. `update_player` (`game.c`) sets `facing` to the cardinal unit vector of the current move step in the same branch that already picks `anim_row`/`flip` -- SIDE sets `facing.x = ±1`, DOWN/UP set `facing = {0, ±1}` -- so facing always matches the walk animation the player is showing. `update_player_attack` (`game.c`, called from `behavior_player` after `update_player`) is the activator: a fresh `ACTION_ATTACK` press starts a swing by setting `hitbox_active_timer = ATTACK_ACTIVE_SECONDS` (0.15s), gated on `hitbox_active_timer <= 0` so holding/mashing the button can't restart the window mid-swing -- no separate cooldown field. No movement lock: the player can keep walking (and re-orient `facing`) through the active window, matching D26's v1 scope. Directional placement reuses `facing` directly rather than capturing a separate attack-direction field: `entity_hitbox_rect_prim`'s one-rect fallback (`entity.c`) adds `facing * ENTITY_HITBOX_REACH` (8px) on top of the plain `hitbox_offset_x/y` attrs, gated on `hitbox_active_timer > 0` -- inactive entities (and the authored-composite branch, still unused pending the TODO.md TOML-authoring gap) see the exact same offset as before this feature landed, so `detect_melee_damage`/`entity_apply_damage`/i-frames/defeat (S6.10a) needed zero changes. Three black-box integration tests (`engine/test/integration_test.c`, driven through real `frame_update` frames: one frame of held left-stick input plus a fresh `ACTION_ATTACK` gamepad press establishes `facing` and swings in the same frame, since `update_player` runs before `update_player_attack` inside `behavior_player`): an enemy 8px in the facing direction takes `max(1, 5-0) = 5` damage, a second enemy 40px the other way takes none (out of arc), and pressing no attack at all leaves the first enemy untouched. The hits-in-front test was verified to fail (health never leaves its blueprint default) with `update_player_attack`'s body temporarily replaced by an early return. Knockback and ranged/projectiles are the remaining slices. (S6.10b, D26)
- [x] Knockback + contact damage per D26, completing S6.10c: `Entity` gains `knockback_timer`/`knockback_velocity` runtime fields (`entity.h`), same footing as `iframe_timer`/`hitbox_active_timer` -- not emitted to TOML, zeroed by `entity_init`'s `memset`. `entity_apply_knockback(target, from_position, distance)` (`entity.c`) sets both: direction is `normalize(target->position - from_position)`, falling back to `target->facing` if the two positions coincide; `knockback_velocity` is the FULL initial velocity sized so integrating its linear decay to 0 over `KNOCKBACK_SECONDS` (0.15s) covers exactly `distance` px, and `knockback_timer` starts at `KNOCKBACK_SECONDS`. `distance <= 0` (the common case: no `knockback` attr authored) is a no-op. Both `detect_melee_damage` and the new `detect_contact_damage` (`game.c`) call it right after a landed `entity_apply_damage`, passing the attacker's scoped `knockback` attr (default 0) -- knockback is thus attacker-authored, not target-authored. A new `tick_knockback` pass (`game.c`, runs in `game_update` right after both damage passes so a hit landed THIS frame starts moving THIS frame) moves every entity with a live `knockback_timer` by `knockback_velocity * (knockback_timer / KNOCKBACK_SECONDS) * delta_time`, decrements the timer, then calls `resolve_entity_obstacles` -- the same push-out every other mover in `game.c` uses -- so a knockback can't shove a target through a wall. `detect_contact_damage` mirrors `detect_melee_damage`'s pairwise loop shape but gates on a truthy `contact_damage` attr (`attr_get_scoped_bool`) instead of a live hitbox, and tests `entity_collision_region` (the physical body) instead of hitbox/hurtbox -- everything else (the `damage` attr, `entity_apply_damage`, the old/new health defeat gate) is identical, so i-frames de-dupe repeated contact the same way they de-dupe repeated melee hits. Four black-box integration tests (`engine/test/integration_test.c`, driven through real `game_update` frames): a knocked-back target ends up displaced along attacker->target after the decay window, the same knockback stops at a solid wall placed just past the target's resting position instead of tunneling through, a `contact_damage` entity damages an overlapping health entity once and not again inside the i-frame window (mirroring `test_integration_iframes_block_repeat_damage`'s exact timing), and a plain solid with no `contact_damage` attr does not damage anything it overlaps. All four verified to fail against a temporarily no-op'd `entity_apply_knockback`, a temporarily-removed `resolve_entity_obstacles` call in `tick_knockback`, and a temporarily no-op'd `detect_contact_damage`, respectively. Projectiles/ranged attacks are the remaining slice. (S6.10c, D26)
- [x] Projectile behavior + spawn-based ranged attacks per D26, completing S6.10d and all of S6.10: `SpawnRequest` (`effect.h`) gains a `Vector2 facing` field, set from `context.entity->facing` in `execute_spawn_action` (`rule.c`) -- the ACTION_SPAWN handler's rule-owning entity is the "spawner". `apply_spawn_effects` (`frame.c`) applies it to every entity `level_spawn_entity` created for that request (root plus any composite children, re-derived from the pre/post `entities.count` delta) rather than assuming exactly one, so a spawned projectile inherits whichever direction its spawner was last facing -- harmless for non-directional spawns, essential for a projectile blueprint. A new `projectile` behavior (`behavior_projectile`, `game.c`, registered in `behavior_table`) moves `facing * speed * delta_time` every frame, no steering or `resolve_entity_obstacles` (a projectile flies through solid geometry rather than stopping at a wall -- deferred polish, TODO.md). `Entity` gains a runtime-only `projectile_lifetime_timer` field (`entity.h`, same footing as `patrol_phase`): since a spawned entity has no per-instance init hook, `behavior_projectile` treats "still exactly 0" as "never seeded" and seeds it from the scoped `projectile_lifetime` attr on the entity's first update, decrementing every frame after; reaching 0 soft-destroys the entity (`active` attr set false via `attr_set_bool`), safe against re-seeding because the same frame that zeroes it also flips `active`, and the function's own active check short-circuits every later call. Damage is NOT reimplemented -- a projectile blueprint authors `contact_damage`/`damage`/`destroy_on_hit` and rides the existing `detect_contact_damage` pass (S6.10c) unchanged. `destroy_on_hit`: after a contact-damage entity A lands a hit in `detect_contact_damage`, a truthy `destroy_on_hit` attr on A soft-destroys it (`active` false) and breaks out of that attacker's target loop, so a projectile vanishes on impact instead of phasing through. This surfaced a latent gap: `detect_contact_damage`'s attacker loop had no `active` gate of its own (only the target side checked it), so a soft-destroyed attacker would have resumed dealing damage the instant the target's i-frames lapsed -- fixed by a new `entity_can_deal_contact_damage` guard (extracted from the loop's `if` to keep the function's cognitive complexity under clang-tidy's threshold), which also gates the pre-existing `contact_damage` check. The equivalent gap in `detect_melee_damage`'s attacker loop is untouched (out of scope here, low impact since the window there is `ATTACK_ACTIVE_SECONDS` = 0.15s) -- tracked in TODO.md. Four black-box integration tests (`engine/test/integration_test.c`, driven through real `frame_update` frames): a shooter blueprint's own `on_spawn` rule (`wait:0.5, spawn:projectile,200,200`) fires with the shooter as `context.entity`, so driving a few frames of real left-stick movement first establishes its `facing` before the wait elapses -- flies-and-hits (facing the target: the projectile's position moves, then the target's health drops once through the existing damage formula), destroyed-on-hit (continues well past the default i-frame window and asserts both `active == false` on the projectile and stable target health), misses (facing away: target health never moves), and expires (fired into empty space: `active` goes false after `projectile_lifetime` elapses, then position stays frozen on every later frame). The flies-and-hits test was verified to fail (no movement, no hit) with `behavior_projectile`'s body temporarily replaced by an early return. (S6.10d, D26)
- [x] Data-driven animation state machine (directional, blueprint-authored clips) per D31, completing S6.11a: entities carry built-in `state` (idle/walk/attack/hurt/death -- hurt/death defined but not yet triggered) and `direction` (up/down/left/right) attrs, both scoped string lookups (`attr_get_scoped_string`) with a code-level default ("idle"/"down") rather than an authored attr default, since no behavior needs to author them explicitly. A blueprint's `[[blueprint.animation]]` table (`AnimClip { state, row, frames, speed }`, `blueprint.h`/`.c`) parses/emits the same way `[[blueprint.collision]]` does (S4.5/D28) -- state -> row/frame-count/playback-speed rows, round-tripped via a new `emit_anim_clip` (`toml_emitter.c`). Unlike collision, clips are NOT deep-copied onto instantiated entities: `blueprint_find_anim_clip` is looked up live through a new `entity_resolve_blueprint` (`game.c`/`.h`, the full-`Blueprint` sibling of `entity_resolve_defaults`) every frame, since clips are read-only reference data with no per-instance mutation -- an arena copy on every spawn would buy nothing. A single generic pass, `advance_entity_animation` (`game.c`, called once per entity per frame from the same loop that dispatches `behavior_table`), looks up a clip for the entity's own `state` attr (falling back to "walk" then "idle" if the exact state has no clip authored), derives `anim_row = clip.row + <down=0/side=1/up=2>` from the `direction` attr (reproducing the old hardcoded `ANIM_WALK_DOWN/SIDE/UP` layout exactly when a blueprint authors `row = 3`), sets `flip` for "left", and advances `frame_index` at `clip.speed` frames/sec (the same accumulator math as before, just reading `clip.speed`/`clip.frames` instead of the old `ANIM_SPEED`/`WALK_FRAMES` constants). Behaviors no longer write `anim_row`/`flip`/`frame_timer`/`frame_index` directly: `update_player`, `animate_walking_entity` (shared by `npc_patrol`/`chase`), and a new shared `update_entity_anim_attrs` helper set `moving`/`facing` (unchanged movement logic) and then the `state`/`direction` attrs the generic pass reads back. A side effect: `npc_patrol`/`chase` entities now also update `facing` on movement (previously only the player did) -- a prerequisite for `direction` to mean anything for NPCs, and harmless since nothing previously read an NPC's `facing`. The player's own animation is no longer hardcoded: the "player" blueprint (`data/gamedata.toml`) now authors a `walk` clip (`row = 3, frames = 6, speed = 10`) and an `idle` clip (`row = 3, frames = 1, speed = 0` -- holds the standing frame of whichever row `direction` last left it on, reproducing the pre-D31 idle look exactly), and the `ANIM_IDLE_DOWN/UP`/`ANIM_WALK_DOWN/SIDE/UP`/`WALK_FRAMES`/`ANIM_SPEED` constants (`game.h`) are gone. Render (`main.c`): `draw_player_entity` is gone, replaced by a generic `draw_animated_entity` dispatched via a new `entity_has_animation_clips` check (any entity whose blueprint authors at least one clip animates through frame_index/anim_row/flip; everything else keeps the static `get_source_rect` path) -- the player is now just an entity with clips, verified to render and idle-pose correctly against the real `data/gamedata.toml` and embedded assets (screenshot under an off-screen X server). Four tests: three black-box integration tests (`engine/test/integration_test.c`, driven through real `game_update` frames) covering direction -> row/flip mapping, frame_index progression (0..5..0 at the clip's speed) and idle holding frame 0, and the walk/idle state switch -- all three verified to fail with the generic pass's call site commented out (anim_row stuck at its zero default) -- plus a `[[blueprint.animation]]` parse -> emit -> re-parse round-trip test (`toml_emitter_test.c`, mirrors the existing `[[blueprint.collision]]` round-trip). Attack-frame hitbox timing and hurt/death triggering are S6.11b. (S6.11a, D31)
- [x] Combat animation integration per D31, completing S6.11b and all of S6.11: the melee hitbox's active window is now driven by the attack clip's own `frame_index` instead of a fixed timer, and damage/defeat drive `hurt`/`death` animation states with priority over walk/idle. `Entity.hitbox_active_timer` (S6.10b) is renamed to `attack_state_timer` (`entity.h`/`.c`, `game.c`) and repurposed: it now gates the ATTACK STATE rather than the hitbox directly. `update_player_attack` (`game.c`) starts it at the attacker's `attack` clip duration (`blueprint_find_anim_clip(bp, "attack")`'s `frames / speed`, via a new shared `anim_clip_duration` helper also used by hurt/death below) or `ATTACK_STATE_DEFAULT_SECONDS` (0.15s) with no such clip, and resets `frame_index`/`frame_timer` to 0. A new `entity_hitbox_is_active(entity, defaults)` (`entity.c`/`.h`) is the single shared predicate `detect_melee_damage` and `entity_hitbox_rect_prim`'s directional reach shift both now use: true iff `attack_state_timer > 0` AND `frame_index` falls within the scoped `attack_hit_frame_start`/`attack_hit_frame_end` window (ints, defaulting to `[0, INT_MAX]` -- the whole clip -- so an unauthored window, or an attacker with no `attack` clip at all whose `frame_index` never leaves 0, still gets a working hitbox exactly like before this feature). `Entity` gains `hurt_state_timer`/`death_state_timer`/`dying` (`entity.h`), same runtime-only footing as the existing combat timers. `begin_hurt_state` (`game.c`) is called by both `detect_melee_damage` and `detect_contact_damage` right after a landed `entity_apply_damage`, setting `hurt_state_timer` to the target's `hurt` clip duration or `HURT_STATE_DEFAULT_SECONDS` (0.3s) with none. `begin_death_state` is called at the same point those two passes push `TRIGGER_DEFEAT`: sets `dying` (permanent) and, if the target has a `death` clip, `death_state_timer` to that clip's duration (resetting `frame_index`); with no `death` clip, `death_state_timer` stays at its 0 default. A new `tick_death_state` pass (`game.c`, runs in `game_update` alongside the extended `tick_combat_timers`, which now also decrements `attack_state_timer`/`hurt_state_timer`) decrements `death_state_timer` for every `dying` entity and soft-destroys it (`active` attr false) once the timer reaches (or starts at) 0 -- deliberately NOT synchronous with `begin_death_state`: `rules_evaluate_batch` (rule.c) skips inactive entities, so deactivating in the same frame `TRIGGER_DEFEAT` fires would make the target invisible to its own `defeat` rule before that rule gets to run; a clip-less defeat is thus inactive starting the frame AFTER the kill, not the same frame, preserving rule dispatch. A new `resolve_effective_anim_state` (`game.c`) is the priority resolver `advance_entity_animation` consults for its clip-lookup key: `death` (if `dying`) > `hurt` (if `hurt_state_timer > 0`) > `attack` (if `attack_state_timer > 0`) > the behavior-set `state` attr -- used ONLY for the clip lookup/`anim_row` derivation, deliberately NOT written back onto the `state` attr (an entity with no behavior driving it, e.g. a static combat target, would otherwise never overwrite a stale "hurt"/"attack" value with "idle" again, since nothing else resets `state` for such an entity -- discovered as a real test failure during development). Callers (tests included) that need to observe the effective combat state read `dying`/`hurt_state_timer`/`attack_state_timer` directly, the same way existing tests already read `moving`/`frame_index`. `data/gamedata.toml`'s "player" blueprint gains an `attack` clip (`row = 6, frames = 4, speed = 10`, matching a sword-swing sequence discovered on `assets/sprites/player.png`'s row 6/7/8 for down/side/up) and `attack_hit_frame_start = 1`/`attack_hit_frame_end = 2` (the two frames where the blade is actually extended); no `hurt`/`death` clip was added for the player since the sheet's one candidate row (9, a collapse sequence) has no side/up variants and would misrender under the direction-offset scheme for two of three facings -- tracked in TODO.md. Three black-box integration tests (`engine/test/integration_test.c`, driven through real frames): a frame-window test with a deliberately narrow active window (10 frames, well under the 48-frame default i-frame duration so at most one hit can ever land) proves no damage on the press frame, damage once frame_index enters the window, and no further damage once the attack ends -- verified to fail both forcing `entity_hitbox_is_active` always-true (hits on frame 0) and always-false (never hits); a hurt-state test proves `hurt_state_timer` goes positive after a landed hit and decays back to 0 within its default window; a death-state test proves an entity with a `death` clip stays active (so its `defeat` rule still fires) and shows `dying` immediately, then deactivates once the clip's duration elapses, while a clip-less entity deactivates starting the very next frame instead of the same one. The existing S6.10a-d combat tests were updated only mechanically (the `hitbox_active_timer` rename) plus one comment fix on `test_integration_defeat_fires_once` documenting that a clip-less `fragile_target` now also relies on the post-defeat `active` gate, not just the `old_health > 0` gate, to block re-firing. (S6.11b, D31)
- [x] HUD hearts (health display, halves) per D34, completing S6.12a: `HudHearts hud_compute_hearts(HudPlayerHealth health)` (new `engine/src/hud.h`/`.c` module) is the pure layout/state half -- maps current/max player health (raw health points) to a row of `HUD_HEART_EMPTY`/`_HALF`/`_FULL` states, one heart per `HUD_HEALTH_PER_HEART` (2) health points; `health.current` is clamped to `[0, health.max]` and rounded to the nearest half-heart before classification, and heart count (`ceil(health.max / HUD_HEALTH_PER_HEART)`) is clamped to a fixed-cap `HUD_MAX_HEARTS_DISPLAYED` (20) -- screen-space HUD real estate, not a gameplay balance limit (see the header comment); an odd `max` (e.g. 5) makes the last heart a half-slot at full health. `hud_compute_hearts` takes a `HudPlayerHealth {current, max}` struct rather than two adjacent floats, mirroring `effect.h`'s `CameraShakeRequest` precedent for dodging clang-tidy's `bugprone-easily-swappable-parameters`. `hud_heart_screen_position` is a matching pure helper (heart index -> top-left screen position, anchored at a fixed margin, independent of screen size). `hud_draw_hearts` (raylib half, production only) draws each heart as a placeholder square -- filled for full, half-filled for half, gray-outlined for empty -- no heart sprite asset exists yet (TODO.md). Wired into `main.c`'s `render_frame` via a new `draw_player_hud`: reads the player entity (`game_get_player_const`/`entity_resolve_defaults`), current health via the scoped `health` attr, max health via the scoped `max_health` attr (falling back to the blueprint's own default `health` attr when `max_health` is unauthored), then calls `hud_draw_hearts` -- gated on `!state->editor_mode` (play-mode only) and placed after the world's `DrawTexturePro` upscale blit, before `draw_debug_info`/`menu_render`/`settings_render` -- i.e. screen space, after the world render, before the menu/settings overlays, exactly per D34. Nine unit tests (`engine/test/hud_test.c`, mirrors `effect_test.c`'s pattern of compiling `hud.c` directly with FFF fakes for its two raylib draw calls) cover full/half/empty classification, the odd-max half-slot case, current-above-max and negative-current clamping, the fixed-cap overflow clamp, and the position helper's spacing math -- all headless. Gap found during manual (screenshot) verification: the "player" blueprint in `data/gamedata.toml` authors no `health`/`max_health` at all (only the unrelated "tree" blueprint's `health = [100, 100]` exists), so today's HUD renders zero hearts in actual play -- confirmed by temporarily authoring a player `health` value and re-screenshotting, which showed the heart row rendering correctly. Picking a starting/max health value for the player is a content/balance decision, not made here (TODO.md). The inventory pause-menu screen is a separate, not-yet-started slice (S6.12b); minimap/map screen is deferred further, both per D34. (S6.12a, D34)
- [x] Inventory pause-menu screen (gamepad-navigable grid) per D25/D34, completing S6.12: a new `MENU_ENTRY_INVENTORY`/`MENU_ACTION_OPEN_INVENTORY` (`menu.h`/`.c`) sits between Restore and Settings in the pause menu (inserted there rather than right after Resume specifically to avoid reflowing the many existing menu-navigation tests that hardcode "one DOWN-press from Resume lands on Save" -- see the "e.g. after Resume or before Settings" placement note this slice worked from). `InventoryScreen` (new `engine/src/inventory_screen.h`/`.c`) mirrors `SettingsState`'s lifecycle exactly: `open`/`cursor`/`blur_captured` fields, `inventory_screen_init`/`_open`/`_close`/`_is_open`/`_handle_input`/`_render`/`_cleanup`, a `run_inventory_frame` (`frame.c`, mirrors `run_settings_frame`) that `frame_update` branches to (early-return, freezing the world) right after the settings branch, and `dispatch_menu_action`'s new `MENU_ACTION_OPEN_INVENTORY` case (mirrors `OPEN_SETTINGS`) that snapshots `state->progression.items` and closes the menu. The item list is snapshotted once at open time via `inventory_screen_collect_items`, which walks `ItemSet.counts`'s raw `map_strv_int` bucket array directly (`map_strv_int` has no iterator, same pattern `item_set_free`/`unload_sfx_registry` already use) into a `vec_inventory_item_entry` of `{Strv name, int count}` pairs -- allocated against `progression_alloc` (`allocator_arena(&state->progression_arena)`), not `gamedata_arena`, specifically because a hot-reload or level transition can fire while the modal screen is open and `gamedata_arena` (unlike `progression_arena`) gets rewound by both. Three pure, headlessly-tested functions carry the actual logic, no raylib: `inventory_screen_collect_items` (above), `inventory_screen_grid_nav` (`cursor, InventoryGridShape{columns, item_count}, InventoryNavDirection` -> new cursor -- row-major linear-index stepping for LEFT/RIGHT (so RIGHT off the last column of a row lands on the next row's first column) and `±columns` stepping for UP/DOWN, clamped not wrapped at both ends, a no-op returning 0 when `item_count <= 0`), and `inventory_screen_cell_position` (cell index + `InventoryGridLayout{columns, cell_width, cell_height, margin}` -> `{x, y}` relative to the grid's own origin). Both grid-math functions bundle their int-typed extent into a small struct rather than passing it as bare adjacent parameters, the same precedent `hud.h`'s `HudPlayerHealth` (S6.12a) and `effect.h`'s `CameraShakeRequest` set; `inventory_screen_grid_nav` additionally had to move the struct between `cursor` and `direction` (rather than trailing, its first-drafted position) since clang-tidy's `bugprone-easily-swappable-parameters` flags `int`/enum adjacency too (an enum implicitly converts to/from `int` in C) -- not just adjacent same-named-ish ints. `inventory_screen_render` (raylib, production only) shares the menu/settings blurred backdrop (`main.c`'s `capture_overlay_blur_if_needed` now folds a third `blur_captured` flag in) and draws a 4-column vignette-panel grid of `<name> x<count>` cells, highlighting the cursor cell, or an "Empty" message with zero items -- rendered via `state->assets.ui_font` passed directly as a parameter (no dedicated font load, mirroring `draw_dialogue_box`/`draw_toast` rather than `MenuState`/`SettingsState`'s own cached-font fields). Sixteen unit tests (`engine/test/inventory_screen_test.c`, compiles `inventory_screen.c` directly with FFF fakes for its raylib/input.c dependencies, mirrors `menu_test.c`'s pattern) cover `collect_items` (occupied-only, order-independent, and the empty-set case), all four nav directions' mid-grid and clamped-edge cases plus the empty-inventory no-op, and the cell-position math across first cell/next column/next row/an arbitrary index. One black-box integration test (`engine/test/integration_test.c`, driven through real `frame_update` frames): open the pause menu, walk to Inventory (3 DOWN-presses), confirm -- proves the screen opens, the menu closes, and held movement input never moves the player for 20 frames (mirrors `test_integration_dialogue_world_frozen`'s freeze proof) -- then CANCEL closes the screen and reopens the menu. Verified to fail against a temporarily no-op'd `inventory_screen_grid_nav` (cursor never moves) and a temporarily no-op'd `inventory_screen_close` (screen never reports closed). Equipment/categories, per-item actions (use/equip/drop), and the minimap/map screen remain open per D34/the Inventory & Items open questions below. (S6.12b, D25/D34)
- [x] Inventory & items
- [x] Dialogue system
- [x] Level transitions
- [x] HUD inventory screen + pause-menu entry (S6.12b) -- hearts (S6.12a) done above per D34; the inventory grid itself is the S6.12b roadmap bullet directly above.
- [x] Master/music/SFX volume preferences + Settings General rows per D32/F31, completing S6.13a: `Preferences` (`preferences.h`/`.c`) gains three `float` fields, `master_volume`/`music_volume`/`sfx_volume`, defaulting to `PREFERENCES_VOLUME_DEFAULT` (1.0) in `preferences_init_defaults` and round-tripping through a new `[audio]` preferences.toml table (mirrors the existing `[paths]` table); `preferences_load` tries `toml_double_in` then `toml_int_in` per field (same fallback order `input_func_toml.c`'s `parse_scale` already uses for TOML's int/float ambiguity), clamps to `[PREFERENCES_VOLUME_MIN, PREFERENCES_VOLUME_MAX]`, and a missing `[audio]` table or field keeps whatever `prefs` already held (the 1.0 default) -- the same back-compat contract `data_dir` already has, so preferences.toml files written before S6.13a still load. Two pure helpers, `preferences_effective_music_volume`/`_sfx_volume`, fold master into the per-channel value (`master * channel`, clamped) and are headlessly unit-tested (`preferences_test.c`) across the multiply, the clamp, and the 0/1 edges. The Settings General tab (`settings.c`) gains a `GeneralRow` enum (`GENERAL_ROW_DATA_DIR`/`_MASTER_VOLUME`/`_MUSIC_VOLUME`/`_SFX_VOLUME`/`_COUNT`), bumping `GENERAL_TOTAL_ROWS` from 1 to `GENERAL_ROW_COUNT` (4); `ACTION_NAV_LEFT`/`_RIGHT` step the selected volume row by `GENERAL_VOLUME_STEP` (0.1), clamped, and mark `save_preferences_requested` so the existing settings-dispatcher save path (`frame.c`, unchanged) writes preferences.toml on every adjustment, the same one-event-one-save model path-edit's commit already uses. Adding real rows makes the `GENERAL_TOTAL_ROWS - 1` NAV_DOWN clamp live for the first time, so the `NOLINTNEXTLINE(misc-redundant-expression)` workaround (F31, tracked in TODO.md) is removed -- clang-tidy stays clean without it because the bound is no longer trivially false. Applied in production only: `main.c`'s game loop calls `SetMusicVolume(bgm, preferences_effective_music_volume(&state->preferences))` every frame before `UpdateMusicStream`; `sfx_alias_pool_play` (`audio.h`/`.c`) gained a `float volume` parameter, set via `SetSoundVolume` on the claimed alias before `PlaySound`, threaded in from `frame.c`'s `apply_sound_effects` via `preferences_effective_sfx_volume(&state->preferences)` -- both call sites are headless-inert (the music loop never runs in tests, and the SFX registry that gates `sfx_alias_pool_play` is only populated by `main.c`'s production asset loading), so no new raylib fakes were needed outside `audio_test.c`'s existing whitebox fake list (`SetSoundVolume` added there). A new `settings_general_test.c` (whitebox, mirrors `settings_capture_test.c`'s pattern) drives `settings_handle_input` directly: NAV_DOWN traversal across all four rows (clamping at the top), NAV_LEFT/RIGHT adjusting and clamping each of the three volume rows at 0/1, and confirming the data_dir row's LEFT/RIGHT are no-ops. Per-level music selection and crossfade transitions remain open, S6.13b. (S6.13a, D32, F31)
- [x] Per-level music with a name-keyed registry and a 1.0s linear crossfade on level transition, completing S6.13 per D32: each `Level` already carried `music_name` (a level's `music = "bgm.mp3"` TOML field, `level.h`); an embedded name->`Music` registry (`map_strv_music`, `audio.h`/`.c`, keyed on full filename like the `map_strv_sound` SFX registry) is populated once at startup by `main.c`'s `music_registry_add` (called from `load_persistent_assets`, below `gamedata_base` so it survives every hot-reload/level-transition like textures/SFX). With one shipped music asset (`bgm.mp3`) the registry has one entry; the SYSTEM is the deliverable, more tracks are content. The single global `Music bgm` that `main.c` used to load/play/update/unload directly is gone, replaced by registry-driven stream management. Crossfade state and gain are split from the streams so they are headless-testable without an audio device: a `MusicState` on `GameState` (`audio.h`, runtime session state, NOT `GamedataState`, not undo-snapshotted -- same category as `CameraEffect`) holds the current track NAME, the outgoing (fading) track NAME, and a `crossfade_timer` (seconds remaining, 0 = no crossfade). `music_on_level_changed(MusicState *, const char *new_track_name)` (called from `game_load_gamedata` after a fresh load/hot-reload AND from `level_activate` after the editor's direct current_level swap, both `game.c`) starts a crossfade only when the new track NAME differs from the current one -- a same-name transition (different level, same track) does NOT restart the fade, and a first-ever load with nothing playing just sets current with no fade. `music_state_tick` (called each frame in `game_update`) counts the timer down and clears the outgoing name at 0. The pure `music_crossfade_gain(float timer, float total)` returns `{outgoing = timer/total, incoming = 1 - timer/total}`, clamped to `[0,1]`, with `timer <= 0` yielding `{0, 1}` -- unit-tested in `audio_test.c` at the full/half/zero/overlong points. `main.c`'s production-only stream layer (`music_streams_update`, called once per frame after `handle_transition`) reads `MusicState` + the registry: it stops any stale stream, then plays/updates the current (and, mid-crossfade, the outgoing) `Music` at `gain * preferences_effective_music_volume(&prefs)` (S6.13a's master*music channel volume); a track name not in the registry is logged and skipped, no crash. Integration tests (`integration_test.c`, driven black-box through the real enter-trigger -> `transition` path) assert the state transitions: load sets current with no crossfade, a transition to a different track starts a 1.0s crossfade (outgoing = old, current = new, timer = `MUSIC_CROSSFADE_SECONDS`), ticking past 1.0s clears the outgoing name, and a further transition to a level naming the SAME track does not restart the fade. Spatial SFX falloff was explicitly OUT of scope for this slice and remains deferred (see TODO.md). (S6.13b, D32)
- [x] Fade-to-black level transitions per D27, completing S6.14: a level swap no longer happens the instant a `transition:` rule action fires -- it now runs a pure `TRANSITION_FADE_OUT -> swap-at-midpoint -> TRANSITION_FADE_IN` state machine, 0.3s (`TRANSITION_FADE_SECONDS`) each way. `TransitionFade` (`game.h`, `{TransitionFadePhase phase, float timer}`) lives on `GameState` in the same category as `CameraEffect`/`MusicState` -- runtime session state, not `GamedataState`, not undo-snapshotted -- but is deliberately NOT reset by `game_snap_camera` (unlike `CameraEffect`): the swap step itself calls `game_snap_camera` while `fade.phase` is already about to become `FADE_IN`, so resetting `fade` there would stomp the very transition in flight. Two pure, unit-tested (`game_test.c`) functions carry the whole state machine: `transition_fade_tick(TransitionFade fade, float delta_time)` returns a `TransitionFadeStep{TransitionFade fade, bool do_swap}` -- `do_swap` is true exactly on the frame `FADE_OUT`'s timer reaches zero, the instant `frame.c`'s `handle_transition` must run the level-swap body, and phase moves to `FADE_IN` with a fresh timer that same call; `FADE_IN` reaching zero returns to `NONE`. `transition_fade_alpha(TransitionFade fade)` maps phase/timer to a `[0, 1]` black-overlay opacity for the renderer (0 at `NONE`, ramping 0->1 across `FADE_OUT`, exactly 1 at the swap instant, ramping 1->0 across `FADE_IN`). Both take a single `TransitionFade` struct rather than a bare `(TransitionFadePhase, float)` pair specifically because clang-tidy's `bugprone-easily-swappable-parameters` flags that pair (the enum implicitly converts to `float`) -- the same struct-wrapping precedent `HudPlayerHealth`/`CameraShakeRequest` already set. `handle_transition` (`frame.c`) is reworked into a driver: idle (`fade.phase == NONE`) with `state->transition.pending` starts the fade (`phase = FADE_OUT`, `timer = TRANSITION_FADE_SECONDS`) and consumes `pending`, but does NOT swap that same frame; any other frame ticks `transition_fade_tick` and, only when `do_swap` fires, runs `run_transition_swap` -- the exact pre-S6.14 instant-swap body (gamedata reload, player positioned at the spawn coords the `transition:level,x,y` action still carries unchanged, camera snap, overlap-tracking pre-seed so enter triggers don't refire at spawn, "Level loaded" undo entry), now extracted into its own function and simply called later. A second `transition:` firing while a fade is already in flight is left pending rather than queued or dropped: `execute_transition_action` (rule.c) overwrites the same `TransitionRequest` fields in place, so the last such request before the fade returns to idle is the one consumed, coalescing into a single target -- not reachable via ordinary play since input is suppressed for the whole fade (see below), but documented in `handle_transition`'s comment since a scripted/timer-driven transition could still reach it. Input suppression: `run_active_frame` (`frame.c`) passes a zeroed `InputState` into `game_update` for every frame `state->fade.phase != TRANSITION_FADE_NONE` -- since `handle_transition` runs once per frame right after `frame_update` (`main.c`, mirrored by `test_helpers.c`'s `test_advance_frame(s)`), the phase read is the one left over from the previous frame's tick, exactly the boundary the fade is meant to gate; editor input and the toast timer are untouched, since D27 only suppresses player input. Render (`main.c`, production only): `draw_transition_fade_overlay` draws a full-screen `DrawRectangle` at `Fade(BLACK, transition_fade_alpha(state->fade))` opacity, called right after `draw_dialogue_box` and before the pause-menu/settings/inventory overlays -- ordering is a formality since a transition never runs while one of those is open. Headless tests never render, so this path is production-only and untested by construction (matches every other raylib-draw wrapper in the codebase). One new integration test (`integration_test.c`, driven black-box through the same enter-trigger -> `transition:interior,80,60` fixture `test_integration_transition_changes_level` already uses) proves the D27 guarantee end to end: the level stays "field" while fading out even with movement input held, the swap lands within the fade's ~0.3s window, the player can't drift during either fade half despite held input, and movement resumes once the fade returns to `NONE` -- verified to fail (the level is already "interior" the very frame the fade starts) against a temporary revert of `handle_transition` to the pre-S6.14 instant swap. The five pre-existing transition-driving integration tests (`test_integration_transition_changes_level`, `test_integration_transition_to_different_track_starts_crossfade`, `test_integration_transition_to_same_track_does_not_restart_crossfade`, `test_integration_progression_survives_transition`, `test_integration_item_survives_transition`) all already loop "advance frames until the level name changes" with a generous iteration budget rather than hardcoding a fixed frame count for the swap itself, so none needed re-timing -- the extra ~18 frames the fade now adds before the swap lands are absorbed by the existing budget, and every post-swap assertion (spawn position, crossfade start/skip, flag/item survival) still reads the same state at the same "loop just exited" frame boundary as before. Entity-state persistence across a transition remains out of scope per D27/D33 (save system) -- levels still reset on re-entry. A narrow, documented gap surfaced during this slice -- a hot-reload or pause-menu RESTORE landing while a fade is in flight can race the pending swap -- is tracked in TODO.md rather than fixed here, since resolving it is a genuine design choice (defer the reload, cancel the fade, or re-validate the pending transition) outside this slice's scope. (S6.14, D27)
- [ ] AI & pathfinding (patrol, aggro, chase)
- [ ] Audio (music crossfade, spatial sound, ambient layers)
- [ ] NPC behaviors (patrol, dialogue)
- [x] Platform saves directory resolution, completing S6.15a per D33 (save system, sliced a-e): `platform_saves_dir(Str *out, Allocator alloc, ErrorState *err)` (`platform_paths.h`/`.c`) composes the OS data-dir `saves/` path -- Linux/BSD `$XDG_DATA_HOME/sleipner/saves/` (fallback `$HOME/.local/share/sleipner/saves/`), Windows `%APPDATA%/sleipner/saves/`, Android `<internalDataPath>/saves/` -- via a new `compose_user_data_path` static that mirrors the existing `platform_preferences_path`'s `compose_user_config_path`, but rooted at the DATA dir (`XDG_DATA_HOME`/`.local/share`) instead of the CONFIG dir, and with no binary-adjacent portable override (saves are always app-local runtime state, unlike the `<binary_dir>/preferences.toml` escape hatch preferences gets). `platform_ensure_saves_dir(const char *dir_path, ErrorState *err)` creates the directory (mkdir -p semantics via raylib `MakeDirectory`, a `DirectoryExists` check first makes repeat calls a no-op); since it operates on a directory path directly rather than stripping a basename off a file path like `platform_ensure_parent_dir` does, the shared MakeDirectory-if-missing logic was pulled into a static `ensure_dir_exists` used by both. `platform_paths_test.c` extends the existing fff-fake-driven pattern (`GetApplicationDirectory`/`FileExists`/`DirectoryExists`/`MakeDirectory` fakes, env vars reset in `setUp`) with `XDG_DATA_HOME`-set/unset composition tests and dir-creation idempotency tests. Save file format, serialization, and the pause-menu Save/Load UI remain unbuilt -- S6.15c-e.
- [x] Per-level entity delta layer (in-memory), completing S6.15b per D33: leaving a level now captures a snapshot of its entities' position/instance-attrs/active state, and returning re-applies it onto the freshly re-parsed level -- so an opened chest, a moved entity, or a soft-destroyed one all survive a transition round trip (D27's "entity-state persistence across a transition remains out of scope" gap is now closed in memory; serialization to a save file is still S6.15c). `EntityDelta { int id; Vector2 position; AttrSet attrs; bool active; }` (`progression.h`) captures the two-level-scoping insight directly: an entity's instance `AttrSet` already IS its delta against blueprint defaults (per the "Entity–blueprint connection" design), so `attrs` alone covers every attr-shaped gameplay change; `active` is kept as its own field too even though it's presently redundant with an explicit `active` entry inside `attrs` (every soft-destroy in this codebase already goes through `attr_set_bool(..., "active", ...)`), so the delta's active/inactive marker has a stable, attr-encoding-independent home once S6.15c gives it a save-file schema. A new `attr_set_copy(Allocator *alloc, AttrSet *dst, const AttrSet *src)` (`attribute.h`/`.c`) deep-copies an entity's attrs (names, and string values too) into the delta store independently of the live entity being torn down -- unit-tested (`attribute_test.c`) by mutating the source after copying and then freeing it, which would surface as a heap-use-after-free under ASan if the copy were shallow. `LevelDelta { vec_entity_delta entities; }` lives in a new `map_strv_level_delta` (`MAP_DECL(strv_level_delta, Strv, LevelDelta)`), keyed by level name, held on `ProgressionState.level_deltas` (`progression.h`) -- the natural home alongside `flags`/`vars`/`items`, since it needs the exact same progression_arena lifetime (survives transitions/hot-reload, wiped by `game_reset_progression`'s wholesale zero on new-game/RESTORE). The key is always a fresh `progression_alloc` copy of the level's name (never a bare view into the gamedata-arena-backed `Level.name`, which the very reload this capture precedes rewinds) -- unlike `ItemSet`'s `item_give`, `progression_capture_level_delta` doesn't bother checking for an existing key first; a recapture's key is simply orphaned once `map_set`'s existing-key path matches it by content and only overwrites `.value`, which is fine (one small `Str` per transition) and turned out to also be the fix for a Clang Static Analyzer false positive that only reproduced when the get-then-conditionally-set branch coexisted with `progression_apply_level_delta`'s own `map_get` call in the same translation unit (confirmed via a bisected minimal repro; the `capacity > 0` / `entries != nullptr` invariant the analyzer thought could be violated is unreachable by inspection of `map.h`'s own code, since `map_rehash` is the only writer of both fields and always sets them together). `progression_capture_level_delta`/`progression_apply_level_delta` are wired into `frame.c`'s `run_transition_swap`: capture runs BEFORE `level_loader_fn` reloads the level being left (while its entities are still the live ones), apply runs AFTER the target level is freshly parsed but BEFORE the existing player-spawn-position write -- so a stale captured player position is deliberately overwritten by the door's declared spawn coords immediately afterward, no special-casing of the player entity id needed. Deltas are cleared on hot-reload (`progression_clear_level_deltas`, called from `main.c`'s `reset_editor_after_reload` -- the one function both `poll_hot_reload` and `menu_dispatch_restore` funnel through, but NOT the transition path) since the gamedata just changed and any captured delta is stale against the new authoring; new-game/RESTORE clears it implicitly since `level_deltas` rides the same `game_reset_progression` wholesale zero as `flags`/`vars`/`items`. Two new black-box integration tests (`integration_test.c`): `test_integration_level_state_persists_across_transition` (opens and soft-destroys a chest via `set_attr:opened,true`/`set_attr:active,false`, round-trips field -> interior -> field, asserts both survive -- verified to fail against a temporary revert of the two `run_transition_swap` hooks) and `test_integration_player_keeps_spawn_coords` (guards the ordering decision specifically: leaving "interior" through its exit door captures the player's delta near the exit, not interior's own spawn point, so re-entering must not leave the player at that stale position). v1 limitation, not attempted here: entities spawned at runtime (`spawn:`, S6.6) get ids only valid for that session, so `level_find_entity_by_id` never matches them again after a reload -- spawned entities do not persist across transitions (tracked in TODO.md).
- [x] Save-state TOML serialize/deserialize, completing S6.15c per D33: a new `SaveState { Str current_level_name; ProgressionState progression; }` (`save.h`) is a thin bundle over S6.15b's process-lifetime state plus the one fact it doesn't already carry -- which level to reload on restore. `save_serialize(const SaveState *, Allocator *, ErrorState *) -> Str` and `save_deserialize(const char *, Allocator *, ErrorState *, SaveState *out) -> bool` (`save.c`) are pure string<->struct: no file I/O, no pause-menu wiring (still S6.15d). Schema: a `[save]` header (`version`, `current_level`, a `flags` array of set flag names), `[save.vars]`/`[save.items]` tables (name -> typed value / name -> count), and one `[[level_delta]]` array-of-tables entry per captured level, each with a `name` and a `[[level_delta.entity]]` sub-array (`id`, `pos`, `active`, plus the entity's own attrs). `toml_emit_save` (`toml_emitter.h`/`.c`) does the emitting, taking the raw `FlagSet`/`AttrSet`/`ItemSet`/`map_strv_level_delta` pieces rather than a bundled type -- mirrors `toml_emit_gamedata`'s own raw-pieces signature, keeping `SaveState` a `save.h`-only concern. It reuses the existing `emit_attr_value` (typed int/float/bool/string encoding) directly, and a newly extracted `emit_float_literal` helper (factored out of `emit_attr_value`'s `ATTR_FLOAT` case and the bindings emitter's `emit_scale_field`, which had duplicated the same "%g, force a decimal point so TOML keeps it as float" logic) backs the new position-pair emission -- unlike gamedata's own `[[level.entity]]` `pos` field (which truncates to int, since authored positions are whole-pixel by convention), a save's captured runtime position can be fractional, so it round-trips as a float pair. `emit_float_literal` lists its float `value` parameter before the `buffer`/`capacity`/`offset` trio, the same `bugprone-easily-swappable-parameters` dodge `emit_collision_kind` already uses, since an adjacent `(int offset, float value)` pair implicitly converts. Parsing (`save.c`) mirrors `blueprint.c`'s `parse_custom_attr`/`level.c`'s `parse_instance_attr` bool/int/double/string cascade -- a third copy of the same established idiom (the first two are file-local statics with no shared header, so this duplicates rather than extracts) -- and `level.c`'s generic key-walk (`toml_table_nkval`+`narr`+`ntab`, skipping the fields with dedicated parsing) for entity-delta attrs. A missing section (no `flags`, no `[save.vars]`, no `[[level_delta]]`) is tolerated as empty, not an error; a missing `[save]` table, a hard TOML parse failure, or an unsupported `version` (`SAVE_FORMAT_VERSION = 1`, named rather than a bare literal per the magic-numbers lint note) all return false with a wrapped `ErrorState` chain. tomlc99 string datums (`toml_string_in`/`toml_string_at`) are freed via the vendor `free()` exemption (CLAUDE.md) exactly like `blueprint.c`/`level.c` already do; the transient mutable copy `toml_parse` needs (it parses in place) is allocated and freed through the caller's own `Allocator` rather than a second allocator, confirmed safe to free before `toml_free` since tomlc99's `STRNDUP` copies every string out of the input buffer during the parse itself. Five unit tests (`save_test.c`, registered in `engine_tests`/`main_test.c` alongside `toml_emitter_test.c` since both need real `tomlc99` parsing, not fff fakes): a full round trip (several flags, one var of each typed kind, stacked items, two levels' worth of entity deltas including one inactive/"destroyed" entity, asserted on the deserialized STRUCT, order-independent via `map`/`flag` lookups rather than assumed iteration order) built and freed against `test_heap_alloc` so the whole thing runs ASan/LSan-clean; a malformed/truncated TOML string; a syntactically valid document missing the `[save]` table entirely; a minimal valid save (bare `version`+`current_level`) round-tripping to an empty-but-valid state; and an unsupported `version` value. The player's own cross-level state (health, inventory-adjacent attrs) is NOT a separate save section yet -- it rides inside whichever level's `EntityDelta` the player entity happens to occupy at save time (via S6.15b's `progression_capture_level_delta`, which must run for the current level before `save_serialize` is called), so a save taken mid-level captures the player correctly but there is no dedicated "player" TOML table the way DESIGN.md's original multiplayer-era Save Format sketch envisioned -- tracked in TODO.md as a documented gap, not attempted here. File I/O (reading/writing the actual save slot on disk) and the pause-menu Save/Load UI are S6.15d. (S6.15c, D33)
- [x] Save file I/O, autosave, and load-and-apply, completing S6.15d1 per D33 (the pause-menu Save/Load slot picker is the separate S6.15d2 slice, now also done -- see below): `save_write(Diag *, GameState *, const char *path) -> bool` (`save.c`) runs `progression_capture_level_delta` for the CURRENT level first (so the player's own position/attrs and any just-made gameplay changes are in the delta before it's captured), bundles a `SaveState`, `save_serialize`s it, backs up `path` to `path.bak` if it already exists, and writes via raylib's `SaveFileText`. The `.bak` step reuses `backup_file`, pulled out of `main.c`'s file-local `save_gamedata` into a new `file_backup.h`/`.c` (signature changed from `(GameState *, const char *)` to `(Diag *, const char *)` since `save.c` has no `GameState`-shaped backup caller of its own) -- and the small `FOPEN_READ`/`FOPEN_WRITE`/`FOPEN_APPEND` platform-mode-string block `main.c` previously defined inline moved to a new shared `fopen_mode.h` alongside it, since now two translation units need it. `save_load(Diag *, GameState *, const char *path, SaveLevelLoaderFn, void *, UndoHistory *) -> bool` (`save.c`) is "restore from a save file": `LoadFileText` the path (a missing file fails cleanly with `state` untouched), `save_deserialize` straight into `progression_arena` via an `arena_save`/`arena_restore` checkpoint rather than a scratch-buffer-then-deep-copy pass (on success the parsed `SaveState`'s flags/vars/items/level_deltas already live in the right arena and `state->progression` is simply reassigned to it; on any failure -- bad TOML, or the saved level name no longer existing in gamedata -- `arena_restore` discards the partial parse and `state->progression` is left exactly as it was, though `state->gamedata` may already reflect `game_load_gamedata`'s own partial-failure contract in the level-not-found case, same as any other reload failure), then loads the saved `current_level_name` via a caller-supplied `SaveLevelLoaderFn` (a `save.h`-local typedef structurally identical to `frame.h`'s `LevelLoaderFn` -- same four parameter types, so `production_level_loader`/`test_level_loader` pass through without a cast -- declared fresh rather than reused so `save.h` doesn't have to pull in `frame.h`'s much heavier transitive includes), re-applies that level's delta via `progression_apply_level_delta` (which restores the player's position too, since it never special-cases the player's entity id), snaps the camera, and clears + pushes a fresh undo baseline the same way every other reload path does (`frame.c`'s `run_transition_swap`, `main.c`'s `reset_editor_after_reload`). `save_autosave(Diag *, GameState *) -> bool` (`save.c`) composes `<platform_saves_dir>autosave.toml` (`platform_paths.h`, ensuring the directory exists first) and calls `save_write`. Autosave is wired through a new `AutosaveFn` (`frame.h`, matching `save_autosave`'s signature) on `FrameContext`, nullptr-is-OK like every other `FrameContext` callback (headless tests get no autosave unless a test explicitly wires one) -- production sets `frame_ctx.autosave_fn = save_autosave` and it fires from two call sites: `frame.c`'s `run_transition_swap`, right after the level swap and its own undo baseline (so the autosave's own `progression_capture_level_delta` call captures the player at the just-arrived spawn position), and `main.c`'s post-loop shutdown sequence, right after the game-loop `while` exits and before teardown begins. Both call sites treat a `false` return as non-fatal: log the error, clear it, and continue -- a failed autosave (no writable saves dir, disk full) must never abort a transition already in flight or block shutdown. A real bug surfaced and was fixed while writing the round-trip test: `toml_emitter.c`'s `emit_save_entity_delta` unconditionally emitted `active = <bool>` from `EntityDelta`'s own dedicated field, then looped over the entity's instance `attrs` with no exclusion list -- for any entity soft-destroyed via the documented `set_attr:active,false` convention (progression.h's own `EntityDelta.active` doc comment), the attrs loop then ALSO emitted an `active = ...` line for the very same key, a duplicate that tomlc99's parser rejects outright, so `save_load` failed on any save containing such an entity. Fixed with a small `entity_delta_attr_is_reserved` check in the attrs loop, mirroring the reserved-key set `save.c`'s `entity_delta_key_is_known` already uses on the parse side. Three new black-box tests (`save_test.c`): `test_save_write_then_load_round_trips_via_file` drives real interact input (give an item, set a flag, open-and-soft-destroy a chest) against a temp-file save, then directly mutates the live state (clears the flag, doubles the item count, re-closes/reactivates the chest, moves the player) before `save_load`ing the same file back and asserting every mutated field was restored -- verified to fail when `save_load`'s commit step (the `state->progression` reassignment, the `progression_apply_level_delta` call, or the level-loader call) is stubbed to a no-op; `test_save_load_missing_file_fails`; and `test_autosave_on_transition_writes_and_round_trips` drives a real transition with `frame_ctx.autosave_fn` wired to `save_autosave` and `XDG_DATA_HOME` redirected to a temp directory (the same env-var-override convention `platform_paths_test.c` already uses), asserting the autosave file lands on disk and round-trips a transition-captured player position that deliberately differs from the target level's own authored spawn point, so a broken apply step is caught rather than coincidentally matching. The editor's own gamedata Save (menu SAVE, `save_gamedata`/`backup_file` against `data/gamedata.toml`) is untouched and stays entirely separate from the player-facing save system. (S6.15d1, D33)
- [x] Pause-menu Save/Load slot picker, completing S6.15d2 per D33 (closing out the save system, sliced a-e; only the DESIGN reconciliation pass, S6.15e, remains): the pause menu gains two new entries, `MENU_ENTRY_SAVE_GAME`/`MENU_ENTRY_LOAD_GAME` (`menu.h`), placed right after the editor's `MENU_ENTRY_SAVE`/`MENU_ENTRY_RESTORE` (both pairs are persistence operations) and before `MENU_ENTRY_INVENTORY` -- the editor's raw gamedata Save/Restore is untouched and stays entirely separate from the player-facing save system, exactly like S6.15d1 left it. A new module `save_screen.h`/`.c` (`SaveScreen`) mirrors `inventory_screen.h`'s modal-overlay lifecycle (`_init`/`_open`/`_close`/`_is_open`/`_handle_input`/`_render`/`_cleanup`, world frozen while open, sharing the menu/settings/inventory blur backdrop) but the "data" is a fixed on-disk slot list rather than a data-driven snapshot: `SAVE_SLOT_COUNT` (3) numbered slots plus the autosave file as an extra LOAD-only row, `slot_exists`/`autosave_exists` populated by a `FileExists` probe at open time so the renderer can mark filled/empty rows and dim empty ones. `save_screen_entry_count(SaveScreenMode)` (3 in SAVE mode, 4 in LOAD mode) and `save_screen_nav(cursor, entry_count, direction)` (clamp-not-wrap, same convention as `inventory_screen_grid_nav`/`MenuState`'s own up/down) are pure, unit-tested functions, as are `save_screen_slot_path`/`save_screen_autosave_path` (`Strv saves_dir, int slot_index -> Str "<saves_dir>save_<slot_index + 1>.toml"`, 0-based index / 1-based on-disk name). `SaveScreen` deliberately never touches `GameState`/`Diag`/`UndoHistory` or calls `save_write`/`save_load` itself -- `save_screen_handle_input` just reports the cursor row back via a `confirmed_slot` out-param on CONFIRM (unconditionally, even for an empty LOAD slot; gating on emptiness is the caller's job since the caller already needs `slot_exists`/`autosave_exists` to resolve the real path) -- keeping the module's only dependencies alloc/blur/input/input_func/raylib/str/strv, the same reason `inventory_screen.c` never reaches into `progression.h` directly. All the GameState-shaped wiring lives in `frame.c`: `open_save_screen` (a new `dispatch_menu_action` case per new action, `MENU_ACTION_OPEN_SAVE_MENU`/`MENU_ACTION_OPEN_LOAD_MENU`) resolves `platform_saves_dir` and calls `save_screen_open`, closing the pause menu the same way `MENU_ACTION_OPEN_INVENTORY` does; a new `run_save_screen_frame` (mirrors `run_inventory_frame`'s world-freeze branch in `frame_update`) drains `confirmed_slot` into `handle_save_screen_confirm`, which resolves+ensures the saves directory, composes the path (`save_screen_slot_path`/`_autosave_path`), and calls `save_write` (SAVE mode) or `save_load` (LOAD mode, `ctx->level_loader_fn`/`ctx->undo_history` -- the same `FrameContext` fields `run_transition_swap`'s autosave hook already uses). A successful confirm closes the picker straight back to active play (never reopening the pause menu, unlike CANCEL) -- D33's spec that confirming Save/Load resumes gameplay; a failed directory resolution, a failed `save_write`/`save_load`, or a confirm on an empty LOAD slot all leave the picker open with a toast (`Saved`/`Save failed`/`Loaded`/`Load failed`/`Empty`, reusing `editor_state->toast_text`/`toast_timer` the same way `main.c`'s `menu_dispatch_save` already does) instead of silently no-opping or bouncing back to the menu -- a failed load must be visible. `MenuDispatchCtx`/`FrameContext` both gained a `SaveScreen *save_screen` field (nullptr-is-OK, same contract as `inventory`/`settings`); `main.c`/`test_helpers.c` wire a real one in, `RenderParams`/`capture_overlay_blur_if_needed`/the render call site follow the same four-way (menu/settings/inventory/save_screen) pattern the blur capture already used for three. Inserting the two new entries between `MENU_ENTRY_RESTORE` and `MENU_ENTRY_INVENTORY` shifted `MENU_ENTRY_INVENTORY`/`_SETTINGS`/`_TOGGLE_DEBUG_OVERLAY`/`_QUIT`'s ordinal values, which rippled into eight existing hardcoded down-press-count navigation tests in `integration_test.c` (all updated in the same commit; `MENU_ENTRY_SAVE`/`_RESTORE` themselves kept their values, so the much larger set of tests landing on `MENU_ENTRY_SAVE` needed no change). Tests: `save_screen_test.c` (new, fff-fake unity-build unit tests mirroring `inventory_screen_test.c`'s style, `FileExists` faked via `SET_RETURN_SEQ` to drive the open-time slot scan) covers `entry_count`/`entry_is_autosave`/`nav`/`slot_path`/`autosave_path`/`open`/`entry_exists`/`handle_input`; two new `menu_test.c` cases assert the two new `MenuAction` mappings; one new black-box `integration_test.c` case, `test_integration_save_screen_save_and_load_round_trip_via_menu`, drives the entire round trip through the real F3/DOWN/ENTER input path against a temp `XDG_DATA_HOME` (the same env-var-override convention `platform_paths_test.c`/`save_test.c`'s autosave test already use) -- give an item, open Save Game, prove the world-freeze the same way the Inventory test above does, navigate to slot index 1 and confirm (asserts `save_2.toml` lands on disk and both overlays close back to active play), mutate the live item count/player position directly, then Load Game the same slot and assert the mutation was overwritten back to the saved values -- verified to fail against a temporary stub of `handle_save_screen_confirm`'s `save_write`/`save_load` calls. (S6.15d2, D33)
- [x] DESIGN.md save-format section reconciled with the shipped save system, completing S6.15e per D33 and closing out S6.15 (sliced a-e) and Stage 6 of the open-work master plan (`work/open-work-master-plan.md`): the old `## Save System` section (a pre-S6.15 multiplayer-era sketch describing a `[[player]]`/`[[entity_state]]`/`[meta]` format that was never built) is replaced with a description of what `save.c`/`toml_emitter.c`/`save_screen.c` actually ship -- the `[save]`/`[save.vars]`/`[save.items]`/`[[level_delta]]`/`[[level_delta.entity]]` schema `toml_emit_save` emits (with a representative example verified against the emitter and `save_test.c`'s fixture), the `progression_capture_level_delta`/`_apply_level_delta` delta layer doubling as cross-transition persistence (D27/D33), `platform_saves_dir`'s per-platform save locations, the `SAVE_SLOT_COUNT` slot files plus `autosave.toml`, autosave-on-transition/-on-quit, the pause-menu Save/Load slot picker, and that the editor's own gamedata Save/Restore (`MENU_ENTRY_SAVE`/`_RESTORE`) is a separate mechanism untouched by any of this. The three gaps already tracked in TODO.md's "Level transition follow-ups" section (no dedicated cross-level player section, `save_load` not pre-seeding `prev_player_overlaps`, spawned entities not persisting across transitions) are cross-referenced rather than duplicated -- none of them were fixed here, since this slice is docs-only; no engine code changed. With S6.15e done, Stage 6 -- Gameplay systems (S6.1 PRNG through S6.15 Save/load) is complete. (S6.15e, D33)

### Phase 9 — Collision Engine Integration

Collision primitives (circle, rect, triangle) and composable shapes are implemented in
`collision.h`/`collision.c` with full pairwise resolvers and tests. The game loop now resolves
obstacles and detects triggers through the shape system; no `CheckCollisionRecs` / AABB path
remains in `engine/src/`. Slopes and one-way ledges stay parked until a level needs them.

- [x] Collision primitive types: circle, rectangle, triangle
- [x] Composable collision volumes (list of primitives per entity, combined for resolution)
- [x] Pairwise primitive resolvers (all 9 combinations) with unit tests
- [x] Wire `CollisionShape` into entity struct (collision_region/trigger_region, one-rect fallback)
- [x] Replace player/obstacle AABB resolution in game.c with `resolve_composite`
- [x] Replace simple AABB overlap in `enter`/`collide` trigger detection with shape-based overlap
- [ ] Slope and angled surface support
- [ ] One-way platforms (jump-down ledges)
- [x] Editor visualization: draw all primitives in debug mode (collision green, trigger yellow)
- [x] `[[blueprint.collision]]` composite authoring: parse a primitive list
  (rect/circle/triangle) into a blueprint `CollisionShape`, deep-copy into
  each instantiated entity's `collision_region` so composite shapes drive
  resolution and triggers, and emit + round-trip. Blueprints without it
  keep the one-rect `collision_offset`/`collision_size` attr fallback.

## Resolved Decisions

- **Data format:** TOML via tomlc99 (vendored in `engine/vendor/tomlc99/`). Single file `data/gamedata.toml`.
- **Safe save:** Write to temp file, rename onto the original. Atomic on same filesystem. Prevents corrupt partial writes from Syncthing races.
- **Release distribution:** Load `gamedata.toml` from filesystem at runtime on both platforms.
- **Engine grows organically.** Don't build engine features speculatively — add them when the game needs them.
- **Hot-reload:** Poll mtime on `gamedata.toml` (~once per second) in play mode — auto-reload when the file changes (Syncthing edits from phone appear live). In editor mode, no auto-reload — reload is explicit only, to avoid blowing away unsaved in-memory changes.
- **Tile map:** 16x16 pixel tiles, 2 layers (ground + overlay). Ground is terrain (grass, dirt, water, paths). Overlay renders on top of ground but under entities (flowers, puddles, shadows). Stored as arrays of integer tile IDs in TOML, row by row. Autotiling (automatic edge/corner sprite selection) is an editor feature — the file stores concrete tile IDs, the editor computes them on placement.
- **TOML emitter:** Clean regeneration, no comment/formatting preservation. The in-game editor is the primary editing interface — comments aren't useful. Keeps the emitter dead simple.
- **Camera system:** Smooth follow with lerp interpolation (`CAMERA_FOLLOW_SPEED = 10.0F`). Camera tracks player position each frame, clamped to level edges so the viewport never shows outside the level. When the level is smaller than the viewport, camera locks to the level center. `game_snap_camera()` teleports the camera on level load/transitions (no lerp) and also clears any in-flight `camera_pan`/`camera_shake` effect (S6.5) so it can't bleed into the new level. Editor camera is unaffected — still free-roaming. `camera_pan:x,y,duration` overrides follow for its duration (lerping via `camera_pan_position`, then follow resumes); `camera_shake:magnitude,duration` is a decaying random jitter added only at the camera assembly point (`main.c`), never written into `camera_target` itself.
- **Dialogue system:** Blocking, per D24 — `dialogue:text` opens a paged, typewriter-revealed box and suspends the triggering rule (via the same continuation mechanism `wait:` uses) until the player closes it, so any actions after `dialogue:` in the same rule only run once the box is gone. Multi-page text splits `text` on `|`; CONFIRM skips a mid-reveal page to fully shown, then advances to the next page, then closes on the last. The world (player movement, rules, timers) freezes while a dialogue is open, mirroring the pause menu. Only one dialogue can be active at a time — see TODO.md. Portraits/names, branching choices, and localization are still open (see Open Questions below).

## Open Questions

### Combat
- Resolved by D26 (S6.10a): hitbox/hurtbox are named collision regions separate from the physical collision/trigger regions (D28), live while `hitbox_active_timer > 0` (tied to attack-frame windows once S6.10b lands); damage = `max(1, attacker damage - target defense)`; i-frames default to 0.8s (`iframes` attr) with render flicker; a hit crossing health from > 0 to <= 0 fires the existing `defeat` trigger, gated against re-firing on an already-defeated entity. Implementation: `entity_hitbox_region`/`entity_hurtbox_region`/`entity_apply_damage` (`entity.c`), `detect_melee_damage`/`tick_combat_timers` (`game.c`).
- Knockback direction/distance — resolved by D26 (impulse along attacker->target, `knockback` attr = distance in px, decaying over 0.15s via a per-entity offset field) and implemented (S6.10c): `entity_apply_knockback`/`tick_knockback` (`entity.c`/`game.c`).
- Ranged attacks — resolved by D26 (projectile blueprints via `spawn:` + a straight-line mover behavior) and implemented (S6.10d): `behavior_projectile`/`entity_can_deal_contact_damage` (`game.c`), the `SpawnRequest.facing` field (`effect.h`/`effect.c`/`rule.c`/`frame.c`).
- Attack-frame hitbox timing and hurt/death animation states — resolved by D31 and implemented (S6.11b), completing S6.11: `entity_hitbox_is_active` (`entity.c`/`.h`) replaces the old flat `hitbox_active_timer > 0` gate with `attack_state_timer > 0 && frame_index` inside the scoped `attack_hit_frame_start`/`attack_hit_frame_end` window; `begin_hurt_state`/`begin_death_state`/`tick_death_state`/`resolve_effective_anim_state` (`game.c`) wire damage/defeat into the `hurt`/`death` states with priority over walk/idle/attack. See the S6.11b roadmap bullet above for the full mechanism.

### Inventory & Items
- Resolved by D25 (S6.8a/S6.8b): unlimited list (no slot cap), stackable by count, `give_item`/`has_item`/`remove_item` work off a name -> count map (`ItemSet`, `ProgressionState.items`) rather than item entities; a pickup shows a "Got <item>" toast via S1.2's toast surface.
- Visual inventory screen layout — resolved by D25/D34 and implemented (S6.12b): a modal pause-menu overlay (mirrors the Settings overlay's lifecycle, world frozen while open) renders the snapshot as a gamepad-navigable 4-column grid of `<name> x<count>` cells. See the S6.12b roadmap bullet above (Phase 8) for the mechanism. Implementation: `InventoryScreen`/`inventory_screen_collect_items`/`_grid_nav`/`_cell_position`/`_render` (`engine/src/inventory_screen.h`/`.c`).
- Equipment vs consumables vs key items — different categories?
- Equipment that modifies attributes (e.g. sword child entity with `damage` attribute)?
- Per-item actions from the inventory grid (use/equip/drop) — v1 (S6.12b) is read-only, CONFIRM is a no-op on a selected cell.

### Dialogue System
- NPC portraits/names alongside text?
- Branching choices — player picks from options?
- Localization considerations?

### Level Transitions
- Resolved by D27 (S6.14): fade to black -- 0.3s fade out, swap at the fully-black midpoint, 0.3s fade in, input suppressed throughout, implemented as a pure `TransitionFade` phase/timer state machine. See the S6.14 roadmap bullet above (Phase 8) for the mechanism.
- Door animations before transition?
- Spawn position — defined per-door, or level-wide default?
- Entering from different directions — multiple spawn points per level?
- Do entities/state persist when leaving and returning to a level?

### Animation State Machine
- Resolved by D31 (S6.11a): directional sprites are 4-way (up/down/left/right, `direction` attr), not 8-way -- left/right share one sprite-sheet row, mirrored via `flip`. `state`/`direction` are built-in scoped string attrs behaviors set (default "idle"/"down"); a generic per-frame pass (`advance_entity_animation`, `game.c`) looks up a `[[blueprint.animation]]` clip for the entity's `state` and derives `anim_row`/`flip`/`frame_index` from it. Per-entity overrides vs blueprint defaults: resolved as blueprint-only for now -- clips are looked up live via the entity's blueprint (`entity_resolve_blueprint`), not copied onto the instance, so there is no instance-level clip override yet; adding one later is a straightforward extension of the same lookup (check instance attrs before falling back to the blueprint's `[[blueprint.animation]]`, mirroring how every other scoped attr already works). Implementation: `AnimClip`/`blueprint_find_anim_clip` (`blueprint.h`/`.c`), `advance_entity_animation`/`update_entity_anim_attrs` (`game.c`), `draw_animated_entity` (`main.c`).
- Idle/walk chain (state set by movement behaviors) is resolved and implemented (S6.11a) per the bullet above. Attack/hurt/death chaining, and which attack frames have active hitboxes, are resolved by D31 and implemented (S6.11b) -- see the Combat section above.

### HUD & Menus
- Health display: resolved by D34 (S6.12a) -- hearts, in halves, top-left, from the player's scoped `health`/`max_health` attrs, drawn in screen space after the world render and before the menu/settings overlays. See the S6.12a roadmap bullet above (Phase 8) for the mechanism. Placeholder shapes today, pending a real heart sprite (TODO.md). The minimap remains open.
- Inventory screen — resolved by D25/D34 and implemented (S6.12b): a modal pause-menu overlay, not full-screen, mirroring the Settings overlay. See the Inventory & Items section above and the S6.12b roadmap bullet (Phase 8) for the mechanism.
- Pause menu — Resume, Save, Restore, Inventory, Settings, Toggle Debug
  Overlay, Quit. Settings opens the keybindings rebind UI; Inventory
  opens the item grid overlay; the rest dispatch the corresponding
  action. Implemented in `engine/src/menu.c`.
- Minimap or full map screen?
- All menus gamepad-navigable with on-screen button hints.

### Keybindings Settings UI

Pause menu's "Settings" entry opens a three-screen overlay
(`engine/src/settings.c`):

- **List screen** — scrollable list of every `InputAction` and
  `InputAxis` plus a final "Reset all to defaults" entry. Each row
  shows the action / axis name and its current binding label
  (rendered via `input_func_label`). Confirm enters Detail; Cancel
  closes back to the menu.
- **Detail screen** — per-target view listing each existing
  alternative, plus "Add new alternative" and "Reset to defaults"
  rows. Confirm on an alternative enters Capture; `EDITOR_DELETE`
  removes one; Cancel returns to List.
- **Capture screen** — high-water-mark chord capture for actions:
  each frame, atoms that transition from not-held to held (a
  release-edge, tracked against the previous frame's held set) join
  a peak set; finalize when the held set becomes empty after at
  least one atom was captured. Tolerates out-of-sync release order
  so Ctrl+Shift+Z resolves correctly regardless of which key the
  user lifts first. Axis capture has its own three-flow state
  machine: stick deflection or trigger pull finalizes a single
  `ATOM_GP_AXIS` / `ATOM_GP_TRIGGER`; a key press transitions to a
  "now press the positive direction" prompt that finalizes
  `ATOM_KB_AXIS{neg, pos}`.

`KEY_ESCAPE` and `GAMEPAD_BUTTON_MIDDLE_RIGHT` (Start) are
**reserved cancels**: read raw and never bindable from the UI, so
binding `ACTION_CANCEL` elsewhere cannot trap the user inside the
capture screen. Action-chord capture accumulates by release edge:
`capture_prev_held` snapshots the atoms already held the moment
capture opens, and each frame only atoms that transition from
not-held to held join the chord, so the Confirm press that opened
the screen is excluded without needing an arming frame - a later
re-press of that same key during capture is a fresh edge and can
still be bound. Axis capture is a separate, simpler path and keeps
its own `capture_axis_armed` wait for one no-input frame before it
starts looking for stick/trigger/key input.

Persistence: every successful mutation sets
`SettingsState.save_requested`; the frame dispatcher forwards that
to a host-supplied `KeybindingsSaveFn`. Production wires this to
`save_keybindings` in main.c, which serializes the BindingStore via
`toml_emit_bindings` and writes to `KEYBINDINGS_PATH`
(`data/keybindings.toml` on desktop, the Syncthing path on
Android). At startup, `input_func_load_defaults` populates defaults
and `input_func_load_bindings_toml` overlays the file on top so
user customizations win.

Settings shares the menu's blur backdrop and reopens fresh after
hot-reload (its memory lives in `gamedata_arena`).

### Preferences and Path Configuration

Beyond keybindings, Settings exposes a "General" tab that surfaces
runtime preferences via a small `Preferences` struct
(`engine/src/preferences.{h,c}`). v1 carries a single field,
`data_dir`, that controls where `gamedata.toml`, `keybindings.toml`,
and `trace.log` are read and written. Defaults match the pre-prefs
constants — `data/` on desktop, `/storage/emulated/0/Sync/sleipner/`
on Android — so behavior is unchanged out of the box.

**On-disk file:** `preferences.toml` at the OS-conventional config
location resolved by `engine/src/platform_paths.c`:

- Linux/BSD: `$XDG_CONFIG_HOME/sleipner/preferences.toml` if
  `$XDG_CONFIG_HOME` is set, otherwise
  `$HOME/.config/sleipner/preferences.toml`.
- Windows: `%APPDATA%/sleipner/preferences.toml`.
- Android: `<internalDataPath>/preferences.toml` via raylib
  `GetApplicationDirectory()`.
- A `<binary_dir>/preferences.toml` next to the executable trumps
  the OS path if it exists, supporting portable installs.

The file does not live inside `data_dir` — that would be a
chicken-and-egg cycle, since `preferences.toml` is what overrides
`data_dir`. The OS-conventional parent directory is created on
first save via raylib `MakeDirectory` (covered by
`platform_ensure_parent_dir`).

**Schema:**

```
[paths]
data_dir = "/path/to/dir/"
```

**Loading:** missing file is silent (defaults remain). Parse error
sets `ErrorState` and proceeds with defaults so a corrupt user file
never blocks startup. Fields not present in the file keep whatever
value `Preferences` already held, so adding new keys later does not
break old saved files.

**Path Picker UX (Settings → General → Data directory):** a hybrid
screen with three modes that share one buffer.

- BROWSE (default): raylib `LoadDirectoryFilesEx(buf, "DIRS*",
  false)` lists subdirectories. The list always starts with a
  synthesized `<USE THIS DIRECTORY>` row at index 0 — the screen
  opens with that row selected, so a single CONFIRM (gamepad
  RIGHT_FACE_DOWN, keyboard Enter) commits the seeded `data_dir`
  and exits. A `<SELECT DRIVE>` row follows at index 1, then a
  `..` row when not at filesystem root, then subdirectories.
  CONFIRM dispatches per row: USE_THIS commits, SELECT_DRIVE
  enters DRIVE_SELECT mode, `..` goes up, anything else enters
  that folder. NAV_UP/DOWN and PAGE_UP/DOWN move the cursor;
  CANCEL goes up one level (or exits at root). One action verb
  (CONFIRM) means no binding clash — the earlier draft used
  `ACTION_INTERACT` for commit, which shared the gamepad south
  face with `ACTION_CONFIRM` and broke "enter folder" on gamepad
  entirely.

  Both the seed and every refresh run through `path_normalize`
  (backslash → slash, collapse runs of slashes) and
  `path_edit_make_absolute` (prepend `GetWorkingDirectory()` if
  the buffer is relative). The first defends against raylib's
  `_WIN32` path joiner emitting `/\data` from `basePath="/"` and
  `entry="data"`; the second defends against the default seed
  `data/` falling off the end of `GetPrevDirectoryPath` after one
  `..` press. Commit re-normalizes once more before writing the
  Str so a path typed via KEYBOARD mode lands forward-slash-only.
- DRIVE_SELECT: presents filesystem roots. On Windows
  `GetLogicalDrives()` produces `A:/` … `Z:/`; on POSIX a single
  `/` is shown so the user always has a "jump to root" shortcut
  inside the picker. CONFIRM picks a drive and returns to BROWSE
  rooted at it; CANCEL returns to BROWSE without changing buf.
- KEYBOARD: reuses the two-level radial `KeyboardWidget` from
  `engine/src/keyboard_widget.{h,c}` (extracted from the editor's
  word builder so it has no editor dependency). Type into the
  same buffer. ACTION_KEYBOARD_BACKSPACE (default Backspace key,
  gamepad Y / RIGHT_FACE_UP) deletes one character; CANCEL always
  exits the widget in a single press, no matter how deep into a
  sub-group the user has navigated. The host treats
  `KB_RESULT_EXIT_REQUESTED` as "drop back to BROWSE" rather than
  committing — commit always goes through BROWSE's USE_THIS row,
  so the user cannot accidentally save by emptying the buffer.

ACTION_WB_KEYBOARD_MODE (gamepad RIGHT_FACE_LEFT, keyboard Delete) toggles
between BROWSE and KEYBOARD without leaving the screen.

**Trace log two-stage init:** `debug_init` opens trace.log at the
boot-default path (compile-time) before preferences load, then
`debug_reopen_trace` switches to the `data_dir`-resolved path once
preferences are available. Append mode preserves any boot-stage
content. When `data_dir` matches the boot path the reopen is a
fast close + reopen at the same location.

**Tab system:** the LIST screen now renders a tab header. Two new
universal `InputAction` values, `ACTION_TAB_PREV` / `ACTION_TAB_NEXT`
(gamepad L1/R1, keyboard Shift+Tab/Tab), switch tabs at the LIST
level. They share L1/R1 with `ACTION_PAGE_UP/DOWN`, so the list
handler checks `TAB_*` first and early-returns — same
order-sensitivity pattern as `ACTION_EDITOR_UNDO` vs
`ACTION_NAV_LEFT` on L1+Left.

### AI & Pathfinding
- Enemy behaviors beyond static — chase, patrol, flee, guard?
- Aggro range — attribute-driven? Line of sight or radius?
- Pathfinding — grid-based A*, or simple steering?
- Patrol routes — defined as waypoint lists in TOML?
- Group behavior — enemies coordinating, flanking?

### Audio Design
- Music crossfade on level transition — RESOLVED (S6.13b, D32): 1.0s linear crossfade, `MUSIC_CROSSFADE_SECONDS`, keyed on the level's `music` track name via an embedded registry.
- Ambient sound layers — per-level, per-area?
- Spatial sound — volume based on distance to entity? (deferred from S6.13b, see TODO.md)
- Sound priority — what happens when too many sounds play at once?
- Music and SFX volume as separate settings?

### Asset Pipeline
- Assets are loaded from the filesystem at runtime — does this require a bundling strategy for distribution?
- Atlas packing — manual or automated as part of the build?
- Should large assets (music, tilesets) use a different strategy than small ones (icons, UI)?

### Collision System Evolution

The primitive and composite resolver layer is implemented (`CollisionPrimitive` /
`CollisionShape` in `collision.h`). The remaining work is integration: replacing `Rectangle
collision` on Entity with `CollisionShape`, wiring `resolve_composite` into the game loop,
and adding named regions (collision vs trigger vs hitbox). See Phase 9 in the roadmap.

#### Named regions (planned)

Separate collision and trigger volumes on each entity, replacing the single `Rectangle collision` field. `collision_region` (physics resolution, blocks movement) and `trigger_region` (enter trigger detection) landed first; `hitbox`/`hurtbox` (attack damage regions) landed in S6.10a. An empty shape (`.prims.count == 0`) means the region is absent — no special sentinel needed.

#### Decomposability

Each primitive is convex by construction. Arbitrary polygons — including concave ones — are
representable by composing triangles (triangulation). There is no concave polygon primitive
and none is needed: the composition mechanism provides it for free.

### Modding
- The data-driven architecture makes modding nearly free — worth designing for explicitly?
- Mod loading — override gamedata.toml entries, or separate mod files merged at load?
- Asset overrides — can mods replace textures/sounds?
- Mod conflicts — what happens when two mods modify the same entity?
- Should the editor support exporting mods as standalone packages?
