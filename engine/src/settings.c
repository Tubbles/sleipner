#include "settings.h"

#include "alloc.h"
#include "blur.h"
#include "input.h"
#include "input_func.h"
#include "keyboard_widget.h"
#include "preferences.h"
#include "raylib.h"
#include "str.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
/* Minimal forward declaration for GetLogicalDrives instead of pulling in
 * <windows.h>: the Windows GDI / USER headers redefine raylib's Rectangle,
 * CloseWindow, ShowCursor, and DrawTextEx, which would shred this whole
 * file's signatures. We only need one Win32 function. */
typedef unsigned long DWORD; // NOLINT(readability-identifier-naming) — Win32 ABI symbol
extern __declspec(dllimport) DWORD __stdcall GetLogicalDrives(void);
#endif

#define LIST_VISIBLE_ROWS 14
#define LIST_LINE_HEIGHT 40
#define LIST_TITLE_PAD 80
#define LIST_LEFT_PAD 80
#define LIST_RIGHT_PAD 80
#define LIST_LETTER_SPACING 1.5F
#define VIGNETTE_ALPHA 200

#define SETTINGS_TEXT_DEFAULT 220
#define SETTINGS_TEXT_HIGHLIGHT_R 255
#define SETTINGS_TEXT_HIGHLIGHT_G 230
#define SETTINGS_TEXT_HIGHLIGHT_B 120
#define SETTINGS_TEXT_DIM 140

#define ACTION_ROW_COUNT ACTION_COUNT
#define AXIS_ROW_COUNT AXIS_COUNT
#define RESET_ALL_ROW_INDEX (ACTION_ROW_COUNT + AXIS_ROW_COUNT)
#define LIST_TOTAL_ROWS (RESET_ALL_ROW_INDEX + 1)
#define DETAIL_ADD_ROW_OFFSET 0   /* relative to alt_count */
#define DETAIL_RESET_ROW_OFFSET 1 /* relative to alt_count */
#define DETAIL_EXTRA_ROW_COUNT 2  /* Add + Reset */

#define AXIS_THRESHOLD 0.5F
#define TRIGGER_THRESHOLD 0.0F /* raylib triggers rest at -1, so 0 means partially pulled */

#define LABEL_BUF_CAP 128
#define ROW_BUF_CAP 256
#define ATOM_BUF_CAP 64

void settings_init(SettingsState *settings)
{
    *settings = (SettingsState){0};
}

void settings_set_font(SettingsState *settings, Font font)
{
    settings->font = font;
    settings->font_loaded = true;
}

void settings_open(SettingsState *settings)
{
    settings->open = true;
    settings->screen = SETTINGS_SCREEN_LIST;
    settings->list_index = 0;
    settings->list_scroll = 0;
    settings->detail_index = 0;
    settings->detail_scroll = 0;
    settings->capture_chord_count = 0;
    settings->capture_armed = false;
    settings->save_requested = false;
    settings->toast_text = nullptr;
    settings->toast_timer = 0.0F;
}

void settings_close(SettingsState *settings)
{
    settings->open = false;
    settings->blur_captured = false;
}

bool settings_is_open(const SettingsState *settings)
{
    return settings->open;
}

void settings_cleanup(SettingsState *settings)
{
    /* path_edit_unload uses the static helper below, which cannot be
     * forward-declared without dragging the whole PathEditState block
     * upward. Inline the equivalent: drop the FilePathList allocations
     * raylib hands us while still on the live SettingsState pointer. */
    if (settings->path_edit.dir_list_loaded) {
        UnloadDirectoryFiles(settings->path_edit.dir_list);
        settings->path_edit.dir_list_loaded = false;
    }
    /* settings->font is a non-owning copy from the shared font cache
     * (font_cache_get); it is not unloaded here. */
    *settings = (SettingsState){0};
}

void settings_tick(SettingsState *settings, float delta_time)
{
    if (settings->toast_timer > 0.0F) {
        settings->toast_timer -= delta_time;
        if (settings->toast_timer <= 0.0F) {
            settings->toast_text = nullptr;
            settings->toast_timer = 0.0F;
        }
    }
}

/* --- Helpers ------------------------------------------------------------- */

static void show_toast(SettingsState *settings, const char *text)
{
    settings->toast_text = text;
    settings->toast_timer = 2.0F;
}

static int detail_alt_count(const SettingsState *settings, const BindingStore *store)
{
    if (settings->target_kind == SETTINGS_TARGET_ACTION) {
        return store->actions[settings->target_index].alternatives.count;
    }
    if (settings->target_kind == SETTINGS_TARGET_AXIS) {
        return store->axes[settings->target_index].alternatives.count;
    }
    return 0;
}

static int detail_total_rows(const SettingsState *settings, const BindingStore *store)
{
    return detail_alt_count(settings, store) + DETAIL_EXTRA_ROW_COUNT;
}

static const char *target_label(const SettingsState *settings)
{
    if (settings->target_kind == SETTINGS_TARGET_ACTION) {
        return input_func_action_toml_name_at(settings->target_index);
    }
    if (settings->target_kind == SETTINGS_TARGET_AXIS) {
        return input_func_axis_toml_name_at(settings->target_index);
    }
    return "";
}

static bool key_down_in_state(const InputState *input, int key)
{
    if (key <= 0 || key >= INPUT_MAX_KEY_CODE) {
        return false;
    }
    return (input->key_down[key / INPUT_BITS_PER_WORD] & (1ULL << (key % INPUT_BITS_PER_WORD))) != 0;
}

static bool key_pressed_in_state(const InputState *input, int key)
{
    if (key <= 0 || key >= INPUT_MAX_KEY_CODE) {
        return false;
    }
    return (input->key_pressed[key / INPUT_BITS_PER_WORD] & (1ULL << (key % INPUT_BITS_PER_WORD))) != 0;
}

static bool gp_btn_down_in_state(const InputState *input, int btn)
{
    if (btn <= 0 || btn >= INPUT_GP_BUTTON_COUNT) {
        return false;
    }
    return (input->gp_button_down & (1U << btn)) != 0;
}

static bool gp_btn_pressed_in_state(const InputState *input, int btn)
{
    if (btn <= 0 || btn >= INPUT_GP_BUTTON_COUNT) {
        return false;
    }
    return (input->gp_button_pressed & (1U << btn)) != 0;
}

static bool reserved_cancel_pressed(const InputState *input)
{
    return key_pressed_in_state(input, KEY_ESCAPE) || gp_btn_pressed_in_state(input, GAMEPAD_BUTTON_MIDDLE_RIGHT);
}

static bool atom_in_chord(const SettingsState *settings, const AtomicInput *atom)
{
    for (int index = 0; index < settings->capture_chord_count; index++) {
        const AtomicInput *existing = &settings->capture_chord[index];
        if (existing->kind == atom->kind && existing->int_a == atom->int_a) {
            return true;
        }
    }
    return false;
}

static void chord_add(SettingsState *settings, AtomicInput atom)
{
    if (settings->capture_chord_count >= SETTINGS_MAX_CHORD_ATOMS) {
        return;
    }
    if (atom_in_chord(settings, &atom)) {
        return;
    }
    settings->capture_chord[settings->capture_chord_count] = atom;
    settings->capture_chord_count++;
}

/* Count bindable keys / buttons currently held this frame. Reserved cancels
 * (Esc, Start) are excluded so the user holding Esc to abort doesn't keep
 * the held set non-empty. */
static int count_bindable_held_atoms(const InputState *input)
{
    int count = 0;
    for (int key = 1; key < INPUT_MAX_KEY_CODE; key++) {
        if (key == KEY_ESCAPE) {
            continue;
        }
        if (!key_down_in_state(input, key)) {
            continue;
        }
        if (!input_func_key_name(key)) {
            continue;
        }
        count++;
    }
    for (int btn = 1; btn < INPUT_GP_BUTTON_COUNT; btn++) {
        if (btn == GAMEPAD_BUTTON_MIDDLE_RIGHT) {
            continue;
        }
        if (!gp_btn_down_in_state(input, btn)) {
            continue;
        }
        if (!input_func_gp_button_name(btn)) {
            continue;
        }
        count++;
    }
    return count;
}

/* Walk all bindable keys/buttons; for each currently-held one, add to
 * the high-water mark. */
static void accumulate_held_atoms(SettingsState *settings, const InputState *input)
{
    for (int key = 1; key < INPUT_MAX_KEY_CODE; key++) {
        if (key == KEY_ESCAPE) {
            continue;
        }
        if (!key_down_in_state(input, key)) {
            continue;
        }
        if (!input_func_key_name(key)) {
            continue;
        }
        AtomicInput atom = {.kind = ATOM_KEY, .int_a = key, .int_b = 0, .scale = 1.0F};
        chord_add(settings, atom);
    }
    for (int btn = 1; btn < INPUT_GP_BUTTON_COUNT; btn++) {
        if (btn == GAMEPAD_BUTTON_MIDDLE_RIGHT) {
            continue;
        }
        if (!gp_btn_down_in_state(input, btn)) {
            continue;
        }
        if (!input_func_gp_button_name(btn)) {
            continue;
        }
        AtomicInput atom = {.kind = ATOM_GP_BUTTON, .int_a = btn, .int_b = 0, .scale = 1.0F};
        chord_add(settings, atom);
    }
}

static int find_pressed_named_key(const InputState *input)
{
    for (int key = 1; key < INPUT_MAX_KEY_CODE; key++) {
        if (key == KEY_ESCAPE) {
            continue;
        }
        if (!key_pressed_in_state(input, key)) {
            continue;
        }
        if (!input_func_key_name(key)) {
            continue;
        }
        return key;
    }
    return 0;
}

/* Construct a stack-borrowed PhysicalInput pointing at the capture_chord
 * array. clone_physical inside the mutation API only reads count and data,
 * so this view's alloc field can be zero. */
static PhysicalInput chord_as_physical(const SettingsState *settings)
{
    PhysicalInput view = {0};
    view.parts.data = (AtomicInput *)settings->capture_chord;
    view.parts.count = settings->capture_chord_count;
    view.parts.capacity = SETTINGS_MAX_CHORD_ATOMS;
    return view;
}

static void
apply_action_alternative(SettingsState *settings, BindingStore *store, Allocator alloc, const PhysicalInput *physical)
{
    bool success = false;
    if (settings->capture_alt_index >= 0) {
        success = input_func_set_action_alternative(store, alloc, (InputAction)settings->target_index,
                                                    settings->capture_alt_index, physical);
    } else {
        success = input_func_add_action_alternative(store, alloc, (InputAction)settings->target_index, physical);
    }
    if (success) {
        settings->save_requested = true;
        show_toast(settings, "Bound");
    } else {
        show_toast(settings, "Bind failed");
    }
}

static void
apply_axis_alternative(SettingsState *settings, BindingStore *store, Allocator alloc, const PhysicalInput *physical)
{
    bool success = false;
    if (settings->capture_alt_index >= 0) {
        success = input_func_set_axis_alternative(store, alloc, (InputAxis)settings->target_index,
                                                  settings->capture_alt_index, physical);
    } else {
        success = input_func_add_axis_alternative(store, alloc, (InputAxis)settings->target_index, physical);
    }
    if (success) {
        settings->save_requested = true;
        show_toast(settings, "Bound");
    } else {
        show_toast(settings, "Bind failed");
    }
}

/* --- LIST screen --------------------------------------------------------- */

static void update_list_scroll(SettingsState *settings)
{
    if (settings->list_index < settings->list_scroll) {
        settings->list_scroll = settings->list_index;
    } else if (settings->list_index >= settings->list_scroll + LIST_VISIBLE_ROWS) {
        settings->list_scroll = settings->list_index - LIST_VISIBLE_ROWS + 1;
    }
}

static void enter_detail_action(SettingsState *settings, InputAction action)
{
    settings->target_kind = SETTINGS_TARGET_ACTION;
    settings->target_index = action;
    settings->screen = SETTINGS_SCREEN_DETAIL;
    settings->detail_index = 0;
    settings->detail_scroll = 0;
}

static void enter_detail_axis(SettingsState *settings, InputAxis axis)
{
    settings->target_kind = SETTINGS_TARGET_AXIS;
    settings->target_index = axis;
    settings->screen = SETTINGS_SCREEN_DETAIL;
    settings->detail_index = 0;
    settings->detail_scroll = 0;
}

/* General tab: one row in v1, "Data directory: <value>". */
#define GENERAL_TOTAL_ROWS 1

#define PATH_EDIT_VISIBLE_ROWS 12

static void path_edit_unload(PathEditState *path_edit)
{
    if (path_edit->dir_list_loaded) {
        UnloadDirectoryFiles(path_edit->dir_list);
        path_edit->dir_list_loaded = false;
        path_edit->dir_list = (FilePathList){0};
    }
}

/* Normalize separators in place: convert every backslash to forward slash
 * and collapse runs of slashes to a single slash. raylib joins paths with
 * '\\' on _WIN32 (so the Proton/MinGW build pulls "/\data" out of root),
 * and our own concatenation can produce "//data". One canonical form
 * means the commit, the parent walk, and the displayed string all agree. */
static void path_normalize(char *buf, int *len_out)
{
    if (buf == nullptr) {
        return;
    }
    for (char *cursor = buf; *cursor != '\0'; cursor++) {
        if (*cursor == '\\') {
            *cursor = '/';
        }
    }
    char *write = buf;
    char prev = '\0';
    for (char *read = buf; *read != '\0'; read++) {
        if (*read == '/' && prev == '/') {
            continue;
        }
        *write = *read;
        prev = *read;
        write++;
    }
    *write = '\0';
    if (len_out != nullptr) {
        *len_out = (int)(write - buf);
    }
}

/* True when path is filesystem-absolute. Linux/Android: leading '/'.
 * Windows: 'X:' drive prefix or leading '/' (Wine canonicalizes both).
 * The check is relaxed enough that the same code path serves a Windows
 * binary running under Wine without a separate ifdef. */
static bool path_is_absolute(const char *path)
{
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':') {
        return true;
    }
    return false;
}

/* Detect whether a path is the filesystem root: parent of root resolves
 * to root again. */
static bool path_is_root(const char *path)
{
    const char *parent = GetPrevDirectoryPath(path);
    return parent == nullptr || strcmp(parent, path) == 0;
}

/* Drop trailing slashes (except when the path itself is just "/"). raylib's
 * LoadDirectoryFilesEx is fine either way on POSIX, but stripping the slash
 * keeps GetPrevDirectoryPath's i==0 root check on the right branch and makes
 * the displayed buffer line up with how the user typed it. */
static void path_trim_trailing_slash(char *buf, int *len_out)
{
    int len = (int)strlen(buf);
    while (len > 1 && buf[len - 1] == '/') {
        buf[len - 1] = '\0';
        len--;
    }
    if (len_out != nullptr) {
        *len_out = len;
    }
}

static void path_edit_set_buf(PathEditState *path_edit, const char *value)
{
    if (value == nullptr) {
        path_edit->buf[0] = '\0';
        path_edit->len = 0;
        return;
    }
    size_t length = strlen(value);
    if (length >= PATH_EDIT_BUF_SIZE) {
        length = PATH_EDIT_BUF_SIZE - 1;
    }
    memcpy(path_edit->buf, value, length);
    path_edit->buf[length] = '\0';
    path_edit->len = (int)length;
    path_normalize(path_edit->buf, &path_edit->len);
}

/* Compose buf = cwd + "/" + buf when buf is relative. Falls back to leaving
 * buf untouched if raylib cannot resolve cwd (no real filesystem under tests
 * is rare, but the guard keeps the headless test fixture safe). */
static void path_edit_make_absolute(PathEditState *path_edit)
{
    if (path_is_absolute(path_edit->buf)) {
        return;
    }
    const char *cwd = GetWorkingDirectory();
    if (cwd == nullptr || cwd[0] == '\0') {
        return;
    }
    char joined[PATH_EDIT_BUF_SIZE];
    int written = snprintf(joined, sizeof(joined), "%s/%s", cwd, path_edit->buf);
    if (written < 0 || (size_t)written >= sizeof(joined)) {
        return;
    }
    path_edit_set_buf(path_edit, joined);
}

static void path_edit_refresh(PathEditState *path_edit)
{
    path_edit_unload(path_edit);
    path_normalize(path_edit->buf, &path_edit->len);
    path_trim_trailing_slash(path_edit->buf, &path_edit->len);
    path_edit_make_absolute(path_edit);
    path_edit->dir_list = LoadDirectoryFilesEx(path_edit->buf, "DIRS*", false);
    path_edit->dir_list_loaded = true;
    path_edit->at_root = path_is_root(path_edit->buf);
    path_edit->browse_index = 0;
    path_edit->browse_scroll = 0;
}

/* Row index meanings (in the browse list):
 *   PATH_EDIT_KIND_USE_THIS    : synthesized first row "<USE THIS DIRECTORY>".
 *                                CONFIRM commits the current buf and exits.
 *   PATH_EDIT_KIND_SELECT_DRIVE: synthesized second row "<SELECT DRIVE>".
 *                                CONFIRM transitions to DRIVE_SELECT mode so
 *                                the user can jump to another filesystem
 *                                root. Always present, useful primarily on
 *                                Windows; on POSIX the drive list collapses
 *                                to a single "/" entry.
 *   PATH_EDIT_KIND_DOTDOT      : synthesized ".." row, only present when not
 *                                at filesystem root. CONFIRM goes up one level.
 *   PATH_EDIT_KIND_DIR         : index into FilePathList, points at a
 *                                subdirectory. CONFIRM enters that folder.
 *
 * The synthesized rows live at fixed UI indices so the user lands on
 * USE_THIS by default (browse_index initialized to 0), making the
 * one-press "open screen, A, save current path" workflow work without
 * needing a separate ACTION_INTERACT binding (which would clash with
 * ACTION_CONFIRM since both default to gamepad RIGHT_FACE_DOWN). */
typedef enum {
    PATH_EDIT_KIND_USE_THIS,
    PATH_EDIT_KIND_SELECT_DRIVE,
    PATH_EDIT_KIND_DOTDOT,
    PATH_EDIT_KIND_DIR,
} PathEditRowKind;

#define PATH_EDIT_ROW_USE_THIS 0
#define PATH_EDIT_ROW_SELECT_DRIVE 1

static int path_edit_dotdot_row_index(const PathEditState *path_edit)
{
    return path_edit->at_root ? -1 : 2;
}

static int path_edit_first_dir_row(const PathEditState *path_edit)
{
    return path_edit->at_root ? 2 : 3;
}

static int path_edit_total_rows(const PathEditState *path_edit)
{
    int rows = 2; /* USE_THIS + SELECT_DRIVE */
    if (!path_edit->at_root) {
        rows += 1; /* .. */
    }
    if (path_edit->dir_list_loaded) {
        rows += (int)path_edit->dir_list.count;
    }
    return rows;
}

static PathEditRowKind path_edit_row_kind(const PathEditState *path_edit, int row, int *file_index_out)
{
    *file_index_out = -1;
    if (row == PATH_EDIT_ROW_USE_THIS) {
        return PATH_EDIT_KIND_USE_THIS;
    }
    if (row == PATH_EDIT_ROW_SELECT_DRIVE) {
        return PATH_EDIT_KIND_SELECT_DRIVE;
    }
    if (row == path_edit_dotdot_row_index(path_edit)) {
        return PATH_EDIT_KIND_DOTDOT;
    }
    int file_index = row - path_edit_first_dir_row(path_edit);
    *file_index_out = file_index;
    return PATH_EDIT_KIND_DIR;
}

/* Populate path_edit->drives with the OS-visible filesystem roots.
 * Windows: read GetLogicalDrives() and emit "X:\" for each set bit.
 * POSIX (Linux/Android/macOS): a single "/" entry — the platform has
 * no drive concept, but the entry still gives the user a "jump to
 * root" shortcut without leaving the picker. */
static void path_edit_populate_drives(PathEditState *path_edit)
{
    path_edit->drive_count = 0;
    path_edit->drive_index = 0;
    path_edit->drive_scroll = 0;
#if defined(_WIN32)
    DWORD mask = GetLogicalDrives();
    for (int letter = 0; letter < 26 && path_edit->drive_count < PATH_EDIT_DRIVE_MAX; letter++) {
        if ((mask & (1U << letter)) == 0) {
            continue;
        }
        char *slot = path_edit->drives[path_edit->drive_count];
        slot[0] = (char)('A' + letter);
        slot[1] = ':';
        slot[2] = '/';
        slot[3] = '\0';
        path_edit->drive_count++;
    }
    if (path_edit->drive_count > 0) {
        return;
    }
#endif
    /* POSIX fallback (also reached on Windows when GetLogicalDrives
     * returns no bits, which would only happen in heavily sandboxed
     * environments): present "/" so the user can still escape to
     * a deterministic root. */
    char *slot = path_edit->drives[0];
    slot[0] = '/';
    slot[1] = '\0';
    path_edit->drive_count = 1;
}

static void path_edit_enter_screen(SettingsState *settings, const Preferences *preferences)
{
    PathEditState *path_edit = &settings->path_edit;
    *path_edit = (PathEditState){0};
    path_edit->mode = PATH_EDIT_BROWSE;
    const char *seed =
        (preferences != nullptr && preferences->data_dir.ptr != nullptr) ? preferences->data_dir.ptr : "";
    path_edit_set_buf(path_edit, seed);
    keyboard_widget_reset(&path_edit->kb, path_edit->buf, &path_edit->len, PATH_EDIT_BUF_SIZE);
    path_edit_refresh(path_edit);
    settings->screen = SETTINGS_SCREEN_PATH_EDIT;
}

static void path_edit_commit(SettingsState *settings, Preferences *preferences)
{
    if (preferences == nullptr) {
        return;
    }
    /* Final normalize before saving: the buffer already passed through
     * path_normalize during refresh, but a fresh commit from keyboard
     * mode (where the user can splice in any chars) needs the same
     * canonicalization so the saved data_dir is forward-slash-only. */
    PathEditState *path_edit = &settings->path_edit;
    path_normalize(path_edit->buf, &path_edit->len);
    str_clear(&preferences->data_dir);
    (void)str_append_cstr(&preferences->data_dir, path_edit->buf);
    /* Ensure exactly one trailing slash so "data_dir + filename"
     * composition still works for downstream callers. path_normalize
     * already collapsed any duplicate slashes. */
    if (preferences->data_dir.len == 0 || preferences->data_dir.ptr[preferences->data_dir.len - 1] != '/') {
        (void)str_append_cstr(&preferences->data_dir, "/");
    }
    settings->save_preferences_requested = true;
}

static void path_edit_exit(SettingsState *settings)
{
    path_edit_unload(&settings->path_edit);
    settings->screen = SETTINGS_SCREEN_LIST;
}

static void update_browse_scroll(PathEditState *path_edit)
{
    if (path_edit->browse_index < path_edit->browse_scroll) {
        path_edit->browse_scroll = path_edit->browse_index;
    } else if (path_edit->browse_index >= path_edit->browse_scroll + PATH_EDIT_VISIBLE_ROWS) {
        path_edit->browse_scroll = path_edit->browse_index - PATH_EDIT_VISIBLE_ROWS + 1;
    }
}

static void path_edit_navigate_index(PathEditState *path_edit, const InputState *input, const BindingStore *store)
{
    int total_rows = path_edit_total_rows(path_edit);
    if (total_rows <= 0) {
        total_rows = 1;
    }
    if (input_pressed(input, store, ACTION_NAV_UP) && path_edit->browse_index > 0) {
        path_edit->browse_index--;
    }
    if (input_pressed(input, store, ACTION_NAV_DOWN) && path_edit->browse_index < total_rows - 1) {
        path_edit->browse_index++;
    }
    if (input_pressed(input, store, ACTION_PAGE_UP)) {
        path_edit->browse_index -= PATH_EDIT_VISIBLE_ROWS;
        if (path_edit->browse_index < 0) {
            path_edit->browse_index = 0;
        }
    }
    if (input_pressed(input, store, ACTION_PAGE_DOWN)) {
        path_edit->browse_index += PATH_EDIT_VISIBLE_ROWS;
        if (path_edit->browse_index >= total_rows) {
            path_edit->browse_index = total_rows - 1;
        }
    }
    update_browse_scroll(path_edit);
}

static void path_edit_enter_drive_select(PathEditState *path_edit)
{
    path_edit_populate_drives(path_edit);
    path_edit->mode = PATH_EDIT_DRIVE_SELECT;
}

/* Returns true when the row dispatch already committed + exited the
 * screen (row was USE_THIS); the caller must not touch settings/path_edit
 * after that point. */
static bool path_edit_dispatch_confirm(SettingsState *settings, Preferences *preferences)
{
    PathEditState *path_edit = &settings->path_edit;
    int file_index = -1;
    PathEditRowKind kind = path_edit_row_kind(path_edit, path_edit->browse_index, &file_index);
    switch (kind) {
    case PATH_EDIT_KIND_USE_THIS:
        path_edit_commit(settings, preferences);
        path_edit_exit(settings);
        show_toast(settings, "Data directory saved");
        return true;
    case PATH_EDIT_KIND_SELECT_DRIVE:
        path_edit_enter_drive_select(path_edit);
        return false;
    case PATH_EDIT_KIND_DOTDOT: {
        const char *parent = GetPrevDirectoryPath(path_edit->buf);
        if (parent != nullptr) {
            path_edit_set_buf(path_edit, parent);
            path_edit_refresh(path_edit);
        }
        return false;
    }
    case PATH_EDIT_KIND_DIR:
        if (file_index >= 0 && path_edit->dir_list_loaded && file_index < (int)path_edit->dir_list.count) {
            path_edit_set_buf(path_edit, path_edit->dir_list.paths[file_index]);
            path_edit_refresh(path_edit);
        }
        return false;
    }
    return false;
}

static bool path_edit_pop_to_parent(PathEditState *path_edit)
{
    if (path_edit->at_root) {
        return false;
    }
    const char *parent = GetPrevDirectoryPath(path_edit->buf);
    if (parent == nullptr) {
        return false;
    }
    path_edit_set_buf(path_edit, parent);
    path_edit_refresh(path_edit);
    return true;
}

static void handle_path_edit_browse(SettingsState *settings,
                                    Preferences *preferences,
                                    const InputState *input,
                                    const BindingStore *store)
{
    PathEditState *path_edit = &settings->path_edit;
    path_edit_navigate_index(path_edit, input, store);

    if (input_pressed(input, store, ACTION_WB_KEYBOARD_MODE)) {
        path_edit->mode = PATH_EDIT_KEYBOARD;
        keyboard_widget_reset(&path_edit->kb, path_edit->buf, &path_edit->len, PATH_EDIT_BUF_SIZE);
        return;
    }
    if (input_pressed(input, store, ACTION_CONFIRM)) {
        (void)path_edit_dispatch_confirm(settings, preferences);
        return;
    }
    if (input_pressed(input, store, ACTION_CANCEL) && !path_edit_pop_to_parent(path_edit)) {
        path_edit_exit(settings);
    }
}

static void handle_path_edit_keyboard(SettingsState *settings,
                                      Preferences *preferences,
                                      const InputState *input,
                                      const BindingStore *store)
{
    (void)preferences;
    if (input_pressed(input, store, ACTION_WB_KEYBOARD_MODE)) {
        settings->path_edit.mode = PATH_EDIT_BROWSE;
        path_edit_refresh(&settings->path_edit);
        return;
    }
    KeyboardWidgetResult result = keyboard_widget_handle_input(&settings->path_edit.kb, input, store);
    /* The widget signals EXIT_REQUESTED on CANCEL at top-level group with
     * an empty buf — its native "I'm done typing" gesture for the editor
     * word builder. Path-edit treats it as "drop me back to browse" so
     * the user always exits via the browse list's <USE THIS DIRECTORY>
     * row, never by accident from the keyboard widget. The buf is
     * preserved across the toggle. */
    if (result == KB_RESULT_EXIT_REQUESTED) {
        settings->path_edit.mode = PATH_EDIT_BROWSE;
        path_edit_refresh(&settings->path_edit);
    }
}

static void handle_path_edit_drive_select(SettingsState *settings, const InputState *input, const BindingStore *store)
{
    PathEditState *path_edit = &settings->path_edit;
    int total = path_edit->drive_count;
    if (total <= 0) {
        path_edit->mode = PATH_EDIT_BROWSE;
        return;
    }
    if (input_pressed(input, store, ACTION_NAV_UP) && path_edit->drive_index > 0) {
        path_edit->drive_index--;
    }
    if (input_pressed(input, store, ACTION_NAV_DOWN) && path_edit->drive_index < total - 1) {
        path_edit->drive_index++;
    }
    if (input_pressed(input, store, ACTION_PAGE_UP)) {
        path_edit->drive_index -= PATH_EDIT_VISIBLE_ROWS;
        if (path_edit->drive_index < 0) {
            path_edit->drive_index = 0;
        }
    }
    if (input_pressed(input, store, ACTION_PAGE_DOWN)) {
        path_edit->drive_index += PATH_EDIT_VISIBLE_ROWS;
        if (path_edit->drive_index >= total) {
            path_edit->drive_index = total - 1;
        }
    }
    if (path_edit->drive_index < path_edit->drive_scroll) {
        path_edit->drive_scroll = path_edit->drive_index;
    } else if (path_edit->drive_index >= path_edit->drive_scroll + PATH_EDIT_VISIBLE_ROWS) {
        path_edit->drive_scroll = path_edit->drive_index - PATH_EDIT_VISIBLE_ROWS + 1;
    }
    if (input_pressed(input, store, ACTION_CANCEL)) {
        path_edit->mode = PATH_EDIT_BROWSE;
        return;
    }
    if (input_pressed(input, store, ACTION_CONFIRM)) {
        path_edit_set_buf(path_edit, path_edit->drives[path_edit->drive_index]);
        path_edit->mode = PATH_EDIT_BROWSE;
        path_edit_refresh(path_edit);
    }
}

static void handle_path_edit_input(SettingsState *settings,
                                   Preferences *preferences,
                                   const InputState *input,
                                   const BindingStore *store)
{
    switch (settings->path_edit.mode) {
    case PATH_EDIT_KEYBOARD:
        handle_path_edit_keyboard(settings, preferences, input, store);
        break;
    case PATH_EDIT_DRIVE_SELECT:
        handle_path_edit_drive_select(settings, input, store);
        break;
    case PATH_EDIT_BROWSE:
        handle_path_edit_browse(settings, preferences, input, store);
        break;
    }
}

static void handle_general_tab_input(SettingsState *settings,
                                     const InputState *input,
                                     const BindingStore *store,
                                     const Preferences *preferences,
                                     bool *close_requested)
{
    if (input_pressed(input, store, ACTION_NAV_UP) && settings->general_index > 0) {
        settings->general_index--;
    }
    /* NAV_DOWN clamp is structured for future rows; today GENERAL_TOTAL_ROWS == 1
     * makes the bounds check trivially false. Lint correctly notes the
     * degeneracy; suppressing it keeps the parameterized shape. */
    // NOLINTNEXTLINE(misc-redundant-expression)
    if (input_pressed(input, store, ACTION_NAV_DOWN) && settings->general_index < GENERAL_TOTAL_ROWS - 1) {
        settings->general_index++;
    }
    if (input_pressed(input, store, ACTION_CANCEL)) {
        *close_requested = true;
        return;
    }
    if (input_pressed(input, store, ACTION_CONFIRM)) {
        if (settings->general_index == 0) {
            path_edit_enter_screen(settings, preferences);
        }
    }
}

static void handle_input_tab_body(
    SettingsState *settings, const InputState *input, BindingStore *store, Allocator alloc, bool *close_requested)
{
    if (input_pressed(input, store, ACTION_NAV_UP) && settings->list_index > 0) {
        settings->list_index--;
    }
    if (input_pressed(input, store, ACTION_NAV_DOWN) && settings->list_index < LIST_TOTAL_ROWS - 1) {
        settings->list_index++;
    }
    if (input_pressed(input, store, ACTION_PAGE_UP)) {
        settings->list_index -= LIST_VISIBLE_ROWS;
        if (settings->list_index < 0) {
            settings->list_index = 0;
        }
    }
    if (input_pressed(input, store, ACTION_PAGE_DOWN)) {
        settings->list_index += LIST_VISIBLE_ROWS;
        if (settings->list_index >= LIST_TOTAL_ROWS) {
            settings->list_index = LIST_TOTAL_ROWS - 1;
        }
    }
    update_list_scroll(settings);

    if (input_pressed(input, store, ACTION_CANCEL)) {
        *close_requested = true;
        return;
    }
    if (!input_pressed(input, store, ACTION_CONFIRM)) {
        return;
    }
    if (settings->list_index >= 0 && settings->list_index < ACTION_ROW_COUNT) {
        enter_detail_action(settings, (InputAction)settings->list_index);
    } else if (settings->list_index >= ACTION_ROW_COUNT && settings->list_index < ACTION_ROW_COUNT + AXIS_ROW_COUNT) {
        int axis_index = settings->list_index - ACTION_ROW_COUNT;
        if (axis_index >= 0 && axis_index < AXIS_COUNT) {
            enter_detail_axis(settings, (InputAxis)axis_index);
        }
    } else {
        input_func_reset_all_to_defaults(store, alloc);
        settings->save_requested = true;
        show_toast(settings, "All bindings reset");
    }
}

static void handle_list_input(SettingsState *settings,
                              const InputState *input,
                              BindingStore *store,
                              Preferences *preferences,
                              Allocator alloc,
                              bool *close_requested)
{
    /* Tab switching wins over per-tab navigation: TAB_PREV/NEXT are
     * checked first, then early-return so per-tab handlers do not also
     * see the press (PAGE_UP/DOWN share L1/R1 with TAB_PREV/NEXT on
     * gamepad — same order-sensitivity pattern as ACTION_EDITOR_UNDO
     * vs ACTION_NAV_LEFT). */
    if (input_pressed(input, store, ACTION_TAB_PREV) && settings->tab > SETTINGS_TAB_INPUT) {
        settings->tab--;
        return;
    }
    if (input_pressed(input, store, ACTION_TAB_NEXT) && settings->tab < SETTINGS_TAB_COUNT - 1) {
        settings->tab++;
        return;
    }
    switch (settings->tab) {
    case SETTINGS_TAB_INPUT:
        handle_input_tab_body(settings, input, store, alloc, close_requested);
        break;
    case SETTINGS_TAB_GENERAL:
        handle_general_tab_input(settings, input, store, preferences, close_requested);
        break;
    case SETTINGS_TAB_COUNT:
        break;
    }
}

/* --- DETAIL screen ------------------------------------------------------- */

static void update_detail_scroll(SettingsState *settings)
{
    if (settings->detail_index < settings->detail_scroll) {
        settings->detail_scroll = settings->detail_index;
    } else if (settings->detail_index >= settings->detail_scroll + LIST_VISIBLE_ROWS) {
        settings->detail_scroll = settings->detail_index - LIST_VISIBLE_ROWS + 1;
    }
}

static void enter_capture(SettingsState *settings, int alt_index)
{
    settings->capture_alt_index = alt_index;
    settings->capture_chord_count = 0;
    settings->capture_armed = false;
    settings->capture_kb_axis_neg_key = 0;
    settings->screen = SETTINGS_SCREEN_CAPTURE;
    if (settings->target_kind == SETTINGS_TARGET_ACTION) {
        settings->capture_mode = SETTINGS_CAPTURE_ACTION;
    } else {
        settings->capture_mode = SETTINGS_CAPTURE_AXIS_FIRST;
    }
}

static void delete_alternative(SettingsState *settings, BindingStore *store)
{
    int alt_count = detail_alt_count(settings, store);
    if (settings->detail_index >= alt_count) {
        return;
    }
    if (settings->target_kind == SETTINGS_TARGET_ACTION) {
        input_func_remove_action_alternative(store, (InputAction)settings->target_index, settings->detail_index);
    } else {
        input_func_remove_axis_alternative(store, (InputAxis)settings->target_index, settings->detail_index);
    }
    int new_count = detail_alt_count(settings, store);
    if (settings->detail_index >= new_count + DETAIL_EXTRA_ROW_COUNT) {
        settings->detail_index = new_count + DETAIL_EXTRA_ROW_COUNT - 1;
    }
    settings->save_requested = true;
    show_toast(settings, "Removed");
}

static void reset_target_to_defaults(SettingsState *settings, BindingStore *store, Allocator alloc)
{
    if (settings->target_kind == SETTINGS_TARGET_ACTION) {
        input_func_reset_action_to_defaults(store, alloc, (InputAction)settings->target_index);
    } else {
        input_func_reset_axis_to_defaults(store, alloc, (InputAxis)settings->target_index);
    }
    settings->save_requested = true;
    settings->detail_index = 0;
    show_toast(settings, "Reset to defaults");
}

static void handle_detail_input(SettingsState *settings, const InputState *input, BindingStore *store, Allocator alloc)
{
    int total = detail_total_rows(settings, store);
    int alt_count = total - DETAIL_EXTRA_ROW_COUNT;

    if (input_pressed(input, store, ACTION_NAV_UP) && settings->detail_index > 0) {
        settings->detail_index--;
    }
    if (input_pressed(input, store, ACTION_NAV_DOWN) && settings->detail_index < total - 1) {
        settings->detail_index++;
    }
    update_detail_scroll(settings);

    if (input_pressed(input, store, ACTION_CANCEL)) {
        settings->screen = SETTINGS_SCREEN_LIST;
        return;
    }
    if (input_pressed(input, store, ACTION_EDITOR_DELETE) && settings->detail_index < alt_count) {
        delete_alternative(settings, store);
        return;
    }
    if (!input_pressed(input, store, ACTION_CONFIRM)) {
        return;
    }
    if (settings->detail_index < alt_count) {
        enter_capture(settings, settings->detail_index);
    } else if (settings->detail_index == alt_count + DETAIL_ADD_ROW_OFFSET) {
        enter_capture(settings, -1);
    } else {
        reset_target_to_defaults(settings, store, alloc);
    }
}

/* --- CAPTURE screen ------------------------------------------------------ */

static void
handle_capture_action(SettingsState *settings, const InputState *input, BindingStore *store, Allocator alloc)
{
    if (reserved_cancel_pressed(input)) {
        settings->screen = SETTINGS_SCREEN_DETAIL;
        return;
    }
    int held = count_bindable_held_atoms(input);
    if (!settings->capture_armed) {
        if (held == 0) {
            settings->capture_armed = true;
        }
        return;
    }
    accumulate_held_atoms(settings, input);
    if (held == 0 && settings->capture_chord_count > 0) {
        PhysicalInput view = chord_as_physical(settings);
        apply_action_alternative(settings, store, alloc, &view);
        settings->capture_chord_count = 0;
        settings->screen = SETTINGS_SCREEN_DETAIL;
    }
}

static int find_active_axis(const InputState *input)
{
    for (int axis = 0; axis < INPUT_GP_AXIS_COUNT; axis++) {
        if (axis == GAMEPAD_AXIS_LEFT_TRIGGER || axis == GAMEPAD_AXIS_RIGHT_TRIGGER) {
            continue;
        }
        if (fabsf(input->gp_axis[axis]) >= AXIS_THRESHOLD) {
            return axis;
        }
    }
    return -1;
}

static int find_active_trigger(const InputState *input)
{
    if (input->gp_axis[GAMEPAD_AXIS_LEFT_TRIGGER] >= TRIGGER_THRESHOLD) {
        return GAMEPAD_AXIS_LEFT_TRIGGER;
    }
    if (input->gp_axis[GAMEPAD_AXIS_RIGHT_TRIGGER] >= TRIGGER_THRESHOLD) {
        return GAMEPAD_AXIS_RIGHT_TRIGGER;
    }
    return -1;
}

static void finalize_axis_atom(SettingsState *settings, BindingStore *store, Allocator alloc, AtomicInput atom)
{
    settings->capture_chord[0] = atom;
    settings->capture_chord_count = 1;
    PhysicalInput view = chord_as_physical(settings);
    apply_axis_alternative(settings, store, alloc, &view);
    settings->capture_chord_count = 0;
    settings->screen = SETTINGS_SCREEN_DETAIL;
}

static void
handle_capture_axis_first(SettingsState *settings, const InputState *input, BindingStore *store, Allocator alloc)
{
    if (reserved_cancel_pressed(input)) {
        settings->screen = SETTINGS_SCREEN_DETAIL;
        return;
    }
    /* Wait one no-input frame to arm, otherwise the press that opened
     * capture (e.g. Confirm = Enter) immediately resolves as a kb_axis
     * neg_key. */
    int held = count_bindable_held_atoms(input);
    if (!settings->capture_armed) {
        if (held == 0) {
            settings->capture_armed = true;
        }
        return;
    }
    int axis = find_active_axis(input);
    if (axis >= 0) {
        AtomicInput atom = {.kind = ATOM_GP_AXIS, .int_a = axis, .int_b = 0, .scale = 1.0F};
        finalize_axis_atom(settings, store, alloc, atom);
        return;
    }
    int trigger = find_active_trigger(input);
    if (trigger >= 0) {
        AtomicInput atom = {.kind = ATOM_GP_TRIGGER, .int_a = trigger, .int_b = 0, .scale = 1.0F};
        finalize_axis_atom(settings, store, alloc, atom);
        return;
    }
    int key = find_pressed_named_key(input);
    if (key) {
        settings->capture_kb_axis_neg_key = key;
        settings->capture_mode = SETTINGS_CAPTURE_AXIS_SECOND;
    }
}

static void
handle_capture_axis_second(SettingsState *settings, const InputState *input, BindingStore *store, Allocator alloc)
{
    if (reserved_cancel_pressed(input)) {
        settings->capture_mode = SETTINGS_CAPTURE_AXIS_FIRST;
        settings->capture_kb_axis_neg_key = 0;
        return;
    }
    int key = find_pressed_named_key(input);
    if (key) {
        AtomicInput atom = {
            .kind = ATOM_KB_AXIS, .int_a = settings->capture_kb_axis_neg_key, .int_b = key, .scale = 1.0F};
        finalize_axis_atom(settings, store, alloc, atom);
    }
}

static void handle_capture_input(SettingsState *settings, const InputState *input, BindingStore *store, Allocator alloc)
{
    switch (settings->capture_mode) {
    case SETTINGS_CAPTURE_ACTION:
        handle_capture_action(settings, input, store, alloc);
        break;
    case SETTINGS_CAPTURE_AXIS_FIRST:
        handle_capture_axis_first(settings, input, store, alloc);
        break;
    case SETTINGS_CAPTURE_AXIS_SECOND:
        handle_capture_axis_second(settings, input, store, alloc);
        break;
    }
}

void settings_handle_input(SettingsState *settings,
                           const InputState *input,
                           BindingStore *store,
                           Preferences *preferences,
                           Allocator alloc,
                           bool *close_requested)
{
    switch (settings->screen) {
    case SETTINGS_SCREEN_LIST:
        handle_list_input(settings, input, store, preferences, alloc, close_requested);
        break;
    case SETTINGS_SCREEN_DETAIL:
        handle_detail_input(settings, input, store, alloc);
        break;
    case SETTINGS_SCREEN_CAPTURE:
        handle_capture_input(settings, input, store, alloc);
        break;
    case SETTINGS_SCREEN_PATH_EDIT:
        handle_path_edit_input(settings, preferences, input, store);
        break;
    }
}

/* --- Render -------------------------------------------------------------- */

static Color color_for_row(bool selected, bool dim)
{
    if (selected) {
        return (Color){SETTINGS_TEXT_HIGHLIGHT_R, SETTINGS_TEXT_HIGHLIGHT_G, SETTINGS_TEXT_HIGHLIGHT_B, 255};
    }
    if (dim) {
        return (Color){SETTINGS_TEXT_DIM, SETTINGS_TEXT_DIM, SETTINGS_TEXT_DIM, 255};
    }
    return (Color){SETTINGS_TEXT_DEFAULT, SETTINGS_TEXT_DEFAULT, SETTINGS_TEXT_DEFAULT, 255};
}

static void draw_text(const SettingsState *settings, const char *text, int pos_x, int pos_y, Color color)
{
    DrawTextEx(settings->font, text, (Vector2){(float)pos_x, (float)pos_y}, (float)SETTINGS_FONT_SIZE,
               LIST_LETTER_SPACING, color);
}

static void list_row_label(int row, char *out, size_t cap, const BindingStore *store)
{
    if (row >= 0 && row < ACTION_ROW_COUNT) {
        const char *name = input_func_action_toml_name_at(row);
        char binding[LABEL_BUF_CAP] = {0};
        (void)input_func_label_at(store, row, binding, sizeof(binding));
        (void)snprintf(out, cap, "%-26s %s", name ? name : "", binding);
        return;
    }
    if (row >= ACTION_ROW_COUNT && row < ACTION_ROW_COUNT + AXIS_ROW_COUNT) {
        int axis_index = row - ACTION_ROW_COUNT;
        const char *name = input_func_axis_toml_name_at(axis_index);
        (void)snprintf(out, cap, "%s", name ? name : "");
        return;
    }
    (void)snprintf(out, cap, "Reset all to defaults");
}

static const char *tab_label(SettingsTab tab)
{
    switch (tab) {
    case SETTINGS_TAB_INPUT:
        return "Input";
    case SETTINGS_TAB_GENERAL:
        return "General";
    case SETTINGS_TAB_COUNT:
        break;
    }
    return "";
}

static void render_tab_header(const SettingsState *settings)
{
    int pen_x = LIST_LEFT_PAD;
    int pen_y = LIST_TITLE_PAD;
    for (int tab = 0; tab < SETTINGS_TAB_COUNT; tab++) {
        const char *label = tab_label((SettingsTab)tab);
        bool selected = (tab == (int)settings->tab);
        draw_text(settings, label, pen_x, pen_y, color_for_row(selected, false));
        Vector2 measured = MeasureTextEx(settings->font, label, (float)SETTINGS_FONT_SIZE, LIST_LETTER_SPACING);
        if (selected) {
            DrawRectangle(
                pen_x, pen_y + (int)measured.y + 2, (int)measured.x, 2,
                (Color){SETTINGS_TEXT_HIGHLIGHT_R, SETTINGS_TEXT_HIGHLIGHT_G, SETTINGS_TEXT_HIGHLIGHT_B, 255});
        }
        pen_x += (int)measured.x + LIST_LINE_HEIGHT; /* gap between tab labels */
    }
}

static void render_input_tab_body(const SettingsState *settings, const BindingStore *store, Rectangle screen, int row_y)
{
    int last = settings->list_scroll + LIST_VISIBLE_ROWS;
    if (last > LIST_TOTAL_ROWS) {
        last = LIST_TOTAL_ROWS;
    }
    for (int row = settings->list_scroll; row < last; row++) {
        char buf[ROW_BUF_CAP];
        list_row_label(row, buf, sizeof(buf), store);
        bool selected = (row == settings->list_index);
        draw_text(settings, buf, LIST_LEFT_PAD, row_y, color_for_row(selected, false));
        row_y += LIST_LINE_HEIGHT;
    }
    if (settings->toast_text) {
        int inner = (int)screen.width - LIST_LEFT_PAD - LIST_RIGHT_PAD;
        Vector2 measured =
            MeasureTextEx(settings->font, settings->toast_text, (float)SETTINGS_FONT_SIZE, LIST_LETTER_SPACING);
        int toast_x = LIST_LEFT_PAD + ((inner - (int)measured.x) / 2);
        draw_text(settings, settings->toast_text, toast_x, row_y + LIST_LINE_HEIGHT, color_for_row(false, false));
    }
}

static void
render_general_tab_body(const SettingsState *settings, const Preferences *preferences, Rectangle screen, int row_y)
{
    char buf[ROW_BUF_CAP];
    const char *value =
        (preferences != nullptr && preferences->data_dir.ptr != nullptr) ? preferences->data_dir.ptr : "(unknown)";
    (void)snprintf(buf, sizeof(buf), "Data directory:  %s", value);
    bool selected = (settings->general_index == 0);
    draw_text(settings, buf, LIST_LEFT_PAD, row_y, color_for_row(selected, false));
    if (settings->toast_text) {
        int inner = (int)screen.width - LIST_LEFT_PAD - LIST_RIGHT_PAD;
        Vector2 measured =
            MeasureTextEx(settings->font, settings->toast_text, (float)SETTINGS_FONT_SIZE, LIST_LETTER_SPACING);
        int toast_x = LIST_LEFT_PAD + ((inner - (int)measured.x) / 2);
        draw_text(settings, settings->toast_text, toast_x, row_y + (LIST_LINE_HEIGHT * 2), color_for_row(false, false));
    }
}

static void render_list_screen(const SettingsState *settings,
                               const BindingStore *store,
                               const Preferences *preferences,
                               Rectangle screen)
{
    render_tab_header(settings);
    int row_y = LIST_TITLE_PAD + LIST_LINE_HEIGHT + (LIST_LINE_HEIGHT / 2);
    switch (settings->tab) {
    case SETTINGS_TAB_INPUT:
        render_input_tab_body(settings, store, screen, row_y);
        break;
    case SETTINGS_TAB_GENERAL:
        render_general_tab_body(settings, preferences, screen, row_y);
        break;
    case SETTINGS_TAB_COUNT:
        break;
    }
}

static const char *atom_short_name(const AtomicInput *atom)
{
    if (atom->kind == ATOM_KEY) {
        return input_func_key_name(atom->int_a);
    }
    if (atom->kind == ATOM_GP_BUTTON) {
        return input_func_gp_button_name(atom->int_a);
    }
    if (atom->kind == ATOM_GP_AXIS || atom->kind == ATOM_GP_TRIGGER) {
        return input_func_gp_axis_name(atom->int_a);
    }
    return nullptr;
}

static int append_kb_axis_label(char *out, size_t cap, int len, const AtomicInput *atom)
{
    char tmp[ATOM_BUF_CAP];
    const char *neg = atom->int_a > 0 ? input_func_key_name(atom->int_a) : "";
    const char *pos = atom->int_b > 0 ? input_func_key_name(atom->int_b) : "";
    (void)snprintf(tmp, sizeof(tmp), "%s/%s", neg ? neg : "?", pos ? pos : "?");
    return len + snprintf(out + len, cap - (size_t)len, "%s", tmp);
}

static void format_physical_label(const PhysicalInput *physical, char *out, size_t cap)
{
    int len = 0;
    out[0] = '\0';
    for (int part_idx = 0; part_idx < physical->parts.count; part_idx++) {
        const AtomicInput *atom = &physical->parts.data[part_idx];
        if (part_idx > 0) {
            len += snprintf(out + len, cap - (size_t)len, " + ");
        }
        if (atom->kind == ATOM_KB_AXIS) {
            len = append_kb_axis_label(out, cap, len, atom);
            continue;
        }
        const char *name = atom_short_name(atom);
        len += snprintf(out + len, cap - (size_t)len, "%s", name ? name : "?");
    }
}

static const PhysicalInput *detail_alternative_at(const SettingsState *settings, const BindingStore *store, int row)
{
    if (settings->target_kind == SETTINGS_TARGET_ACTION) {
        return &store->actions[settings->target_index].alternatives.data[row];
    }
    return &store->axes[settings->target_index].alternatives.data[row];
}

static void detail_row_label(
    const SettingsState *settings, const BindingStore *store, int row, char *out, size_t cap, bool *out_dim)
{
    int alt_count = detail_alt_count(settings, store);
    if (row < alt_count) {
        const PhysicalInput *physical = detail_alternative_at(settings, store, row);
        char parts[LABEL_BUF_CAP];
        format_physical_label(physical, parts, sizeof(parts));
        (void)snprintf(out, cap, "  %d.  %s", row + 1, parts);
        *out_dim = false;
        return;
    }
    if (row == alt_count + DETAIL_ADD_ROW_OFFSET) {
        (void)snprintf(out, cap, "+ Add new alternative");
    } else {
        (void)snprintf(out, cap, "Reset to defaults");
    }
    *out_dim = true;
}

static void render_detail_toast(const SettingsState *settings, Rectangle screen, int hint_y)
{
    if (!settings->toast_text) {
        return;
    }
    Vector2 measured =
        MeasureTextEx(settings->font, settings->toast_text, (float)SETTINGS_FONT_SIZE, LIST_LETTER_SPACING);
    int width = (int)screen.width;
    int toast_x = LIST_LEFT_PAD + ((width - LIST_LEFT_PAD - LIST_RIGHT_PAD - (int)measured.x) / 2);
    draw_text(settings, settings->toast_text, toast_x, hint_y + LIST_LINE_HEIGHT, color_for_row(false, false));
}

static void render_detail_screen(const SettingsState *settings, const BindingStore *store, Rectangle screen)
{
    char title[LABEL_BUF_CAP];
    (void)snprintf(title, sizeof(title), "Edit: %s", target_label(settings));
    draw_text(settings, title, LIST_LEFT_PAD, LIST_TITLE_PAD, color_for_row(false, false));

    int total = detail_total_rows(settings, store);
    int row_y = LIST_TITLE_PAD + LIST_LINE_HEIGHT + (LIST_LINE_HEIGHT / 2);
    int last = settings->detail_scroll + LIST_VISIBLE_ROWS;
    if (last > total) {
        last = total;
    }
    for (int row = settings->detail_scroll; row < last; row++) {
        char buf[ROW_BUF_CAP];
        bool dim = false;
        detail_row_label(settings, store, row, buf, sizeof(buf), &dim);
        bool selected = (row == settings->detail_index);
        draw_text(settings, buf, LIST_LEFT_PAD, row_y, color_for_row(selected, dim));
        row_y += LIST_LINE_HEIGHT;
    }
    int hint_y = row_y + LIST_LINE_HEIGHT;
    draw_text(settings, "Confirm: rebind   Delete: remove   Cancel: back", LIST_LEFT_PAD, hint_y,
              color_for_row(false, true));
    render_detail_toast(settings, screen, hint_y);
}

static void capture_chord_label(const SettingsState *settings, char *out, size_t cap)
{
    int len = 0;
    for (int index = 0; index < settings->capture_chord_count; index++) {
        const AtomicInput *atom = &settings->capture_chord[index];
        const char *name = nullptr;
        if (atom->kind == ATOM_KEY) {
            name = input_func_key_name(atom->int_a);
        } else if (atom->kind == ATOM_GP_BUTTON) {
            name = input_func_gp_button_name(atom->int_a);
        }
        len += snprintf(out + len, cap - (size_t)len, "%s%s", index > 0 ? " + " : "", name ? name : "?");
    }
    if (len == 0) {
        (void)snprintf(out, cap, "...");
    }
}

static const char *capture_prompt(SettingsCaptureMode mode)
{
    switch (mode) {
    case SETTINGS_CAPTURE_AXIS_FIRST:
        return "Move stick / trigger or press the negative direction";
    case SETTINGS_CAPTURE_AXIS_SECOND:
        return "Now press the positive direction";
    case SETTINGS_CAPTURE_ACTION:
    default:
        return "Press a key, button, or chord";
    }
}

static void render_capture_screen(const SettingsState *settings, Rectangle screen)
{
    int screen_width = (int)screen.width;
    int screen_height = (int)screen.height;
    const char *prompt = capture_prompt(settings->capture_mode);
    Vector2 prompt_measured = MeasureTextEx(settings->font, prompt, (float)SETTINGS_FONT_SIZE, LIST_LETTER_SPACING);
    int prompt_x = (screen_width - (int)prompt_measured.x) / 2;
    int prompt_y = (screen_height / 2) - SETTINGS_FONT_SIZE;
    draw_text(settings, prompt, prompt_x, prompt_y, color_for_row(false, false));

    if (settings->capture_mode == SETTINGS_CAPTURE_ACTION) {
        char chord[ROW_BUF_CAP];
        capture_chord_label(settings, chord, sizeof(chord));
        Vector2 chord_measured = MeasureTextEx(settings->font, chord, (float)SETTINGS_FONT_SIZE, LIST_LETTER_SPACING);
        int chord_x = (screen_width - (int)chord_measured.x) / 2;
        int chord_y = prompt_y + LIST_LINE_HEIGHT + (LIST_LINE_HEIGHT / 2);
        draw_text(settings, chord, chord_x, chord_y,
                  (Color){SETTINGS_TEXT_HIGHLIGHT_R, SETTINGS_TEXT_HIGHLIGHT_G, SETTINGS_TEXT_HIGHLIGHT_B, 255});
    }

    const char *hint = "Esc / Start to cancel";
    Vector2 hint_measured = MeasureTextEx(settings->font, hint, (float)SETTINGS_FONT_SIZE, LIST_LETTER_SPACING);
    int hint_x = (screen_width - (int)hint_measured.x) / 2;
    int hint_y = prompt_y + (LIST_LINE_HEIGHT * 4);
    draw_text(settings, hint, hint_x, hint_y, color_for_row(false, true));
}

static void path_edit_browse_label(const PathEditState *path_edit, int row, char *out, size_t cap)
{
    int file_index = -1;
    PathEditRowKind kind = path_edit_row_kind(path_edit, row, &file_index);
    switch (kind) {
    case PATH_EDIT_KIND_USE_THIS:
        (void)snprintf(out, cap, "<USE THIS DIRECTORY>");
        return;
    case PATH_EDIT_KIND_SELECT_DRIVE:
        (void)snprintf(out, cap, "<SELECT DRIVE>");
        return;
    case PATH_EDIT_KIND_DOTDOT:
        (void)snprintf(out, cap, "../");
        return;
    case PATH_EDIT_KIND_DIR:
        if (file_index >= 0 && path_edit->dir_list_loaded && file_index < (int)path_edit->dir_list.count) {
            const char *name = GetFileName(path_edit->dir_list.paths[file_index]);
            (void)snprintf(out, cap, "%s/", name != nullptr ? name : "");
            return;
        }
        out[0] = '\0';
        return;
    }
    out[0] = '\0';
}

/* Render a centered footer hint by composing per-action labels through
 * input_func_label, which emits gamepad-first / keyboard-second
 * (e.g. "A/Enter"). pairs is an array of {InputAction, verb} ending
 * with action == ACTION_COUNT. */
typedef struct {
    InputAction action;
    const char *verb;
} HintPair;

static void render_path_edit_hints(const SettingsState *settings,
                                   const BindingStore *store,
                                   const HintPair *pairs,
                                   Rectangle screen)
{
    char line[ROW_BUF_CAP];
    int written = 0;
    line[0] = '\0';
    for (const HintPair *pair = pairs; pair->action != ACTION_COUNT; pair++) {
        char binding[LABEL_BUF_CAP];
        (void)input_func_label(store, pair->action, binding, sizeof(binding));
        const char *separator = (written > 0) ? "   " : "";
        int chunk =
            snprintf(line + written, sizeof(line) - (size_t)written, "%s%s: %s", separator, binding, pair->verb);
        if (chunk < 0 || (size_t)written + (size_t)chunk >= sizeof(line)) {
            line[sizeof(line) - 1] = '\0';
            break;
        }
        written += chunk;
    }
    Vector2 measured = MeasureTextEx(settings->font, line, (float)SETTINGS_FONT_SIZE, LIST_LETTER_SPACING);
    int hint_x = LIST_LEFT_PAD + (((int)screen.width - LIST_LEFT_PAD - LIST_RIGHT_PAD - (int)measured.x) / 2);
    int hint_y = (int)screen.height - LIST_LINE_HEIGHT - LIST_TITLE_PAD;
    draw_text(settings, line, hint_x, hint_y, color_for_row(false, true));
}

static void
render_path_edit_browse(const SettingsState *settings, const BindingStore *store, Rectangle screen, int row_y)
{
    const PathEditState *path_edit = &settings->path_edit;
    int total_rows = path_edit_total_rows(path_edit);
    int last = path_edit->browse_scroll + PATH_EDIT_VISIBLE_ROWS;
    if (last > total_rows) {
        last = total_rows;
    }
    if (total_rows == 0) {
        draw_text(settings, "(empty)", LIST_LEFT_PAD, row_y, color_for_row(false, true));
    }
    for (int row = path_edit->browse_scroll; row < last; row++) {
        char label[ROW_BUF_CAP];
        path_edit_browse_label(path_edit, row, label, sizeof(label));
        bool selected = (row == path_edit->browse_index);
        draw_text(settings, label, LIST_LEFT_PAD, row_y, color_for_row(selected, false));
        row_y += LIST_LINE_HEIGHT;
    }
    static const HintPair pairs[] = {
        {ACTION_CONFIRM, "select / enter"},
        {ACTION_CANCEL, "up / exit"},
        {ACTION_WB_KEYBOARD_MODE, "keyboard"},
        {ACTION_COUNT, nullptr},
    };
    render_path_edit_hints(settings, store, pairs, screen);
}

static void
render_path_edit_drive_select(const SettingsState *settings, const BindingStore *store, Rectangle screen, int row_y)
{
    const PathEditState *path_edit = &settings->path_edit;
    static const HintPair pairs[] = {
        {ACTION_CONFIRM, "pick drive"},
        {ACTION_CANCEL, "back"},
        {ACTION_COUNT, nullptr},
    };
    /* Early return on the empty case keeps the loop guarded by a known
     * non-zero drive_count and a non-negative drive_scroll, which the
     * static analyzer cannot otherwise prove (it does not propagate
     * the populate-drives invariants to the renderer). */
    if (path_edit->drive_count <= 0) {
        draw_text(settings, "(no drives)", LIST_LEFT_PAD, row_y, color_for_row(false, true));
        render_path_edit_hints(settings, store, pairs, screen);
        return;
    }
    int scroll = path_edit->drive_scroll < 0 ? 0 : path_edit->drive_scroll;
    int last = scroll + PATH_EDIT_VISIBLE_ROWS;
    if (last > path_edit->drive_count) {
        last = path_edit->drive_count;
    }
    for (int row = scroll; row < last; row++) {
        bool selected = (row == path_edit->drive_index);
        draw_text(settings, path_edit->drives[row], LIST_LEFT_PAD, row_y, color_for_row(selected, false));
        row_y += LIST_LINE_HEIGHT;
    }
    render_path_edit_hints(settings, store, pairs, screen);
}

static void render_path_edit_screen(
    const SettingsState *settings, const BindingStore *store, Rectangle screen, int screen_width, int screen_height)
{
    /* Top: title + current path. */
    draw_text(settings, "Edit data directory:", LIST_LEFT_PAD, LIST_TITLE_PAD, color_for_row(false, false));
    char buffer_line[ROW_BUF_CAP];
    (void)snprintf(buffer_line, sizeof(buffer_line), "> %s", settings->path_edit.buf);
    draw_text(settings, buffer_line, LIST_LEFT_PAD, LIST_TITLE_PAD + LIST_LINE_HEIGHT,
              (Color){SETTINGS_TEXT_HIGHLIGHT_R, SETTINGS_TEXT_HIGHLIGHT_G, SETTINGS_TEXT_HIGHLIGHT_B, 255});

    int body_y = LIST_TITLE_PAD + (LIST_LINE_HEIGHT * 3);
    switch (settings->path_edit.mode) {
    case PATH_EDIT_BROWSE:
        render_path_edit_browse(settings, store, screen, body_y);
        break;
    case PATH_EDIT_DRIVE_SELECT:
        render_path_edit_drive_select(settings, store, screen, body_y);
        break;
    case PATH_EDIT_KEYBOARD: {
        keyboard_widget_draw(&settings->path_edit.kb, (KbScreenSize){screen_width, screen_height}, settings->font);
        static const HintPair pairs[] = {
            {ACTION_CONFIRM, "pick"}, {ACTION_KEYBOARD_BACKSPACE, "backspace"},
            {ACTION_CANCEL, "back"},  {ACTION_WB_KEYBOARD_MODE, "browse mode"},
            {ACTION_COUNT, nullptr},
        };
        render_path_edit_hints(settings, store, pairs, screen);
        break;
    }
    }
}

void settings_render(const SettingsState *settings,
                     const BindingStore *store,
                     const Preferences *preferences,
                     const BlurPipeline *blur,
                     int screen_width,
                     int screen_height)
{
    if (!settings->open) {
        return;
    }
    Rectangle screen = (Rectangle){0, 0, (float)screen_width, (float)screen_height};
    blur_draw(blur, screen);
    DrawRectangle(0, 0, screen_width, screen_height, (Color){0, 0, 0, VIGNETTE_ALPHA});

    if (!settings->font_loaded) {
        return;
    }
    switch (settings->screen) {
    case SETTINGS_SCREEN_LIST:
        render_list_screen(settings, store, preferences, screen);
        break;
    case SETTINGS_SCREEN_DETAIL:
        render_detail_screen(settings, store, screen);
        break;
    case SETTINGS_SCREEN_CAPTURE:
        render_capture_screen(settings, screen);
        break;
    case SETTINGS_SCREEN_PATH_EDIT:
        render_path_edit_screen(settings, store, screen, screen_width, screen_height);
        break;
    }
}
