#pragma once

#include "alloc.h"
#include "blur.h"
#include "input.h"
#include "input_func.h"
#include "raylib.h"
#include "rule.h"
#include "strv.h"
#include "vec.h"

#include <stdbool.h>

/* Pause-overlay Inventory screen (S6.12b, D25/D34).
 *
 * Reached from the pause menu's "Inventory" entry. Mirrors the Settings
 * overlay's lifecycle -- modal, world frozen while open, sharing the
 * menu/settings blurred backdrop (main.c's capture_overlay_blur_if_needed
 * folds this screen's open/blur_captured flags into the same check) --
 * and renders a gamepad-navigable grid of the player's items
 * (ProgressionState.items, an ItemSet keyed by item name -> count, S6.8a)
 * as name/count cells.
 *
 * The item list is snapshotted into `entries` at inventory_screen_open
 * time by walking the ItemSet's raw map buckets (map_strv_int has no
 * iterator, see inventory_screen_collect_items) rather than read live
 * every frame -- the grid's cursor/selection needs a stable, indexable
 * list, and a map has neither stable indices nor iteration order. Item
 * name Strvs point into progression_arena, which nothing rewinds while
 * the modal screen is open (only game_reset_progression, reachable only
 * from the pause menu's RESTORE action, mutually exclusive with this
 * screen being open), so the snapshot's Strv views stay valid for the
 * screen's lifetime. The snapshot vec itself is allocated against
 * progression_alloc (not gamedata_arena) for the same reason: unlike
 * gamedata_arena, nothing rewinds progression_arena on a hot-reload or
 * level transition, both of which can happen while this screen is open. */

typedef struct {
    Strv name;
    int count;
} InventoryItemEntry;

VEC_DECL(inventory_item_entry, InventoryItemEntry)

/* Grid shape (v1): a fixed 4-column layout. Cell dimensions and margin
 * size the vignette panel and each cell's fill/outline in
 * inventory_screen_render. */
#define INVENTORY_GRID_COLUMNS 4
#define INVENTORY_CELL_WIDTH 168
#define INVENTORY_CELL_HEIGHT 96
#define INVENTORY_CELL_MARGIN 16
#define INVENTORY_PANEL_PADDING 32
#define INVENTORY_FONT_SIZE 20
#define INVENTORY_LETTER_SPACING 1.5F

typedef struct {
    int x;
    int y;
} InventoryCellPosition;

/* Visual sizing for inventory_screen_cell_position/_render. Bundled into
 * a struct (rather than four adjacent int parameters) to sidestep
 * clang-tidy's bugprone-easily-swappable-parameters. */
typedef struct {
    int columns;
    int cell_width;
    int cell_height;
    int margin;
} InventoryGridLayout;

/* Grid extent for inventory_screen_grid_nav: how many columns wide the
 * grid is, and how many items are in it. Bundled for the same
 * swappable-parameters reason as InventoryGridLayout above. */
typedef struct {
    int columns;
    int item_count;
} InventoryGridShape;

typedef enum {
    INVENTORY_NAV_LEFT,
    INVENTORY_NAV_RIGHT,
    INVENTORY_NAV_UP,
    INVENTORY_NAV_DOWN,
} InventoryNavDirection;

typedef struct {
    bool open;
    int cursor;
    vec_inventory_item_entry entries;
    /* Lazy blur capture, mirrors MenuState.blur_captured /
     * SettingsState.blur_captured: set false on close, set true by the
     * renderer on the first open frame. */
    bool blur_captured;
} InventoryScreen;

void inventory_screen_init(InventoryScreen *screen);

/* Walk `items`'s raw map buckets (map_strv_int has no iterator) and
 * collect every MAP_ENTRY_OCCUPIED entry into a fresh vec allocated
 * against `alloc`. Order follows the map's internal bucket layout, not
 * insertion order. Pure -- no raylib, headlessly testable. */
vec_inventory_item_entry inventory_screen_collect_items(const ItemSet *items, Allocator alloc);

/* Snapshot `items` into screen->entries via inventory_screen_collect_items
 * and reset the cursor to 0. `alloc` should be the process-lifetime
 * progression allocator (allocator_arena(&state->progression_arena)) --
 * see the module doc comment above for why. */
void inventory_screen_open(InventoryScreen *screen, const ItemSet *items, Allocator alloc);

/* Mark the screen closed. Does not free `entries` -- it lives in
 * progression_alloc and is simply orphaned until the next open
 * overwrites the field with a fresh snapshot. */
void inventory_screen_close(InventoryScreen *screen);

[[nodiscard]] bool inventory_screen_is_open(const InventoryScreen *screen);

/* Move `cursor` by one grid step in `direction`, clamped to
 * [0, shape.item_count). Row-major layout: LEFT/RIGHT step the linear
 * index by -1/+1 (so RIGHT from the last column of a row lands on the
 * first column of the next row, and vice versa for LEFT); UP/DOWN step
 * by -shape.columns/+shape.columns. Any step landing outside
 * [0, item_count) clamps to the nearest valid index (no wrap). Returns 0
 * unconditionally when shape.item_count <= 0 -- cursor nav on an empty
 * inventory is a no-op. Pure, no raylib. */
int inventory_screen_grid_nav(int cursor, InventoryGridShape shape, InventoryNavDirection direction);

/* cell_index -> {x, y} relative to the grid's own origin (0, 0),
 * row-major with layout.columns columns. Caller offsets by the grid's
 * screen origin. layout.columns must be > 0. Pure, no raylib. */
InventoryCellPosition inventory_screen_cell_position(int cell_index, InventoryGridLayout layout);

/* Move the cursor on NAV_LEFT/RIGHT/UP/DOWN, close on CANCEL (raises
 * *close_requested). CONFIRM is a no-op in v1 -- D25/D34 specify a plain
 * read-only name/count grid, no per-item action. */
void inventory_screen_handle_input(InventoryScreen *screen,
                                   const InputState *input,
                                   const BindingStore *bindings,
                                   bool *close_requested);

/* Draw the blurred backdrop, a vignette panel, and the item grid (or an
 * "Empty" message when screen->entries.count == 0), highlighting the
 * cursor cell. Caller is responsible for being inside BeginDrawing(). */
void inventory_screen_render(
    const InventoryScreen *screen, Font ui_font, const BlurPipeline *blur, int screen_width, int screen_height);

/* Reset to a zero-initialised state. Safe on an already zero-initialised
 * screen. entries is not freed -- see inventory_screen_close. */
void inventory_screen_cleanup(InventoryScreen *screen);
