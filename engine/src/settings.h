#pragma once

#include "alloc.h"
#include "blur.h"
#include "input.h"
#include "input_func.h"
#include "raylib.h"

#include <stdbool.h>

/* Pause-overlay Settings screen.
 *
 * Reached from the pause menu's "Settings" entry. While open, the
 * frame dispatcher routes input here instead of the editor or game,
 * mirroring how the menu suspends normal logic.
 *
 * Three screens:
 *   LIST    — scrollable list of every action, axis, and a final
 *             "Reset all to defaults" entry. CONFIRM enters DETAIL,
 *             CANCEL closes back to the menu.
 *   DETAIL  — shows each alternative for the selected target plus
 *             "Add new alternative" and "Reset to defaults" rows.
 *             CONFIRM enters CAPTURE, EDITOR_DELETE removes an
 *             alternative, CANCEL returns to LIST.
 *   CAPTURE — high-water-mark chord capture for an action target,
 *             or two-step kb_axis / single-axis capture for an axis
 *             target. KEY_ESCAPE / GAMEPAD_BUTTON_MIDDLE_RIGHT are
 *             reserved cancels, read raw and never bindable from the
 *             UI. */

#define SETTINGS_FONT_SIZE 32

typedef enum {
    SETTINGS_SCREEN_LIST,
    SETTINGS_SCREEN_DETAIL,
    SETTINGS_SCREEN_CAPTURE,
} SettingsScreen;

typedef enum {
    SETTINGS_TARGET_ACTION,
    SETTINGS_TARGET_AXIS,
    SETTINGS_TARGET_RESET_ALL,
} SettingsTargetKind;

typedef enum {
    SETTINGS_CAPTURE_ACTION,
    SETTINGS_CAPTURE_AXIS_FIRST,
    SETTINGS_CAPTURE_AXIS_SECOND,
} SettingsCaptureMode;

#define SETTINGS_MAX_CHORD_ATOMS 8

typedef struct {
    bool open;
    Font font;
    bool font_loaded;
    /* Lazy blur capture: like MenuState.blur_captured, set false on
     * close and set true by the renderer on the first open frame. */
    bool blur_captured;

    SettingsScreen screen;
    int list_index;
    int list_scroll;

    SettingsTargetKind target_kind; /* what kind of row was selected on LIST */
    int target_index;               /* InputAction or InputAxis enum value */

    int detail_index;  /* selected row inside DETAIL: alternative or "Add" / "Reset" */
    int detail_scroll; /* DETAIL list scroll position */

    /* Capture state */
    SettingsCaptureMode capture_mode;
    int capture_alt_index;                               /* >= 0 = replace this alternative; < 0 = add new */
    AtomicInput capture_chord[SETTINGS_MAX_CHORD_ATOMS]; /* high-water mark peak set */
    int capture_chord_count;
    /* False until one frame of no-input has been observed since capture
     * began. Prevents the press that opened capture (e.g. ACTION_CONFIRM
     * = ENTER) from immediately appearing in the chord. */
    bool capture_armed;
    int capture_kb_axis_neg_key; /* used by AXIS_SECOND; carries first key forward */

    /* Out-of-band signal used by frame.c to invoke the host's save
     * callback after a successful mutation. settings_handle_input sets
     * this to true; the dispatcher consumes it and clears it. */
    bool save_requested;
    /* Toast text shown below the title for a couple of frames after a
     * save / reset. Pointer to a string literal; no ownership. */
    const char *toast_text;
    float toast_timer;
} SettingsState;

void settings_init(SettingsState *settings);

void settings_set_font(SettingsState *settings, Font font);

void settings_open(SettingsState *settings);

void settings_close(SettingsState *settings);

[[nodiscard]] bool settings_is_open(const SettingsState *settings);

/* Run one frame of input. Mutates the BindingStore via the input_func
 * mutation API. Sets settings->save_requested when the caller should
 * persist the store to disk. Toggles `*close_requested` if the user
 * pressed cancel from the LIST screen. */
void settings_handle_input(
    SettingsState *settings, const InputState *input, BindingStore *store, Allocator alloc, bool *close_requested);

/* Decay the toast timer. Caller drives this with delta_time. */
void settings_tick(SettingsState *settings, float delta_time);

/* Render the blurred backdrop and the active screen. Caller is
 * responsible for being inside BeginDrawing(). */
void settings_render(const SettingsState *settings,
                     const BindingStore *store,
                     const BlurPipeline *blur,
                     int screen_width,
                     int screen_height);

void settings_cleanup(SettingsState *settings);
