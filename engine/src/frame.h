#pragma once

#include "diag.h"
#include "editor/editor.h"
#include "game.h"
#include "input.h"
#include "menu.h"
#include "raylib.h"
#include "settings.h"
#include "undo.h"

#include <stdbool.h>

/* Frame-level dispatchers.
 *
 * Lifted out of main.c so headless tests can drive the same
 * orchestration the production loop runs. The render path, audio,
 * gamepad polling, hot-reload, gamedata persistence, and transition
 * handling stay in main.c — those are production-only concerns.
 *
 * The split lets engine_lib test code call run_active_frame /
 * dispatch_menu_action against a real GameState without pulling in
 * raylib's window/audio/file I/O. */

void handle_global_toggles(GameState *state, const InputState *input, bool *font_preview_enabled);

/* Flip menu->open. Render-side blur capture happens lazily the next
 * frame the menu is open (see menu.h blur_captured). */
void toggle_menu_open(MenuState *menu);

/* Optional save / restore handlers passed via MenuDispatchCtx. nullptr
 * is permitted: SAVE / RESTORE menu actions become silent no-ops
 * (the menu still closes), which is the right shape for headless
 * tests that don't have a writable gamedata path. */
typedef void (*MenuSaveFn)(Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history);
typedef void (*MenuRestoreFn)(
    Diag *diag, GameState *state, EditorState *editor_state, WatchList *watches, UndoHistory *undo_history);

/* Persist BindingStore mutations to disk after a successful rebind. nullptr
 * is permitted: settings still mutates the in-memory store, just no disk write
 * (the right shape for headless tests). */
typedef bool (*KeybindingsSaveFn)(GameState *state);

/* Pluggable level loader for handle_transition. Production wires this
 * to a wrapper around its file-I/O load_gamedata; tests wire it to a
 * wrapper around game_load_gamedata against an in-memory TOML
 * string. Returning false logs an error but does not abort the
 * frame; transitions to invalid level names degrade to "stay on the
 * current level" rather than crash. */
typedef bool (*LevelLoaderFn)(Diag *diag, GameState *state, const char *level_name, void *user_data);

typedef struct {
    Diag *diag;
    GameState *state;
    EditorState *editor_state;
    WatchList *watches;
    UndoHistory *undo_history;
    MenuState *menu;
    SettingsState *settings;
    bool *quit_requested;
    MenuSaveFn save_fn;
    MenuRestoreFn restore_fn;
} MenuDispatchCtx;

/* Apply one MenuAction. RESUME / TOGGLE_DEBUG_OVERLAY / QUIT are
 * pure state transitions; SAVE / RESTORE go through ctx.save_fn /
 * ctx.restore_fn (each may be nullptr); OPEN_SETTINGS hands control
 * to the Settings overlay. */
void dispatch_menu_action(MenuDispatchCtx ctx, MenuAction action);

/* Editor-or-play step + simulation step. Does NOT call
 * handle_transition — production main.c does that explicitly after
 * frame_update so headless tests can observe transition.pending
 * without the engine library reaching for file I/O. */
void run_active_frame(Diag *diag,
                      GameState *state,
                      Camera2D *editor_camera,
                      EditorState *editor_state,
                      WatchList *watches,
                      UndoHistory *undo_history,
                      InputState input,
                      float delta_time);

/* Editor-mode input dispatch. Selects the right sub-mode handler
 * (drag, handles, place, attr_edit, radial, word_builder,
 * fuzzy_finder, gamepad_kb, blueprint_browse, or browse). */
void handle_editor_input(Diag *diag,
                         GameState *state,
                         Camera2D *camera,
                         EditorState *editor_state,
                         WatchList *watches,
                         UndoHistory *undo_history,
                         InputState input,
                         float delta_time);

/* Persistent state pointers for one full-frame dispatch.
 * Production main.c builds this once and passes it to frame_update
 * every loop iteration; headless tests build a TestGame fixture
 * around the same fields. */
typedef struct {
    EditorState *editor_state;
    Camera2D *editor_camera;
    WatchList *watches;
    UndoHistory *undo_history;
    MenuState *menu;
    SettingsState *settings;
    bool *font_preview_enabled;
    bool *quit_requested;
    MenuSaveFn save_fn;
    MenuRestoreFn restore_fn;
    KeybindingsSaveFn keybindings_save_fn;
    LevelLoaderFn level_loader_fn;
    void *level_loader_user_data;
} FrameContext;

/* Run one full frame of input dispatch: global toggles, menu open /
 * close press, ACTION_QUIT, then either menu navigation (if menu is
 * open) or the active editor / play frame. Does NOT call
 * handle_transition or render — production main.c does those
 * separately. The same function drives both production and
 * integration tests. */
void frame_update(Diag *diag, GameState *state, FrameContext *ctx, InputState input, float delta_time);

/* Apply a pending level transition. No-op when state->transition.pending
 * is false; otherwise clears the flag, calls ctx->level_loader_fn to
 * reload gamedata under the requested level name, places the player
 * at the saved spawn point, snaps the camera, and pre-seeds player
 * overlap tracking so newly-overlapped entities don't spuriously
 * fire enter triggers. Pushes a "Level loaded" undo entry on success. */
void handle_transition(Diag *diag, GameState *state, FrameContext *ctx);
