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

### Two Files, Two Concerns

- **`gamedata.toml`** = the game (level design, blueprints, rules). Shared via Syncthing, versioned in git. Never contains player progress.
- **Save file** = runtime world state. One save captures the complete host state — all players, all global variables, all entity modifications. It's the world, not a character sheet.

### Storage Location

Save files live in app-local storage, **not** Syncthing. Syncthing is for creative data (gamedata.toml). Saves are runtime state — syncing them would cause conflicts if two devices play simultaneously.

- Desktop: `~/.local/share/sleipner/saves/`
- Android: app internal storage

### Persistent World State

The world has one persistent state. Everything persists by default:
- Kill an enemy → it stays dead.
- Open a chest → it stays open.
- Move an object → it stays moved.

Respawning is explicit — a timer rule, a `fire_event:respawn_enemies` trigger, or a design decision in the rules. The game designer decides what resets, not the engine.

### Save Format

TOML, same parser/emitter as gamedata. The save stores the **delta from the gamedata baseline** — only what changed. Anything not in the save is at its blueprint/level default. This keeps saves small.

```toml
[meta]
timestamp = 2026-03-16T19:30:00
playtime = 3600

# All players in this world
[[player]]
name = "player_1"
level = "overworld"
position = [320, 180]
health = [7, 10]
direction = "down"
items = ["sword", "key", "potion"]
equipment = { main_weapon = "iron_sword", shield = "wooden_shield" }

[[player]]
name = "player_2"
level = "overworld"
position = [340, 180]
health = [10, 10]
items = ["bow", "arrow", "arrow"]

[globals]
chest_1_opened = true
boss_defeated = false
bridge_repaired = true

# Entities modified from their gamedata baseline
# Only stores the delta — what changed from blueprint/level defaults
[[entity_state]]
level = "overworld"
entity_id = 6
destroyed = true

[[entity_state]]
level = "dungeon_1"
entity_id = 3
attrs = { health = [2, 5] }
```

### Loading

1. Load `gamedata.toml` — the designed world (blueprints, levels, rules).
2. Apply `entity_state` deltas from the save file on top — destroyed entities are removed, modified attributes are overridden.
3. Restore player positions, inventories, equipment.
4. Restore global variables.

### Save Slots

Multiple numbered files: `save_1.toml`, `save_2.toml`, etc. Each stores a timestamp and current level name for the slot selection screen. Auto-save goes to a dedicated `autosave.toml` that doesn't overwrite manual saves.

### Auto-Save

- On every level transition.
- On quit.
- Manual save via pause menu.

### Multiplayer

The host owns the save. All players are stored in the same save file. When a player joins, their character section is created. When they leave, their state persists in the save for when they rejoin.

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
- [ ] Multiple player entities with independent input sources
- [ ] Per-player camera (each client follows own player)
- [ ] Host-authoritative game loop (host simulates, clients send inputs)
- [ ] State sync: attribute deltas over UDP
- [ ] Reliable channel for events (rule triggers, item pickups)
- [ ] Full state snapshot on player join
- [ ] LAN discovery (broadcast/listen)
- [ ] Collaborative editor: multiple cursors, entity locking, live sync
- [ ] Per-player undo in editor
- [ ] Host-only save with save requests from clients

### Phase 7 — Distribution
- [ ] Embed `gamedata.toml` in binary for release builds (desktop)
- [ ] Bundle `gamedata.toml` in APK assets for release (Android)
- [ ] Dev mode flag to load from filesystem instead of embedded

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
- [ ] Level transitions
- [x] HUD inventory screen + pause-menu entry (S6.12b) -- hearts (S6.12a) done above per D34; the inventory grid itself is the S6.12b roadmap bullet directly above.
- [ ] AI & pathfinding (patrol, aggro, chase)
- [ ] Audio (music crossfade, spatial sound, ambient layers)
- [ ] NPC behaviors (patrol, dialogue)
- [ ] Save/load system (persistent world state, delta from gamedata, auto-save, slots)

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
- Transition effect — fade to black, wipe, instant cut?
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
- Music crossfade on level transition — duration, curve?
- Ambient sound layers — per-level, per-area?
- Spatial sound — volume based on distance to entity?
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
