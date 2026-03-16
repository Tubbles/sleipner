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

## Level Data

A level is:

- A tile map (grid of tile IDs for terrain).
- A list of entity instances (blueprint + overrides + position).
- Metadata (level name, dimensions, music track, ambient color).

### Serialization

Levels serialize to a binary or simple text format on disk. The editor saves/loads levels. For distribution, level data is embedded in the binary via `#embed` (desktop) or bundled in APK assets (Android).

### Format (TBD)

Start simple — even a hand-rolled binary format is fine. Can evolve to something more structured later. Key requirement: the format must be stable enough that saved levels survive code changes.

## Roadmap

### Phase 1 — Foundation (Current)
- [x] Tiled grass background
- [x] Player avatar with animation and gamepad control
- [x] Static obstacles with AABB collision
- [x] Depth-sorted rendering
- [x] Background music
- [x] Debug overlay (F3)
- [x] Android APK build

### Phase 2 — Entity System
- [ ] Component structs (Transform, Sprite, Collision)
- [ ] Entity storage (flat array or slot map)
- [ ] Convert existing Player and Obstacle to entity instances
- [ ] Blueprint definitions (hardcoded initially, data-driven later)

### Phase 3 — Editor Mode
- [ ] Toggle play/editor mode
- [ ] Free camera with cursor
- [ ] Select entities, show properties
- [ ] Move entities with gamepad
- [ ] Resize collision boxes visually
- [ ] Place new entities from blueprint palette

### Phase 4 — Persistence
- [ ] Serialize level to file
- [ ] Load level from file
- [ ] Embed level data for distribution

### Phase 5 — Gameplay
- [ ] NPC behaviors (patrol, dialogue)
- [ ] Player interaction (chests, doors)
- [ ] Multiple levels with transitions
- [ ] Combat system
- [ ] Inventory

## Open Questions

- Tile map format: fixed grid size? Multiple layers? Autotiling?
- Undo system: command pattern with history stack? Snapshot-based?
- How many entity params are enough? Fixed array vs dynamic allocation?
- Should blueprints live in a separate file or inline in the level?
- Hot-reload behaviors during development?
