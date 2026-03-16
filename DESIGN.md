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

### Game Data Format

Plain text, section-based, one statement per line. `#` comments. `[section]` headers separate blueprints and levels. Each entity is a single line — clean one-line diffs.

```
# === Blueprints ===

[blueprint: tree]
sprite: texture=tree.png src=0,0,64,80
collision: offset=20,60 size=24,16
behavior: static

[blueprint: chest]
sprite: texture=chest.png src=0,0,16,16
collision: offset=0,0 size=16,16
behavior: chest

[blueprint: house]
sprite: texture=house.png src=0,0,96,128
collision: offset=0,64 size=96,64
behavior: static

[blueprint: fence_v]
sprite: texture=fence.png src=0,0,16,48
collision: offset=0,0 size=16,48
behavior: static

[blueprint: player]
sprite: texture=player.png src=0,0,32,32
animation: frames=6 size=32 speed=10 row=0
collision: offset=11,22 size=10,10
behavior: player speed=80

# === Levels ===

[level: overworld]
size: 640,360
music: bgm.mp3

entity: blueprint=house pos=40,20
entity: blueprint=tree pos=200,60
entity: blueprint=tree pos=350,150
entity: blueprint=tree pos=80,200
entity: blueprint=tree pos=450,40
entity: blueprint=tree pos=500,220
entity: blueprint=chest pos=300,100
entity: blueprint=chest pos=160,280
entity: blueprint=fence_v pos=260,50
entity: blueprint=fence_v pos=260,98
entity: blueprint=player pos=320,180
```

### Runtime Loading

- On startup, the game reads `gamedata.txt` and parses all `[blueprint:]` and `[level:]` sections.
- Blueprints are registered in a lookup table (name → component defaults).
- The active level's entities are instantiated from their blueprints with per-instance overrides.
- The editor modifies in-memory state and writes the entire file back on save.
- Parser is hand-rolled C — the format is trivial to parse (read lines, split on spaces, split on `=`).

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
- [ ] Create `data/gamedata.txt` with current level as data
- [ ] Text parser for `[blueprint:]` and `[level:]` sections
- [ ] Load blueprints and level from `gamedata.txt` at startup
- [ ] Remove hardcoded obstacles from main.c
- [ ] Hard-link `data/gamedata.txt` into Syncthing share
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
- [ ] Embed `gamedata.txt` in binary for release builds (desktop)
- [ ] Bundle `gamedata.txt` in APK assets for release (Android)
- [ ] Dev mode flag to load from filesystem instead of embedded

### Phase 6 — Gameplay
- [ ] NPC behaviors (patrol, dialogue)
- [ ] Player interaction (chests, doors)
- [ ] Multiple levels with transitions
- [ ] Combat system
- [ ] Inventory

## Open Questions

- Tile map format: fixed grid size? Multiple layers? Autotiling?
- Undo system: command pattern with history stack? Snapshot-based?
- How many behavior params are enough? Fixed array vs dynamic allocation?
- Hot-reload: detect file changes and reload blueprints/levels live?
- Android Syncthing path: hardcode or make configurable?
- Conflict resolution: what if Syncthing syncs while the editor is saving? (Syncthing does in-place writes so hard link survives, but partial writes could corrupt. Write to temp + rename?)
- Release builds: embed gamedata.txt, or ship alongside binary?
- Hard link maintenance: document the `ln` setup step, detect broken links?
