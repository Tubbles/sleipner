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
- **Round-trip.** Play, pause, edit, resume. The editor and the game share the same world state.

### Editor Modes

1. **Browse mode** — Free camera, select entities, inspect properties.
2. **Place mode** — Pick from a blueprint palette, place instances in the world.
3. **Edit mode** — Modify the selected entity: move, resize collision box, change sprite, tweak parameters.
4. **Tile mode** — Paint terrain tiles on the grid.

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

### Rules (Triggers / Conditions / Actions)

Rules make game logic data-driven. Each entity can have zero or more rules. A rule is: one **trigger**, zero or more **conditions** (all must be true), and a list of **actions** (executed in order).

**Triggers** — what fires the rule:

| Trigger | Fires when |
|---|---|
| `interact` | Player presses A near the entity |
| `enter` | Player steps into the entity's zone |
| `collide` | Entity touches another entity |
| `defeat` | Entity health reaches 0 |
| `timer:N` | N seconds after level load or after another event |

**Conditions** — optional gates:

| Condition | True when |
|---|---|
| `has_item:X` | Player has item X |
| `flag:X` | Global flag X is set |
| `not_flag:X` | Global flag X is not set |
| `attr:X` | Attribute X on self is truthy |
| `attr:X<N` | Numeric attribute comparison |
| `not_attr:X` | Attribute X on self is falsy |
| `tag.attr:X` | Attribute X on tagged family member is truthy |

**Actions** — what happens:

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

**TOML example:**

```toml
[[blueprint]]
name = "locked_chest"
texture = "chest.png"
src = [0, 0, 16, 16]
collision_offset = [0, 0]
collision_size = [16, 16]
behavior = "static"
is_locked = true

[[blueprint.rule]]
trigger = "interact"
conditions = ["self.attr:is_locked", "has_item:key"]
actions = [
  "remove_item:key",
  "set_attr:self.is_locked,false",
  "change_sprite:16,0,16,16",
  "give_item:sword",
  "play_sound:chest_open.wav",
  "dialogue:You found a sword!",
]
```

The C side has one function per action type. The rule engine loops: check trigger → check conditions → execute actions. Adding a new trigger/condition/action type is one C function — automatically available in the editor.

**Flags** are a global string set that persists with save data. They gate progression (e.g. "boss_defeated", "bridge_repaired") and allow rules to respond to world state.

### Live Edit/Play Flow

- **Toggle is instant** — shared world state, no reload delay.
- **Edit mode**: game paused, free camera, select and modify attributes. Changes are visible immediately — move a collision box and see it update live.
- **Play mode**: game runs with current state. Changes made in edit mode are in effect.
- **Play-from-here**: resume without resetting state (test a specific scenario mid-action).
- **Restart level**: reload from gamedata, reset all runtime state.
- **Attribute watcher**: pin attributes to the debug overlay — see them update in real time during play. Pick entities, pick attributes, they display as a live HUD.

## Data Architecture

Game data is split into two concerns with separate storage:

### Asset Database (`assets/`)

Binary resources: sprites, music, sound effects. These rarely change, are large, and don't diff well. Checked into git as-is. Embedded via `#embed` on desktop, bundled in APK assets on Android.

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
- [ ] Headless engine mode (init without window/audio for testing)
- [ ] Split game loop into update (pure logic) and render (side effects)
- [ ] Integration test harness (load gamedata, feed inputs, assert state)
- [ ] Arena allocator for gamedata memory
- [ ] Load blueprints into blueprint lookup table
- [ ] Instantiate level entities from gamedata (replace hardcoded obstacles)
- [ ] Remove hardcoded obstacle data from main.c
- [ ] Hot-reload: poll mtime in play mode, reload on change
- [ ] TOML emitter for editor save

### Phase 3 — Entity System
- [ ] Attribute system (built-in + custom, typed key-value pairs)
- [ ] Blueprint/instance scoping (instance overrides, blueprint fallback)
- [ ] Blueprint inheritance (extends)
- [ ] Entity storage (flat array in arena)
- [ ] Convert Player and Obstacle to entity instances with attributes
- [ ] Entity composition (children with relative positioning)
- [ ] Tag system (named references within composition trees)

### Phase 4 — Rule Engine
- [ ] Rule struct (trigger + conditions + actions)
- [ ] Trigger evaluation (interact, enter, collide, defeat, timer)
- [ ] Condition evaluation (has_item, flag, attr checks, tag references)
- [ ] Action execution (one C function per action type)
- [ ] Flag storage (global string set, persists with save)

### Phase 5 — Editor Mode
- [ ] Toggle play/editor mode (instant, shared world state)
- [ ] On-screen button hints (context-sensitive, always visible)
- [ ] Free camera with cursor
- [ ] Browse mode: select entities, inspect attributes
- [ ] Edit mode: move entities, resize collision boxes with visual handles
- [ ] Place mode: blueprint palette, place instances
- [ ] Tile mode: paint ground and overlay layers
- [ ] Radial picker widget
- [ ] Scroll picker widget
- [ ] Word builder (seeded vocabulary + gamedata vocabulary)
- [ ] Fuzzy finder for existing names
- [ ] Value adjuster with visual feedback
- [ ] Gamepad keyboard (last resort)
- [ ] Attribute editor (built-in + custom, with diff view)
- [ ] Rule editor (trigger/condition/action builder with pickers)
- [ ] Child entity editor (composition, tags)
- [ ] Undo (snapshot-based, arena memcpy)
- [ ] Save to gamedata.toml (TOML emitter, atomic write)
- [ ] Attribute watcher (pin to debug overlay, live values during play)

### Phase 6 — Distribution
- [ ] Embed `gamedata.toml` in binary for release builds (desktop)
- [ ] Bundle `gamedata.toml` in APK assets for release (Android)
- [ ] Dev mode flag to load from filesystem instead of embedded

### Phase 7 — Gameplay Systems
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
- [ ] Save/load system

## Resolved Decisions

- **Data format:** TOML via tomlc99 (vendored in `engine/vendor/tomlc99/`). Single file `data/gamedata.toml`.
- **Sync mechanism:** Hard link from `data/gamedata.toml` into Syncthing share. Same inode — git and Syncthing both see real changes.
- **Safe save:** Write to temp file, rename onto the original. Atomic on same filesystem, preserves hard link inode (rename replaces directory entry, not inode). Prevents corrupt partial writes from Syncthing races.
- **Android data path:** `/storage/emulated/0/Sync/sleipner/gamedata.toml` (hardcoded). Desktop: `data/gamedata.toml` (repo-relative).
- **Release distribution:** Embed `gamedata.toml` via `#embed` (desktop) / APK assets (Android). Dev builds load from filesystem path instead.
- **Engine grows organically.** Don't build engine features speculatively — add them when the game needs them.
- **Undo system:** Snapshot-based. Before each editor operation, snapshot the entire in-memory gamedata and push onto a history stack. Undo = pop and restore. Simple, every operation is automatically undoable, no need to define inverse operations. Gamedata is small enough that even 100+ snapshots are negligible memory. If gamedata ever grows to megabytes, migrate to command pattern — undo is internal to the editor so refactoring is cheap.
- **Memory allocation:** Arena allocator for all gamedata. All data loaded from TOML lives in one arena — reload or undo = reset the arena. Behavior params per blueprint are variable-length (pointer + count into the arena), no fixed cap. Undo snapshots are just `memcpy` of the arena.
- **Hot-reload:** Poll mtime on `gamedata.toml` (~once per second) in play mode — auto-reload when the file changes (Syncthing edits from phone appear live). In editor mode, no auto-reload — reload is explicit only, to avoid blowing away unsaved in-memory changes.
- **Tile map:** 16x16 pixel tiles, 2 layers (ground + overlay). Ground is terrain (grass, dirt, water, paths). Overlay renders on top of ground but under entities (flowers, puddles, shadows). Stored as arrays of integer tile IDs in TOML, row by row. Autotiling (automatic edge/corner sprite selection) is an editor feature — the file stores concrete tile IDs, the editor computes them on placement.
- **TOML emitter:** Clean regeneration, no comment/formatting preservation. The in-game editor is the primary editing interface — comments aren't useful. Keeps the emitter dead simple.

## Open Questions

### Save System
- What persists? Player position, inventory, flags, level state?
- Separate save file from `gamedata.toml` (level design data vs player progress)?
- Save format — TOML as well, or something simpler?
- Multiple save slots?
- Auto-save on level transition, or manual only?

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
