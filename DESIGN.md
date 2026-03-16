# Game Design & Architecture

## Vision

Sleipner is a top-down Zelda-like action RPG (Link to the Past / Link's Awakening style). Controller-first, pixel art, runs on Linux and Android.

The game is designed from within itself — an integrated editor mode allows building levels, composing entities, tuning collision, and testing gameplay without leaving the running game.

## In-Game Editor

The editor is a mode within the game, not a separate tool. Toggle between play mode and editor mode at any time. Everything is gamepad-driven.

### Core Principles

- **Edit what you see.** The editor operates on the live game world. Changes are immediately visible.
- **Gamepad-first.** Every editor action is reachable from a controller. No mouse/keyboard required (though keyboard shortcuts are fine as secondary input).
- **Data, not code.** Levels, entity blueprints, collision shapes, and behavior parameters are data that the editor reads and writes. Game logic consumes this data at runtime.
- **Round-trip.** Play, pause, edit, resume. The editor and the game share the same world state.

### Editor Modes

1. **Browse mode** — Free camera, select entities, inspect properties.
2. **Place mode** — Pick from a blueprint palette, place instances in the world.
3. **Edit mode** — Modify the selected entity: move, resize collision box, change sprite, tweak parameters.
4. **Tile mode** — Paint terrain tiles on the grid.

### Editor UI

- Cursor controlled by left stick, snaps to grid (toggle-able).
- Radial menus or scrollable palettes for selecting blueprints / tools.
- Property panel overlay for the selected entity (cycle fields with D-pad, adjust values with sticks/bumpers).
- Visual handles on collision boxes (drag corners/edges to resize).
- Color-coded overlays: collision boxes, spawn points, trigger zones.

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

## Entity System

Composition over inheritance. An entity is an ID plus a bag of components.

### Entity Blueprint

A blueprint is a named template that defines which components an entity has and their default values. Blueprints are data (not code). The editor creates and modifies blueprints. Instances in the world reference a blueprint but can override individual values.

### Components (Initial Set)

| Component | Fields | Purpose |
|---|---|---|
| Transform | position, rotation | Where it is |
| Sprite | texture, source_rect, z_order | How it looks |
| Animation | frame_count, frame_size, speed, row | Animated sprites |
| Collision | rect (relative to transform) | Physical presence |
| Behavior | behavior_id, params[] | What it does (references coded behavior) |

### Behaviors

Behaviors are coded in C and referenced by ID. They implement the "what happens" part that can't be pure data. Examples:

- `BEHAVIOR_STATIC` — Does nothing (trees, rocks, decorations).
- `BEHAVIOR_PLAYER` — Reads input, moves, animates.
- `BEHAVIOR_NPC_WALK` — Walks a patrol path, stops, turns.
- `BEHAVIOR_CHEST` — Opens on interaction, grants item.
- `BEHAVIOR_DOOR` — Transitions to another level/area.
- `BEHAVIOR_TRIGGER` — Fires an event when the player enters a zone.

The behavior params array holds floats that the behavior interprets (e.g. patrol speed, interaction radius, target level ID). The editor exposes these as named fields based on the behavior_id.

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

### Phase 1 — Foundation (Current)
- [x] Tiled grass background
- [x] Player avatar with animation and gamepad control
- [x] Static obstacles with AABB collision
- [x] Depth-sorted rendering
- [x] Background music
- [x] Debug overlay (F3)
- [x] Android APK build

### Phase 2 — Data Pipeline
- [ ] Create `data/gamedata.toml` with current level as TOML data
- [ ] Load blueprints and level from `gamedata.toml` at startup (tomlc99)
- [ ] Remove hardcoded obstacles from main.c
- [ ] TOML emitter for editor save (tomlc99 is read-only)
- [ ] Hard-link `data/gamedata.toml` into Syncthing share
- [ ] Game reads gamedata path (repo-relative on desktop, Syncthing folder on Android)

### Phase 3 — Entity System
- [ ] Component structs (Transform, Sprite, Collision)
- [ ] Entity storage (flat array or slot map)
- [ ] Convert existing Player and Obstacle to entity instances
- [ ] Instantiate entities from blueprint data

### Phase 4 — Editor Mode
- [ ] Toggle play/editor mode
- [ ] Free camera with cursor
- [ ] Select entities, show properties
- [ ] Move entities with gamepad
- [ ] Resize collision boxes visually
- [ ] Place new entities from blueprint palette
- [ ] Save level back to text file

### Phase 5 — Distribution
- [ ] Embed `gamedata.toml` in binary for release builds (desktop)
- [ ] Bundle `gamedata.toml` in APK assets for release (Android)
- [ ] Dev mode flag to load from filesystem instead of embedded

### Phase 6 — Gameplay
- [ ] NPC behaviors (patrol, dialogue)
- [ ] Player interaction (chests, doors)
- [ ] Multiple levels with transitions
- [ ] Combat system
- [ ] Inventory

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

(None currently — all resolved.)
