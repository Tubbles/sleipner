#pragma once

#include "alloc.h"
#include "blur.h"
#include "input.h"
#include "input_func.h"
#include "keyboard_widget.h"
#include "preferences.h"
#include "raylib.h"

#include <stdbool.h>

/* Pause-overlay Settings screen.
 *
 * Reached from the pause menu's "Settings" entry. While open, the
 * frame dispatcher routes input here instead of the editor or game,
 * mirroring how the menu suspends normal logic.
 *
 * The LIST screen renders a tab header at the top: TAB_PREV / TAB_NEXT
 * (gamepad L1/R1, keyboard Tab/Shift+Tab) switch tabs. Each tab has its
 * own body and selection cursor.
 *
 *   Input tab    : the keybinding LIST → DETAIL → CAPTURE flow.
 *   General tab  : non-binding settings (currently just Data directory).
 *
 * Detail and Capture screens are reachable only from the Input tab.
 * Path-edit reachable only from the General tab. */

#define SETTINGS_FONT_SIZE 32

typedef enum {
    SETTINGS_TAB_INPUT,
    SETTINGS_TAB_GENERAL,
    SETTINGS_TAB_COUNT,
} SettingsTab;

typedef enum {
    SETTINGS_SCREEN_LIST,
    SETTINGS_SCREEN_DETAIL,
    SETTINGS_SCREEN_CAPTURE,
    SETTINGS_SCREEN_PATH_EDIT,
} SettingsScreen;

typedef enum {
    PATH_EDIT_BROWSE,
    PATH_EDIT_KEYBOARD,
} PathEditMode;

/* Path-edit screen state. The buf holds the working directory path
 * being edited; len tracks its length (kept in sync with the
 * KeyboardWidget when in KEYBOARD mode). dir_list is owned by raylib
 * and freed via UnloadDirectoryFiles on screen exit and refresh.
 *
 * buf size is a deliberate justified MAX_* exception: paths are
 * bounded by OS PATH_MAX (4096 on Linux, 260 on Windows). 512 is
 * comfortably above typical user paths and below the smallest
 * platform limit. */
#define PATH_EDIT_BUF_SIZE 512

typedef struct {
    PathEditMode mode;
    char buf[PATH_EDIT_BUF_SIZE];
    int len;
    KeyboardWidget kb;
    FilePathList dir_list;
    bool dir_list_loaded;
    bool at_root;
    int browse_index;
    int browse_scroll;
} PathEditState;

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
    SettingsTab tab;
    int list_index;
    int list_scroll;
    /* Selection within the General tab. Kept separate from list_index
     * so tab switches do not lose place. */
    int general_index;
    int general_scroll;

    SettingsTargetKind target_kind; /* what kind of row was selected on LIST */
    int target_index;               /* InputAction or InputAxis enum value */

    int detail_index;  /* selected row inside DETAIL: alternative or "Add" / "Reset" */
    int detail_scroll; /* DETAIL list scroll position */

    /* Path-edit screen: active when screen == SETTINGS_SCREEN_PATH_EDIT. */
    PathEditState path_edit;

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
    /* Sibling flag for preferences.toml writes. Kept separate from
     * save_requested so each dispatcher branch stays independent and
     * either flag can be raised without coupling to the other. */
    bool save_preferences_requested;
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
 * mutation API and Preferences via str_clear+append on data_dir. Sets
 * settings->save_requested or save_preferences_requested when the
 * caller should persist the corresponding store to disk. Toggles
 * `*close_requested` if the user pressed cancel from the LIST screen. */
void settings_handle_input(SettingsState *settings,
                           const InputState *input,
                           BindingStore *store,
                           Preferences *preferences,
                           Allocator alloc,
                           bool *close_requested);

/* Decay the toast timer. Caller drives this with delta_time. */
void settings_tick(SettingsState *settings, float delta_time);

/* Render the blurred backdrop and the active screen. Caller is
 * responsible for being inside BeginDrawing(). The General tab reads
 * `preferences` for the current data_dir value; pass nullptr only if
 * the General tab will not be entered (tests). */
void settings_render(const SettingsState *settings,
                     const BindingStore *store,
                     const Preferences *preferences,
                     const BlurPipeline *blur,
                     int screen_width,
                     int screen_height);

void settings_cleanup(SettingsState *settings);
