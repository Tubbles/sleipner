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
| `dialogue:text` | Show dialogue box |
| `transition:level,x,y` | Go to level at position |
| `spawn:blueprint,x,y` | Create entity at position |
| `destroy` | Remove the entity |
| `camera_pan:x,y,duration` | Pan camera over time |
| `camera_shake:duration` | Screen shake effect |
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
  "camera_shake:0.5",
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

Binary resources: sprites, music, sound effects. These rarely change, are large, and don't diff well. Checked into git as-is. Loaded at runtime from the filesystem via raylib.

```
assets/
├── sprites/       # .png sprite sheets and tiles
├── music/         # .mp3/.ogg background tracks
└── sfx/           # sound effects
```

### Game Data (`data/gamedata.txt`)

All game data — blueprints, levels, tile palettes — lives in a **single file**. This is the creative output that the editor reads and writes. Must be:

- **Git-friendly:** Plain text, line-oriented, diffs show meaningful changes.
- **Live-editable:** The game reads/writes this file at runtime.
- **Synced:** Hard-linked into the Syncthing directory so both git and Syncthing see the same inode.

Why a single file: hard links only work on files, not directories. A single file lets us hard-link it into the Syncthing share so edits from either side (phone or dev machine) are instantly visible to both git and Syncthing without symlink indirection.

### Syncthing Pipeline

```
  Phone (editor)                       Dev machine (git)
  ──────────────                       ─────────────────
  Edit in-game, save                   Edit by hand or via Claude
        ↓                                    ↓
  ~/Sync/sleipner/gamedata.txt  ←→     data/gamedata.txt
  (hard link to same inode             (hard link to same inode
   on dev machine)                      in Syncthing share)
                                             ↓
                                       git commit & push
```

**Setup:** `ln data/gamedata.txt ~/Sync/sleipner/gamedata.txt`

**Desktop path:** Game reads `data/gamedata.txt` (relative to binary / repo root).
**Android path:** Game reads from Syncthing folder (e.g. `/storage/emulated/0/Sync/sleipner/gamedata.txt`).

Both sides edit the same file. Syncthing syncs to the phone. Git versions the repo copy. The hard link means they are the same file on the dev machine — no copy step needed.

Note: hard links break when either side does a delete+recreate instead of in-place write. Syncthing and most text editors preserve inodes on save, but this is worth keeping in mind.

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

## Roadmap

### Phase 1 — Foundation (DONE)
- [x] Tiled grass background
- [x] Player avatar with animation and gamepad control
- [x] Static obstacles with AABB collision (hardcoded in main.c)
- [x] Depth-sorted rendering (by collision bottom edge Y)
- [x] Background music (embedded mp3)
- [x] Debug overlay (F3 / gamepad Select — collision boxes, info panel, scrolling log)
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
- [ ] Entity composition (children with relative positioning)
- [ ] Tag system (named references within composition trees)

### Phase 4 — Rule Engine
- [ ] Rule struct (trigger + conditions + action tree)
- [ ] Trigger evaluation (interact, enter, collide, defeat, timer, event, on_spawn, attr_changed)
- [ ] Condition evaluation (has_item, flag, attr, var, entity_count, AND/OR/NOT grouping)
- [ ] Action execution (one C function per action type)
- [ ] Control flow nodes (if/else, repeat, for-each with entity queries)
- [ ] Variable system (local per-execution, global persistent, $ references in parameters)
- [ ] Subroutines (named reusable action sequences, callable via `call:`)
- [ ] Custom events (fire_event / event trigger, cross-entity decoupling)
- [ ] Timer management (create, destroy named timers)
- [ ] Flag storage (syntactic sugar over boolean global variables)

### Phase 5 — Editor Mode
- [ ] Toggle play/editor mode (instant, shared world state)
- [ ] On-screen button hints (context-sensitive, always visible)
- [ ] Free camera with cursor
- [ ] Browse mode: select entities, inspect attributes
- [ ] Edit mode: move entities, resize collision boxes with visual handles
- [ ] Scene mode: place/move/delete entities, inspect properties
- [ ] Blueprint mode: create/edit blueprints (texture, collision, behavior, attributes, rules, children)
- [ ] Tile mode: paint ground and overlay layers, tile palette
- [ ] Atlas mode: view textures, define named source rects, preview sprites
- [ ] Animation mode: define frame sequences, preview playback, link directional states
- [ ] Rule mode: visual trigger/condition/action editor, test by switching to play
- [ ] Level mode: create levels, set size/music/spawn/transitions
- [ ] Radial picker widget
- [ ] Scroll picker widget
- [ ] Word builder (seeded vocabulary + gamedata vocabulary)
- [ ] Fuzzy finder for existing names
- [ ] Value adjuster with visual feedback
- [ ] Gamepad keyboard (last resort)
- [ ] Attribute editor (built-in + custom, with diff view)
- [ ] Child entity editor (composition, tags)
- [ ] Undo (snapshot-based, arena memcpy)
- [ ] Save to gamedata.toml (TOML emitter, atomic write)
- [ ] Attribute watcher (pin to debug overlay, live values during play)

### Phase 6 — Multiplayer
- [ ] Input source abstraction (decouple player behavior from hardcoded gamepad)
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
- [ ] Proper PRNG (xoshiro256 or similar, replacing `rand()` in particle system)
- [ ] Camera system (follow, bounds, transitions)
- [ ] Combat (hitboxes, damage, knockback, i-frames)
- [ ] Inventory & items
- [ ] Dialogue system
- [ ] Level transitions
- [ ] Animation state machine (directional, idle/walk/attack/hurt)
- [ ] HUD (health, inventory screen, pause menu)
- [ ] AI & pathfinding (patrol, aggro, chase)
- [ ] Audio (music crossfade, spatial sound, ambient layers)
- [ ] NPC behaviors (patrol, dialogue)
- [ ] Save/load system (persistent world state, delta from gamedata, auto-save, slots)

## Resolved Decisions

- **Data format:** TOML via tomlc99 (vendored in `engine/vendor/tomlc99/`). Single file `data/gamedata.toml`.
- **Sync mechanism:** Two separate copies — `data/gamedata.toml` in the repo (versioned, read by desktop game) and `~/Sync/sleipner/gamedata.toml` (Syncthing-managed, read by Android game). Kept in sync by explicit copy with backup (see CLAUDE.md "Gamedata Sync Workflow"). Hard links do not work because Syncthing's atomic write (temp + rename) breaks them.
- **Safe save:** Write to temp file, rename onto the original. Atomic on same filesystem. Prevents corrupt partial writes from Syncthing races.
- **Android data path:** `/storage/emulated/0/Sync/sleipner/gamedata.toml` (hardcoded). Desktop: `data/gamedata.toml` (repo-relative).
- **Release distribution:** Load `gamedata.toml` from filesystem at runtime on both platforms.
- **Engine grows organically.** Don't build engine features speculatively — add them when the game needs them.
- **Undo system:** Snapshot-based. Before each editor operation, snapshot the entire in-memory gamedata and push onto a history stack. Undo = pop and restore. Simple, every operation is automatically undoable, no need to define inverse operations. Gamedata is small enough that even 100+ snapshots are negligible memory. If gamedata ever grows to megabytes, migrate to command pattern — undo is internal to the editor so refactoring is cheap.
- **Memory allocation:** Arena allocator for all gamedata. All data loaded from TOML lives in one arena — reload or undo = reset the arena. Behavior params per blueprint are variable-length (pointer + count into the arena), no fixed cap. Undo snapshots are just `memcpy` of the arena.
- **Hot-reload:** Poll mtime on `gamedata.toml` (~once per second) in play mode — auto-reload when the file changes (Syncthing edits from phone appear live). In editor mode, no auto-reload — reload is explicit only, to avoid blowing away unsaved in-memory changes.
- **Tile map:** 16x16 pixel tiles, 2 layers (ground + overlay). Ground is terrain (grass, dirt, water, paths). Overlay renders on top of ground but under entities (flowers, puddles, shadows). Stored as arrays of integer tile IDs in TOML, row by row. Autotiling (automatic edge/corner sprite selection) is an editor feature — the file stores concrete tile IDs, the editor computes them on placement.
- **TOML emitter:** Clean regeneration, no comment/formatting preservation. The in-game editor is the primary editing interface — comments aren't useful. Keeps the emitter dead simple.
- **Save system:** One save = one world (all players, all state). Saves in app-local storage (not Syncthing). Persistent world — everything persists by default, respawning is explicit via rules. Save stores delta from gamedata baseline. TOML format. Auto-save on level transitions and quit. Multiple numbered slots + autosave.

## Open Questions

### Camera System
- Zelda-style screen-by-screen (snap to room boundaries) or smooth follow?
- Bounds clamping — camera stops at level edges?
- How does `camera_pan` interact with the follow camera?
- Transition effect when moving between rooms/areas?
- Zoom levels — fixed or adjustable?

### Combat
- Melee hitbox — separate from collision box? Active only during attack frames?
- Damage model — how do attack power, defense, and attributes interact?
- Knockback direction and distance — attribute-driven?
- Invincibility frames after taking damage — how many?
- Ranged attacks — projectile entities spawned by rules?
- How does combat interact with the rule system (e.g. `defeat` trigger)?

### Inventory & Items
- How many slots? Fixed grid, or unlimited list?
- Equipment vs consumables vs key items — different categories?
- Visual inventory screen layout — gamepad-navigable grid?
- How does `give_item`/`has_item`/`remove_item` work in memory — string set, or item entities with attributes?
- Stackable items?
- Equipment that modifies attributes (e.g. sword child entity with `damage` attribute)?

### Dialogue System
- Multi-page text with A to advance?
- NPC portraits/names alongside text?
- Branching choices — player picks from options?
- How does dialogue interact with rules (e.g. set flag after conversation)?
- Text speed — instant or typewriter?
- Localization considerations?

### Level Transitions
- Transition effect — fade to black, wipe, instant cut?
- Door animations before transition?
- Spawn position — defined per-door, or level-wide default?
- Entering from different directions — multiple spawn points per level?
- Do entities/state persist when leaving and returning to a level?

### Animation State Machine
- Directional sprites — 4-way (UDLR) or 8-way?
- States: idle, walk, attack, hurt, death — how do they chain?
- How do animations tie into attributes (e.g. `state` attribute)?
- Attack animation frames — which frames have active hitboxes?
- Per-entity animation overrides vs blueprint defaults?

### HUD & Menus
- Health display — hearts, bar, or numeric?
- Inventory screen — overlay or full-screen?
- Pause menu — resume, save, settings, quit?
- Minimap or full map screen?
- All menus gamepad-navigable with on-screen button hints.

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
- Current system is AABB-based — will slopes or angled surfaces be needed?
- One-way platforms (e.g. jump-down ledges)?
- Trigger volumes of arbitrary shape (circles, polygons)?
- How do trigger zones interact with the rule system?

### Modding
- The data-driven architecture makes modding nearly free — worth designing for explicitly?
- Mod loading — override gamedata.toml entries, or separate mod files merged at load?
- Asset overrides — can mods replace textures/sounds?
- Mod conflicts — what happens when two mods modify the same entity?
- Should the editor support exporting mods as standalone packages?
