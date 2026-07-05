#pragma once

#include "alloc.h"
#include "blur.h"
#include "input.h"
#include "input_func.h"
#include "raylib.h"
#include "str.h"
#include "strv.h"

#include <stdbool.h>

/* Pause-menu Save/Load Game slot picker (S6.15d2, D33).
 *
 * Reached from the pause menu's "Save Game" / "Load Game" entries.
 * Mirrors inventory_screen.h's lifecycle -- modal, world frozen while
 * open, sharing the menu/settings/inventory blurred backdrop -- but the
 * list is a fixed set of on-disk save slots (SAVE_SLOT_COUNT numbered
 * slots, plus the autosave file as an extra LOAD-only entry) rather than
 * a data-driven snapshot: there is nothing to snapshot, `slot_exists` /
 * `autosave_exists` are just a FileExists probe taken at open time so the
 * renderer can mark which rows are filled.
 *
 * The screen itself only tracks UI state (mode, cursor, which slots
 * exist) and never touches save_write/save_load directly -- confirming a
 * slot is reported back to the caller via save_screen_handle_input's
 * confirmed_slot out-param, and frame.c's run_save_screen_frame is the
 * one that resolves the actual saves directory (platform_saves_dir),
 * composes the slot's path, and calls S6.15d1's save_write/save_load.
 * That split keeps this module free of GameState/Diag/UndoHistory
 * plumbing, the same reason inventory_screen.c never calls into
 * progression.h directly either. */

/* Number of numbered save slots (save_1.toml .. save_N.toml) shown
 * alongside the autosave entry. A UI layout constant, not a growable
 * collection -- see CLAUDE.md's vec-vs-MAX_* rule -- so a plain
 * fixed-size array sized by it (SaveScreen.slot_exists below) is the
 * justified exception, same as BindingStore's enum-indexed arrays. */
#define SAVE_SLOT_COUNT 3

/* Filename-composition buffer for "save_<n>.toml" -- comfortably fits
 * even a pathological multi-digit slot index; not a growable collection. */
#define SAVE_SCREEN_FILENAME_BUF_SIZE 32

/* Autosave's fixed filename under platform_saves_dir -- matches save.c's
 * own private AUTOSAVE_FILENAME. Duplicated rather than shared via a
 * save.h include: save.h pulls in game.h's full GameState dependency
 * chain (audio/blueprint/gamedata/preferences/progression...), which
 * would break this module's -- and its unit test's -- deliberate
 * independence from GameState (see the module doc comment above). One
 * string literal duplicated between two files is a small price for
 * keeping that boundary. */
#define SAVE_SCREEN_AUTOSAVE_FILENAME "autosave.toml"

#define SAVE_SCREEN_FONT_SIZE 48
#define SAVE_SCREEN_HEADER_FONT_SIZE 56
#define SAVE_SCREEN_LINE_PADDING 16
#define SAVE_SCREEN_BLOCK_VPAD 32
#define SAVE_SCREEN_BLOCK_MIN_WIDTH 480
#define SAVE_SCREEN_HEADER_GAP 24
#define SAVE_SCREEN_SELECTED_FONT_BUMP 6
#define SAVE_SCREEN_LETTER_SPACING 2.0F

typedef enum {
    SAVE_SCREEN_MODE_SAVE,
    SAVE_SCREEN_MODE_LOAD,
} SaveScreenMode;

typedef enum {
    SAVE_SCREEN_NAV_UP,
    SAVE_SCREEN_NAV_DOWN,
} SaveScreenNavDirection;

/* Bundles cursor + entry_count for save_screen_nav below -- a bare int
 * cursor beside a bare int entry_count would trip clang-tidy's
 * bugprone-easily-swappable-parameters, the same reason
 * inventory_screen.h's InventoryGridShape bundles columns + item_count. */
typedef struct {
    int cursor;
    int entry_count;
} SaveScreenCursor;

typedef struct {
    bool open;
    SaveScreenMode mode;
    int cursor;
    /* Populated by save_screen_open via FileExists; read by both the
     * renderer (fill/empty marking) and frame.c's confirm handler
     * (gates LOAD on an empty slot without a second filesystem probe). */
    bool slot_exists[SAVE_SLOT_COUNT];
    /* The autosave file's existence, shown as the extra entry at index
     * SAVE_SLOT_COUNT in LOAD mode only (see save_screen_entry_count).
     * Scanned in both modes for simplicity -- one extra FileExists call
     * per open is negligible and avoids a mode-conditional scan path. */
    bool autosave_exists;
    /* Lazy blur capture, mirrors InventoryScreen.blur_captured /
     * MenuState.blur_captured / SettingsState.blur_captured. */
    bool blur_captured;
} SaveScreen;

void save_screen_init(SaveScreen *screen);

/* Number of navigable rows for `mode`: SAVE_SLOT_COUNT in SAVE mode (you
 * can only save into a numbered slot, never overwrite the autosave from
 * the UI), SAVE_SLOT_COUNT + 1 in LOAD mode (the numbered slots plus the
 * autosave entry at index SAVE_SLOT_COUNT). Pure, no raylib. */
[[nodiscard]] int save_screen_entry_count(SaveScreenMode mode);

/* True when `entry_index` refers to the autosave row rather than one of
 * the SAVE_SLOT_COUNT numbered slots. Pure. */
[[nodiscard]] bool save_screen_entry_is_autosave(int entry_index);

/* Look up whether `entry_index` (a numbered slot or the autosave row)
 * was found on disk at the last save_screen_open. Pure -- reads the
 * screen's cached scan, no filesystem access. */
[[nodiscard]] bool save_screen_entry_exists(const SaveScreen *screen, int entry_index);

/* Move `state.cursor` by one row in `direction`, clamped to
 * [0, state.entry_count) with no wrap -- same clamp-not-wrap convention
 * as inventory_screen_grid_nav and MenuState's own up/down nav. Returns 0
 * unconditionally when state.entry_count <= 0. Pure, no raylib. */
[[nodiscard]] int save_screen_nav(SaveScreenCursor state, SaveScreenNavDirection direction);

/* Compose "<saves_dir>save_<slot_index + 1>.toml" into a freshly
 * allocated Str -- slot_index is 0-based internally, the on-disk/UI
 * numbering is 1-based ("SLOT 1" -> save_1.toml). Pure string
 * composition, no filesystem access; an empty (ptr == nullptr) result
 * means the underlying Str append failed. */
[[nodiscard]] Str save_screen_slot_path(Strv saves_dir, int slot_index, Allocator alloc);

/* Compose "<saves_dir>" + SAVE_SCREEN_AUTOSAVE_FILENAME into a freshly
 * allocated Str. Pure string composition, no filesystem access. */
[[nodiscard]] Str save_screen_autosave_path(Strv saves_dir, Allocator alloc);

/* Reset cursor to 0, set `mode`, and probe every slot + the autosave
 * file via FileExists (composing each path with `alloc`, a scratch
 * allocator the caller's SCRATCH_SCOPE reclaims -- the screen itself
 * only keeps the boolean result, not the paths). `saves_dir` is the
 * already-resolved platform_saves_dir() result; this function does not
 * resolve it itself, matching save_write/save_load's own "caller
 * supplies the path" shape. */
void save_screen_open(SaveScreen *screen, SaveScreenMode mode, Strv saves_dir, Allocator alloc);

/* Mark the screen closed. */
void save_screen_close(SaveScreen *screen);

[[nodiscard]] bool save_screen_is_open(const SaveScreen *screen);

/* Move the cursor on NAV_UP/DOWN, close on CANCEL (raises
 * *close_requested). On CONFIRM, writes the current cursor row into
 * *confirmed_slot (caller must pre-set it to a sentinel like -1 --
 * this function never clears it, only sets it on an actual confirm
 * press, mirroring inventory_screen_handle_input's close_requested
 * convention). Reports the row unconditionally, even for an empty LOAD
 * slot -- gating on emptiness is the caller's job (frame.c), since the
 * caller is also the one resolving the actual save path and needs
 * `screen->slot_exists`/`autosave_exists` anyway to decide whether to
 * treat the confirm as a no-op. */
void save_screen_handle_input(SaveScreen *screen,
                              const InputState *input,
                              const BindingStore *bindings,
                              bool *close_requested,
                              int *confirmed_slot);

/* Draw the blurred backdrop, a vignette panel, a mode header ("SAVE
 * GAME" / "LOAD GAME"), and one row per entry, highlighting the cursor
 * row and dimming empty (non-selected) rows. Caller is responsible for
 * being inside BeginDrawing(). */
void save_screen_render(
    const SaveScreen *screen, Font ui_font, const BlurPipeline *blur, int screen_width, int screen_height);

/* Reset to a zero-initialised state. Safe on an already zero-initialised
 * screen. */
void save_screen_cleanup(SaveScreen *screen);
