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
 *   General tab  : non-binding settings -- Data directory, plus Master/
 *                  Music/SFX volume rows (NAV_LEFT/RIGHT to adjust).
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
    /* Pick a filesystem root: drive letters on Windows, "/" on POSIX.
     * Reached from the synthesized "<SELECT DRIVE>" row in the browse
     * view. The user picks an entry, the screen drops them at that
     * drive's root in BROWSE mode. */
    PATH_EDIT_DRIVE_SELECT,
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

/* Drive listing for the DRIVE_SELECT mode. 26 letters is the upper
 * bound of Windows drive letters (A-Z); on non-Android POSIX the
 * table holds a single "/" entry; on Android it holds the
 * shared-storage roots. Also a justified MAX_* (the OS, not us,
 * defines the count). */
#define PATH_EDIT_DRIVE_MAX 26
/* Must fit the longest string written into a slot, currently
 * Android's "/storage/emulated/0/" (20 chars) and whatever
 * GetApplicationDirectory() returns. */
#define PATH_EDIT_DRIVE_PATH_BUF 64

typedef struct {
    PathEditMode mode;
    char buf[PATH_EDIT_BUF_SIZE];
    int len;
    KeyboardWidget kb;
    FilePathList dir_list;
    bool dir_list_loaded;
    /* Whether buf named a readable directory as of the last refresh.
     * Drives the "(cannot read directory)" annotation in the browse
     * view; does not affect row content or navigation. */
    bool dir_exists;
    bool at_root;
    int browse_index;
    int browse_scroll;
    char drives[PATH_EDIT_DRIVE_MAX][PATH_EDIT_DRIVE_PATH_BUF];
    int drive_count;
    int drive_index;
    int drive_scroll;
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

/* Backing store for formatted toast messages (e.g. the "Also bound to
 * <action>" conflict warning) that can't be a string literal. Comfortably
 * covers "Also bound to " (14 chars) plus the longest action TOML name
 * ("BLUEPRINT_DUPLICATE", 19 chars). */
#define TOAST_MSG_BUF_CAP 64

/* Cap on the previous-frame held-atom snapshot used by release-edge chord
 * capture (see capture_prev_held below). A bit above SETTINGS_MAX_CHORD_ATOMS:
 * atoms can be transiently held during capture (e.g. a modifier the user
 * lets go of before the chord that gets bound) even though the chord itself
 * is capped. */
#define SETTINGS_CAPTURE_PREV_HELD_MAX 16

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
    /* Release-edge tracking for ACTION-mode capture: the bindable atoms
     * held during the previous capture frame. An atom accumulates into
     * capture_chord only the frame it transitions from not-held to held.
     * Initialized (see enter_capture) to the atoms already held at the
     * moment capture opens, so the press that opened capture is excluded
     * without needing an arming flag - but a later re-press of that same
     * atom during capture is a fresh edge and IS captured. */
    AtomicInput capture_prev_held[SETTINGS_CAPTURE_PREV_HELD_MAX];
    int capture_prev_held_count;
    /* AXIS-mode capture only (handle_capture_axis_first): false until one
     * frame of no-input has been observed since capture began. Prevents
     * the press that opened capture from immediately resolving as an
     * axis atom. Axis capture is a separate, unchanged path from the
     * release-edge tracking above - it has no chord to accumulate, so the
     * simpler arming-frame wait still fits it. */
    bool capture_axis_armed;
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
     * save / reset. Points either at a string literal or at toast_msg_buf
     * below; no ownership either way. */
    const char *toast_text;
    float toast_timer;
    char toast_msg_buf[TOAST_MSG_BUF_CAP];
} SettingsState;

void settings_init(SettingsState *settings);

void settings_set_font(SettingsState *settings, Font font);

void settings_open(SettingsState *settings);

void settings_close(SettingsState *settings);

[[nodiscard]] bool settings_is_open(const SettingsState *settings);

/* Run one frame of input. Mutates the BindingStore via the input_func
 * mutation API and Preferences via str_clear+append on data_dir, or via
 * direct assignment to master_volume/music_volume/sfx_volume from the
 * General tab's volume rows. Sets settings->save_requested or
 * save_preferences_requested when the caller should persist the
 * corresponding store to disk. Toggles `*close_requested` if the user
 * pressed cancel from the LIST screen. */
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
