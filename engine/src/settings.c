#include "settings.h"

#include "alloc.h"
#include "blur.h"
#include "input.h"
#include "input_func.h"
#include "raylib.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

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
    if (settings->font_loaded) {
        UnloadFont(settings->font);
    }
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
    if (settings->font_loaded) {
        UnloadFont(settings->font);
    }
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
        return input_func_action_toml_name((InputAction)settings->target_index);
    }
    if (settings->target_kind == SETTINGS_TARGET_AXIS) {
        return input_func_axis_toml_name((InputAxis)settings->target_index);
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

static void handle_list_input(
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
    if (settings->list_index < ACTION_ROW_COUNT) {
        enter_detail_action(settings, (InputAction)settings->list_index);
    } else if (settings->list_index < ACTION_ROW_COUNT + AXIS_ROW_COUNT) {
        enter_detail_axis(settings, (InputAxis)(settings->list_index - ACTION_ROW_COUNT));
    } else {
        input_func_reset_all_to_defaults(store, alloc);
        settings->save_requested = true;
        show_toast(settings, "All bindings reset");
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

void settings_handle_input(
    SettingsState *settings, const InputState *input, BindingStore *store, Allocator alloc, bool *close_requested)
{
    switch (settings->screen) {
    case SETTINGS_SCREEN_LIST:
        handle_list_input(settings, input, store, alloc, close_requested);
        break;
    case SETTINGS_SCREEN_DETAIL:
        handle_detail_input(settings, input, store, alloc);
        break;
    case SETTINGS_SCREEN_CAPTURE:
        handle_capture_input(settings, input, store, alloc);
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
    if (row < ACTION_ROW_COUNT) {
        const char *name = input_func_action_toml_name((InputAction)row);
        char binding[LABEL_BUF_CAP] = {0};
        (void)input_func_label(store, (InputAction)row, binding, sizeof(binding));
        (void)snprintf(out, cap, "%-26s %s", name ? name : "", binding);
        return;
    }
    if (row < ACTION_ROW_COUNT + AXIS_ROW_COUNT) {
        int axis = row - ACTION_ROW_COUNT;
        const char *name = input_func_axis_toml_name((InputAxis)axis);
        (void)snprintf(out, cap, "%s", name ? name : "");
        return;
    }
    (void)snprintf(out, cap, "Reset all to defaults");
}

static void render_list_screen(const SettingsState *settings, const BindingStore *store, Rectangle screen)
{
    draw_text(settings, "Settings: keybindings", LIST_LEFT_PAD, LIST_TITLE_PAD, color_for_row(false, false));
    int row_y = LIST_TITLE_PAD + LIST_LINE_HEIGHT + (LIST_LINE_HEIGHT / 2);
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

void settings_render(const SettingsState *settings,
                     const BindingStore *store,
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
        render_list_screen(settings, store, screen);
        break;
    case SETTINGS_SCREEN_DETAIL:
        render_detail_screen(settings, store, screen);
        break;
    case SETTINGS_SCREEN_CAPTURE:
        render_capture_screen(settings, screen);
        break;
    }
}
