#pragma once

#include "blur.h"
#include "input.h"
#include "input_func.h"
#include "raylib.h"

#include <stdbool.h>

/* Pause-overlay main menu.
 *
 * Opens via the global menu binding (ACTION_MENU_TOGGLE) in either play
 * or editor mode, suspending normal frame logic until closed. Eleven
 * entries: Resume, Save, Restore, Save Game, Load Game, Host Game, Join
 * Game, Inventory, Settings, Toggle Debug Overlay, Quit. Save/Restore are
 * the editor's raw gamedata ops (data/gamedata.toml); Save Game/Load Game
 * are the player-facing save-slot picker (S6.15d2, D33); Host Game/Join
 * Game (S8.3b) start/discover a LAN session -- all four sit together
 * right after Save/Restore since the whole group is "session-management"
 * operations (persist to disk, or persist to the LAN), distinct from the
 * play-facing Inventory/Settings entries below them. Background is a
 * frozen Gaussian blur of the frame the menu opened over.
 *
 * The menu's only inputs are nav (up/down), confirm, and cancel. It owns
 * no game state — Save / Restore / Toggle / Quit return as a MenuAction
 * enum the caller dispatches against; Inventory / Settings / Save Game /
 * Load Game / Host Game / Join Game hand off to their own overlay
 * screens or network.h lifecycle calls (see MENU_ACTION_OPEN_INVENTORY /
 * MENU_ACTION_OPEN_SETTINGS / MENU_ACTION_OPEN_SAVE_MENU /
 * MENU_ACTION_OPEN_LOAD_MENU / MENU_ACTION_HOST_GAME /
 * MENU_ACTION_JOIN_GAME's call sites in frame.c). */

#define MENU_FONT_SIZE 64

typedef enum {
    MENU_ENTRY_RESUME,
    MENU_ENTRY_SAVE,
    MENU_ENTRY_RESTORE,
    MENU_ENTRY_SAVE_GAME,
    MENU_ENTRY_LOAD_GAME,
    MENU_ENTRY_HOST_GAME,
    MENU_ENTRY_JOIN_GAME,
    MENU_ENTRY_INVENTORY,
    MENU_ENTRY_SETTINGS,
    MENU_ENTRY_TOGGLE_DEBUG_OVERLAY,
    MENU_ENTRY_QUIT,
    MENU_ENTRY_COUNT,
} MenuEntry;

typedef enum {
    MENU_ACTION_NONE,
    MENU_ACTION_RESUME,
    MENU_ACTION_SAVE,
    MENU_ACTION_RESTORE,
    MENU_ACTION_OPEN_SAVE_MENU,
    MENU_ACTION_OPEN_LOAD_MENU,
    MENU_ACTION_HOST_GAME,
    MENU_ACTION_JOIN_GAME,
    MENU_ACTION_OPEN_INVENTORY,
    MENU_ACTION_OPEN_SETTINGS,
    MENU_ACTION_TOGGLE_DEBUG_OVERLAY,
    MENU_ACTION_QUIT,
} MenuAction;

typedef struct {
    bool open;
    int selected;
    Font font;
    bool font_loaded;
    /* Lazy blur capture flag. Set false on close, set true by the
     * renderer the first frame the menu draws. Lets state transitions
     * (menu_open / menu_close) stay free of render side effects so
     * headless tests can drive the menu without a GL context. */
    bool blur_captured;
} MenuState;

/* Zero-init the menu. The display font is provided separately via
 * menu_set_font — keeps the menu module free of the embedded-asset
 * symbol references that would otherwise leak into the engine static
 * library and break the unit-test target's link. */
void menu_init(MenuState *menu);

/* Hand over a font for the entry list, e.g. from font_cache_get. The
 * menu holds a non-owning copy: it never calls UnloadFont on it, and
 * menu_cleanup does not unload it either. The shared font cache is the
 * sole owner. Re-entrant: passing a new font just replaces the field. */
void menu_set_font(MenuState *menu, Font font);

/* Mark the menu as open and reset the selected entry to Resume. */
void menu_open(MenuState *menu);

/* Mark the menu as closed. Selected entry is preserved so re-opening
 * later does not feel jarring; reset_selection is a follow-up if the
 * UX wants explicit reset-on-close. */
void menu_close(MenuState *menu);

/* Read one frame of menu input via the function layer and return the
 * dispatch action. MENU_ACTION_NONE means no input fired this frame.
 * MENU_ACTION_RESUME closes the menu (the caller flips menu->open). */
MenuAction menu_handle_input(MenuState *menu, const InputState *input, const BindingStore *bindings);

/* Draw the blurred backdrop and the entry list. Caller is responsible
 * for being inside BeginDrawing(). The blur pipeline is captured at
 * menu-open time and frozen; menu_render only reads it. */
void menu_render(const MenuState *menu, const BlurPipeline *blur, int screen_width, int screen_height);

/* Reset menu state. Safe on a zero-initialised menu. Does not unload
 * the font — it is owned by the shared font cache (font_cache_cleanup). */
void menu_cleanup(MenuState *menu);
