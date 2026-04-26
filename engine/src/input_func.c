#include "input_func.h"

#include "alloc.h"
#include "error.h"
#include "input.h"
#include "raylib.h"
#include "vec.h"

#include <math.h>
#include <string.h>

VEC_IMPL(atomic_input, AtomicInput)
VEC_IMPL(physical_input, PhysicalInput)

#define LABEL_BUF_CAP 64
#define GP_AXIS_DEADZONE 0.15F

/* --- Code <-> name translation ------------------------------------------- */

typedef struct {
    int code;
    const char *toml_name;   /* e.g. "LEFT_CONTROL", "RIGHT_FACE_DOWN" */
    const char *short_label; /* e.g. "Ctrl", "A" — for HUD rendering */
} CodeName;

typedef struct {
    const CodeName *entries;
    size_t count;
} CodeNameTable;

#define MAKE_TABLE(arr) ((CodeNameTable){.entries = (arr), .count = sizeof(arr) / sizeof((arr)[0])})

/* Keyboard. Includes every key currently bound in the engine plus a few
 * obvious extras (text alphabetics). Lookups against this table are
 * case-sensitive on toml_name. */
static const CodeName keyboard_codes[] = {
    {KEY_ENTER, "ENTER", "Ent"},
    {KEY_ESCAPE, "ESCAPE", "Esc"},
    {KEY_TAB, "TAB", "Tab"},
    {KEY_SPACE, "SPACE", "Spc"},
    {KEY_DELETE, "DELETE", "Del"},
    {KEY_BACKSPACE, "BACKSPACE", "Bksp"},
    {KEY_UP, "UP", "Up"},
    {KEY_DOWN, "DOWN", "Dn"},
    {KEY_LEFT, "LEFT", "Left"},
    {KEY_RIGHT, "RIGHT", "Right"},
    {KEY_PAGE_UP, "PAGE_UP", "PgUp"},
    {KEY_PAGE_DOWN, "PAGE_DOWN", "PgDn"},
    {KEY_LEFT_SHIFT, "LEFT_SHIFT", "Shift"},
    {KEY_LEFT_CONTROL, "LEFT_CONTROL", "Ctrl"},
    {KEY_LEFT_ALT, "LEFT_ALT", "Alt"},
    {KEY_LEFT_BRACKET, "LEFT_BRACKET", "["},
    {KEY_RIGHT_BRACKET, "RIGHT_BRACKET", "]"},
    {KEY_F1, "F1", "F1"},
    {KEY_F2, "F2", "F2"},
    {KEY_F3, "F3", "F3"},
    {KEY_F4, "F4", "F4"},
    {KEY_F5, "F5", "F5"},
    {KEY_F6, "F6", "F6"},
    {KEY_F7, "F7", "F7"},
    {KEY_F8, "F8", "F8"},
    {KEY_F9, "F9", "F9"},
    {KEY_F10, "F10", "F10"},
    {KEY_A, "A", "A"},
    {KEY_B, "B", "B"},
    {KEY_C, "C", "C"},
    {KEY_D, "D", "D"},
    {KEY_E, "E", "E"},
    {KEY_G, "G", "G"},
    {KEY_H, "H", "H"},
    {KEY_P, "P", "P"},
    {KEY_Q, "Q", "Q"},
    {KEY_S, "S", "S"},
    {KEY_W, "W", "W"},
    {KEY_X, "X", "X"},
    {KEY_Y, "Y", "Y"},
    {KEY_Z, "Z", "Z"},
};

static const CodeName gp_button_codes[] = {
    {GAMEPAD_BUTTON_RIGHT_FACE_DOWN, "RIGHT_FACE_DOWN", "A"},
    {GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, "RIGHT_FACE_RIGHT", "B"},
    {GAMEPAD_BUTTON_RIGHT_FACE_LEFT, "RIGHT_FACE_LEFT", "X"},
    {GAMEPAD_BUTTON_RIGHT_FACE_UP, "RIGHT_FACE_UP", "Y"},
    {GAMEPAD_BUTTON_LEFT_FACE_DOWN, "LEFT_FACE_DOWN", "Dn"},
    {GAMEPAD_BUTTON_LEFT_FACE_UP, "LEFT_FACE_UP", "Up"},
    {GAMEPAD_BUTTON_LEFT_FACE_LEFT, "LEFT_FACE_LEFT", "Left"},
    {GAMEPAD_BUTTON_LEFT_FACE_RIGHT, "LEFT_FACE_RIGHT", "Right"},
    {GAMEPAD_BUTTON_LEFT_TRIGGER_1, "LEFT_TRIGGER_1", "L1"},
    {GAMEPAD_BUTTON_LEFT_TRIGGER_2, "LEFT_TRIGGER_2", "L2"},
    {GAMEPAD_BUTTON_RIGHT_TRIGGER_1, "RIGHT_TRIGGER_1", "R1"},
    {GAMEPAD_BUTTON_RIGHT_TRIGGER_2, "RIGHT_TRIGGER_2", "R2"},
    {GAMEPAD_BUTTON_LEFT_THUMB, "LEFT_THUMB", "L3"},
    {GAMEPAD_BUTTON_RIGHT_THUMB, "RIGHT_THUMB", "R3"},
    {GAMEPAD_BUTTON_MIDDLE_LEFT, "MIDDLE_LEFT", "Sel"},
    {GAMEPAD_BUTTON_MIDDLE_RIGHT, "MIDDLE_RIGHT", "Start"},
};

static const CodeName gp_axis_codes[] = {
    {GAMEPAD_AXIS_LEFT_X, "LEFT_X", "LX"},
    {GAMEPAD_AXIS_LEFT_Y, "LEFT_Y", "LY"},
    {GAMEPAD_AXIS_RIGHT_X, "RIGHT_X", "RX"},
    {GAMEPAD_AXIS_RIGHT_Y, "RIGHT_Y", "RY"},
    {GAMEPAD_AXIS_LEFT_TRIGGER, "LEFT_TRIGGER", "LT"},
    {GAMEPAD_AXIS_RIGHT_TRIGGER, "RIGHT_TRIGGER", "RT"},
};

static int code_from_name(CodeNameTable table, const char *name)
{
    if (!name) {
        return -1;
    }
    for (size_t index = 0; index < table.count; index++) {
        if (strcmp(table.entries[index].toml_name, name) == 0) {
            return table.entries[index].code;
        }
    }
    return -1;
}

static const char *label_from_code(CodeNameTable table, int code)
{
    for (size_t index = 0; index < table.count; index++) {
        if (table.entries[index].code == code) {
            return table.entries[index].short_label;
        }
    }
    return "?";
}

int input_func_key_from_name(const char *name)
{
    return code_from_name(MAKE_TABLE(keyboard_codes), name);
}

int input_func_gp_button_from_name(const char *name)
{
    return code_from_name(MAKE_TABLE(gp_button_codes), name);
}

int input_func_gp_axis_from_name(const char *name)
{
    return code_from_name(MAKE_TABLE(gp_axis_codes), name);
}

const char *input_func_key_label(int key)
{
    return label_from_code(MAKE_TABLE(keyboard_codes), key);
}

const char *input_func_gp_button_label(int button)
{
    return label_from_code(MAKE_TABLE(gp_button_codes), button);
}

/* --- InputState bit accessors -------------------------------------------- */

static bool key_is_down(const InputState *input, int key)
{
    if (key <= 0 || key >= INPUT_MAX_KEY_CODE) {
        return false;
    }
    return (input->key_down[key / INPUT_BITS_PER_WORD] & (1ULL << (key % INPUT_BITS_PER_WORD))) != 0;
}

static bool key_is_pressed(const InputState *input, int key)
{
    if (key <= 0 || key >= INPUT_MAX_KEY_CODE) {
        return false;
    }
    return (input->key_pressed[key / INPUT_BITS_PER_WORD] & (1ULL << (key % INPUT_BITS_PER_WORD))) != 0;
}

static bool gp_button_is_down(const InputState *input, int button)
{
    if (button <= 0 || button >= INPUT_GP_BUTTON_COUNT) {
        return false;
    }
    return (input->gp_button_down & (1U << button)) != 0;
}

static bool gp_button_is_pressed(const InputState *input, int button)
{
    if (button <= 0 || button >= INPUT_GP_BUTTON_COUNT) {
        return false;
    }
    return (input->gp_button_pressed & (1U << button)) != 0;
}

static float gp_axis_value(const InputState *input, int axis)
{
    if (axis < 0 || axis >= INPUT_GP_AXIS_COUNT) {
        return 0.0F;
    }
    return input->gp_axis[axis];
}

/* --- Atom evaluation ----------------------------------------------------- */

/* True while the atom is currently active. For chord/binding evaluation. */
static bool atom_held(const InputState *input, const AtomicInput *atom)
{
    switch (atom->kind) {
    case ATOM_KEY:
        return key_is_down(input, atom->int_a);
    case ATOM_GP_BUTTON:
        return gp_button_is_down(input, atom->int_a);
    case ATOM_GP_AXIS:
        /* Treat as "active" when magnitude > deadzone. Atoms in chords
         * generally aren't axis atoms, but defining "held" lets the
         * machinery be uniform. */
        return fabsf(gp_axis_value(input, atom->int_a)) > GP_AXIS_DEADZONE;
    case ATOM_GP_TRIGGER:
        return gp_axis_value(input, atom->int_a) > 0.0F;
    case ATOM_KB_AXIS:
        return key_is_down(input, atom->int_a) || key_is_down(input, atom->int_b);
    }
    return false;
}

/* True on the frame the atom transitioned from inactive to active. Only
 * keys and gamepad buttons have meaningful edges; axis atoms always
 * report false (use input_held for hold semantics). */
static bool atom_pressed(const InputState *input, const AtomicInput *atom)
{
    switch (atom->kind) {
    case ATOM_KEY:
        return key_is_pressed(input, atom->int_a);
    case ATOM_GP_BUTTON:
        return gp_button_is_pressed(input, atom->int_a);
    case ATOM_KB_AXIS:
        return key_is_pressed(input, atom->int_a) || key_is_pressed(input, atom->int_b);
    case ATOM_GP_AXIS:
    case ATOM_GP_TRIGGER:
        return false;
    }
    return false;
}

/* Signed contribution to an axis read. Non-axis atoms contribute 0. */
static float atom_axis_value(const InputState *input, const AtomicInput *atom)
{
    switch (atom->kind) {
    case ATOM_KB_AXIS: {
        float value = 0.0F;
        if (atom->int_a > 0 && key_is_down(input, atom->int_a)) {
            value -= 1.0F;
        }
        if (atom->int_b > 0 && key_is_down(input, atom->int_b)) {
            value += 1.0F;
        }
        return value * atom->scale;
    }
    case ATOM_GP_AXIS:
        return gp_axis_value(input, atom->int_a) * atom->scale;
    case ATOM_GP_TRIGGER: {
        /* raylib reports trigger axes as -1..+1 (rest at -1). Normalize
         * to 0..1 to match the existing input.c behavior. */
        float raw = gp_axis_value(input, atom->int_a);
        return ((raw + 1.0F) * 0.5F) * atom->scale;
    }
    case ATOM_KEY:
    case ATOM_GP_BUTTON:
        return 0.0F;
    }
    return 0.0F;
}

/* --- PhysicalInput evaluation -------------------------------------------- */

static bool physical_held(const InputState *input, const PhysicalInput *physical)
{
    if (physical->parts.count == 0) {
        return false;
    }
    for (int index = 0; index < physical->parts.count; index++) {
        if (!atom_held(input, &physical->parts.data[index])) {
            return false;
        }
    }
    return true;
}

static bool physical_pressed(const InputState *input, const PhysicalInput *physical)
{
    if (!physical_held(input, physical)) {
        return false;
    }
    /* At least one part newly pressed this frame. For a single-atom
     * binding, that's the atom itself. For chords, any one part fresh is
     * enough — the user might press Ctrl first then Z, or Z first then
     * Ctrl; the chord fires on the second key. */
    for (int index = 0; index < physical->parts.count; index++) {
        if (atom_pressed(input, &physical->parts.data[index])) {
            return true;
        }
    }
    return false;
}

/* --- Public evaluation API ------------------------------------------------ */

bool input_pressed(const InputState *input, const BindingStore *store, InputAction action)
{
    if (action < 0 || action >= ACTION_COUNT) {
        return false;
    }
    const ActionBinding *binding = &store->actions[action];
    for (int index = 0; index < binding->alternatives.count; index++) {
        if (physical_pressed(input, &binding->alternatives.data[index])) {
            return true;
        }
    }
    return false;
}

bool input_held(const InputState *input, const BindingStore *store, InputAction action)
{
    if (action < 0 || action >= ACTION_COUNT) {
        return false;
    }
    const ActionBinding *binding = &store->actions[action];
    for (int index = 0; index < binding->alternatives.count; index++) {
        if (physical_held(input, &binding->alternatives.data[index])) {
            return true;
        }
    }
    return false;
}

float input_axis(const InputState *input, const BindingStore *store, InputAxis axis)
{
    if (axis < 0 || axis >= AXIS_COUNT) {
        return 0.0F;
    }
    const AxisBinding *binding = &store->axes[axis];
    float total = 0.0F;
    for (int alt_idx = 0; alt_idx < binding->alternatives.count; alt_idx++) {
        const PhysicalInput *physical = &binding->alternatives.data[alt_idx];
        for (int part_idx = 0; part_idx < physical->parts.count; part_idx++) {
            total += atom_axis_value(input, &physical->parts.data[part_idx]);
        }
    }
    if (total > 1.0F) {
        total = 1.0F;
    }
    if (total < -1.0F) {
        total = -1.0F;
    }
    return total;
}

Vector2 input_axis_pair(const InputState *input, const BindingStore *store, InputAxis x_axis, InputAxis y_axis)
{
    Vector2 raw = {input_axis(input, store, x_axis), input_axis(input, store, y_axis)};
    /* Apply unit-disc clamp via the shared deadzone helper. deadzone=0
     * preserves the existing "diagonals not faster than cardinals" guarantee
     * for both digital (keyboard) and post-axis-deadzone analog input. */
    return input_apply_deadzone(raw, 0.0F);
}

/* --- Label rendering ----------------------------------------------------- */

static int append_str(char *out, size_t cap, int len, const char *text)
{
    if ((size_t)len >= cap - 1) {
        out[cap - 1] = '\0';
        return len;
    }
    size_t remain = cap - 1 - (size_t)len;
    size_t text_len = strlen(text);
    if (text_len > remain) {
        text_len = remain;
    }
    memcpy(out + len, text, text_len);
    int new_len = len + (int)text_len;
    out[new_len] = '\0';
    return new_len;
}

static int append_atom_label(char *out, size_t cap, int len, const AtomicInput *atom)
{
    switch (atom->kind) {
    case ATOM_KEY:
        return append_str(out, cap, len, input_func_key_label(atom->int_a));
    case ATOM_GP_BUTTON:
        return append_str(out, cap, len, input_func_gp_button_label(atom->int_a));
    case ATOM_GP_AXIS:
    case ATOM_GP_TRIGGER:
        return append_str(out, cap, len, label_from_code(MAKE_TABLE(gp_axis_codes), atom->int_a));
    case ATOM_KB_AXIS: {
        /* Render as "neg/pos" or just "pos" if no neg; intended for
         * hint bars where axis bindings are rare. */
        int new_len = len;
        if (atom->int_a > 0) {
            new_len = append_str(out, cap, new_len, input_func_key_label(atom->int_a));
            new_len = append_str(out, cap, new_len, "/");
        }
        if (atom->int_b > 0) {
            new_len = append_str(out, cap, new_len, input_func_key_label(atom->int_b));
        }
        return new_len;
    }
    }
    return len;
}

/* True when every atom in the physical input is keyboard (KEY or KB_AXIS). */
static bool physical_is_keyboard(const PhysicalInput *physical)
{
    for (int index = 0; index < physical->parts.count; index++) {
        AtomKind kind = physical->parts.data[index].kind;
        if (kind != ATOM_KEY && kind != ATOM_KB_AXIS) {
            return false;
        }
    }
    return true;
}

static int append_physical_label(char *out, size_t cap, int len, const PhysicalInput *physical)
{
    for (int index = 0; index < physical->parts.count; index++) {
        if (index > 0) {
            len = append_str(out, cap, len, "+");
        }
        len = append_atom_label(out, cap, len, &physical->parts.data[index]);
    }
    return len;
}

int input_func_label(const BindingStore *store, InputAction action, char *out, size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (action < 0 || action >= ACTION_COUNT) {
        return 0;
    }
    const ActionBinding *binding = &store->actions[action];

    /* Render gamepad alternatives first (project is gamepad-first), then
     * keyboard. Within each group, alternatives are joined with "/"; the
     * groups themselves are also joined with "/". */
    int len = 0;
    bool wrote_any = false;
    for (int pass = 0; pass < 2; pass++) {
        bool want_keyboard = (pass == 1);
        for (int index = 0; index < binding->alternatives.count; index++) {
            const PhysicalInput *physical = &binding->alternatives.data[index];
            if (physical_is_keyboard(physical) != want_keyboard) {
                continue;
            }
            if (wrote_any) {
                len = append_str(out, cap, len, "/");
            }
            len = append_physical_label(out, cap, len, physical);
            wrote_any = true;
        }
    }
    if ((size_t)len < cap) {
        out[len] = '\0';
    } else {
        out[cap - 1] = '\0';
        len = (int)cap - 1;
    }
    return len;
}

/* --- Defaults builder ---------------------------------------------------- */

/* The defaults table mirrors the bindings present in the codebase before
 * the function-layer overhaul. Reading order: action, then a sequence of
 * DefaultAtom entries. ATOM_END terminates the current PhysicalInput;
 * ALTS_END terminates the whole list. Sentinel-ness is carried in
 * dedicated booleans rather than out-of-range AtomKind casts. */

typedef struct {
    bool is_atom_end;
    bool is_alts_end;
    AtomicInput atom;
} DefaultAtom;

#define ATOM_END ((DefaultAtom){.is_atom_end = true})
#define ALTS_END ((DefaultAtom){.is_alts_end = true})
#define K(key) ((DefaultAtom){.atom = {.kind = ATOM_KEY, .int_a = (key), .scale = 1.0F}})
#define GB(button) ((DefaultAtom){.atom = {.kind = ATOM_GP_BUTTON, .int_a = (button), .scale = 1.0F}})
#define GA(axis) ((DefaultAtom){.atom = {.kind = ATOM_GP_AXIS, .int_a = (axis), .scale = 1.0F}})
#define GT(axis) ((DefaultAtom){.atom = {.kind = ATOM_GP_TRIGGER, .int_a = (axis), .scale = 1.0F}})
#define KBA(neg, pos) ((DefaultAtom){.atom = {.kind = ATOM_KB_AXIS, .int_a = (neg), .int_b = (pos), .scale = 1.0F}})

/* Push one PhysicalInput (parsed up to ATOM_END) into the binding's
 * alternatives. Advances `cursor` past the ATOM_END. Returns false on
 * vec push failure. */
static bool push_physical(vec_physical_input *alternatives, Allocator alloc, const DefaultAtom **cursor)
{
    PhysicalInput physical = {.parts = vec_atomic_input_new(alloc)};
    while (!(*cursor)->is_atom_end && !(*cursor)->is_alts_end) {
        if (!vec_atomic_input_push(&physical.parts, (*cursor)->atom)) {
            return false;
        }
        (*cursor)++;
    }
    if ((*cursor)->is_atom_end) {
        (*cursor)++; /* skip ATOM_END */
    }
    return vec_physical_input_push(alternatives, physical);
}

static bool build_action(ActionBinding *binding, Allocator alloc, const DefaultAtom *atoms)
{
    binding->alternatives = vec_physical_input_new(alloc);
    const DefaultAtom *cursor = atoms;
    while (!cursor->is_alts_end) {
        if (!push_physical(&binding->alternatives, alloc, &cursor)) {
            return false;
        }
    }
    return true;
}

static bool build_axis(AxisBinding *binding, Allocator alloc, const DefaultAtom *atoms)
{
    binding->alternatives = vec_physical_input_new(alloc);
    const DefaultAtom *cursor = atoms;
    while (!cursor->is_alts_end) {
        if (!push_physical(&binding->alternatives, alloc, &cursor)) {
            return false;
        }
    }
    return true;
}

void input_func_load_defaults(BindingStore *store, Allocator alloc)
{
    /* Each block: atoms separated by ATOM_END; whole list terminated by ALTS_END.
     * Atoms within a single PhysicalInput are AND-combined (chord);
     * separate PhysicalInputs are OR-combined (alternatives). */

    /* clang-format off */
    static const DefaultAtom confirm[]   = { K(KEY_ENTER),  ATOM_END,  GB(GAMEPAD_BUTTON_RIGHT_FACE_DOWN),  ATOM_END, ALTS_END };
    static const DefaultAtom cancel[]    = { K(KEY_ESCAPE), ATOM_END,  GB(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT), ATOM_END, ALTS_END };
    static const DefaultAtom nav_up[]    = { K(KEY_UP),     ATOM_END,  GB(GAMEPAD_BUTTON_LEFT_FACE_UP),     ATOM_END, ALTS_END };
    static const DefaultAtom nav_down[]  = { K(KEY_DOWN),   ATOM_END,  GB(GAMEPAD_BUTTON_LEFT_FACE_DOWN),   ATOM_END, ALTS_END };
    static const DefaultAtom nav_left[]  = { K(KEY_LEFT),   ATOM_END,  GB(GAMEPAD_BUTTON_LEFT_FACE_LEFT),   ATOM_END, ALTS_END };
    static const DefaultAtom nav_right[] = { K(KEY_RIGHT),  ATOM_END,  GB(GAMEPAD_BUTTON_LEFT_FACE_RIGHT),  ATOM_END, ALTS_END };
    static const DefaultAtom page_up[]   = { K(KEY_Q),      ATOM_END,  GB(GAMEPAD_BUTTON_LEFT_TRIGGER_1),   ATOM_END, ALTS_END };
    static const DefaultAtom page_down[] = { K(KEY_E),      ATOM_END,  GB(GAMEPAD_BUTTON_RIGHT_TRIGGER_1),  ATOM_END, ALTS_END };

    static const DefaultAtom editor_toggle[]          = { K(KEY_F5),  ATOM_END, GB(GAMEPAD_BUTTON_MIDDLE_RIGHT), ATOM_END, ALTS_END };
    static const DefaultAtom editor_open_blueprints[] = { ALTS_END }; /* opened via tools radial; no direct binding currently */
    static const DefaultAtom editor_open_tools[]      = { K(KEY_TAB), ATOM_END, GB(GAMEPAD_BUTTON_RIGHT_FACE_UP), ATOM_END, ALTS_END };
    static const DefaultAtom editor_delete[]          = { K(KEY_DELETE), ATOM_END, GB(GAMEPAD_BUTTON_RIGHT_FACE_LEFT), ATOM_END, ALTS_END };
    static const DefaultAtom editor_place[]           = { K(KEY_P),   ATOM_END, GB(GAMEPAD_BUTTON_RIGHT_TRIGGER_1), ATOM_END, ALTS_END };
    static const DefaultAtom editor_type_props[]      = { K(KEY_RIGHT_BRACKET), ATOM_END, GB(GAMEPAD_BUTTON_RIGHT_TRIGGER_2), ATOM_END, ALTS_END };
    static const DefaultAtom editor_grab[]            = { K(KEY_G),   ATOM_END, GB(GAMEPAD_BUTTON_LEFT_THUMB),    ATOM_END, ALTS_END };
    static const DefaultAtom editor_handles[]         = { K(KEY_H),   ATOM_END, ALTS_END }; /* keyboard-only by design (L1 reserved for chord) */
    static const DefaultAtom editor_watch[]           = { K(KEY_LEFT_SHIFT), ATOM_END, GB(GAMEPAD_BUTTON_LEFT_TRIGGER_2), ATOM_END, ALTS_END };
    static const DefaultAtom editor_undo[] = {
        K(KEY_LEFT_CONTROL), K(KEY_Z), ATOM_END,
        GB(GAMEPAD_BUTTON_LEFT_TRIGGER_1), GB(GAMEPAD_BUTTON_LEFT_FACE_LEFT), ATOM_END,
        ALTS_END,
    };
    static const DefaultAtom editor_redo[] = {
        K(KEY_LEFT_CONTROL), K(KEY_Y), ATOM_END,
        GB(GAMEPAD_BUTTON_LEFT_TRIGGER_1), GB(GAMEPAD_BUTTON_LEFT_FACE_RIGHT), ATOM_END,
        ALTS_END,
    };

    static const DefaultAtom attr_dec_1[]   = { K(KEY_LEFT),  ATOM_END, GB(GAMEPAD_BUTTON_LEFT_FACE_LEFT),   ATOM_END, ALTS_END };
    static const DefaultAtom attr_inc_1[]   = { K(KEY_RIGHT), ATOM_END, GB(GAMEPAD_BUTTON_LEFT_FACE_RIGHT),  ATOM_END, ALTS_END };
    static const DefaultAtom attr_dec_10[]  = { K(KEY_LEFT_BRACKET),  ATOM_END, GB(GAMEPAD_BUTTON_LEFT_TRIGGER_1),  ATOM_END, ALTS_END };
    static const DefaultAtom attr_inc_10[]  = { K(KEY_RIGHT_BRACKET), ATOM_END, GB(GAMEPAD_BUTTON_RIGHT_TRIGGER_1), ATOM_END, ALTS_END };
    static const DefaultAtom attr_dec_100[] = { K(KEY_PAGE_DOWN), ATOM_END, GB(GAMEPAD_BUTTON_LEFT_TRIGGER_2),  ATOM_END, ALTS_END };
    static const DefaultAtom attr_inc_100[] = { K(KEY_PAGE_UP),   ATOM_END, GB(GAMEPAD_BUTTON_RIGHT_TRIGGER_2), ATOM_END, ALTS_END };

    static const DefaultAtom blueprint_duplicate[] = { K(KEY_LEFT_BRACKET), ATOM_END, GB(GAMEPAD_BUTTON_LEFT_TRIGGER_1), ATOM_END, ALTS_END };
    static const DefaultAtom blueprint_remove[]    = { K(KEY_DELETE), ATOM_END, GB(GAMEPAD_BUTTON_RIGHT_FACE_LEFT), ATOM_END, ALTS_END };

    static const DefaultAtom wb_keyboard_mode[] = { K(KEY_DELETE), ATOM_END, GB(GAMEPAD_BUTTON_RIGHT_FACE_LEFT), ATOM_END, ALTS_END };
    static const DefaultAtom wb_append[]        = { K(KEY_ENTER),  ATOM_END, GB(GAMEPAD_BUTTON_RIGHT_FACE_DOWN), ATOM_END, ALTS_END };
    static const DefaultAtom wb_pop[]           = { K(KEY_ESCAPE), ATOM_END, GB(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT), ATOM_END, ALTS_END };

    static const DefaultAtom interact[]            = { K(KEY_SPACE), ATOM_END, GB(GAMEPAD_BUTTON_RIGHT_FACE_DOWN), ATOM_END, ALTS_END };

    static const DefaultAtom menu_toggle[]         = { K(KEY_F3), ATOM_END, GB(GAMEPAD_BUTTON_MIDDLE_LEFT), ATOM_END, ALTS_END };
    static const DefaultAtom font_preview_toggle[] = { K(KEY_F4), ATOM_END, GB(GAMEPAD_BUTTON_RIGHT_THUMB), ATOM_END, ALTS_END };
    static const DefaultAtom quit[] = {
        K(KEY_LEFT_CONTROL), K(KEY_Q), ATOM_END,
        GB(GAMEPAD_BUTTON_MIDDLE_LEFT), GB(GAMEPAD_BUTTON_MIDDLE_RIGHT), ATOM_END,
        ALTS_END,
    };

    static const DefaultAtom axis_primary_x[] = {
        KBA(KEY_LEFT, KEY_RIGHT), ATOM_END,
        KBA(KEY_A,    KEY_D),     ATOM_END,
        GA(GAMEPAD_AXIS_LEFT_X),  ATOM_END,
        ALTS_END,
    };
    static const DefaultAtom axis_primary_y[] = {
        KBA(KEY_UP, KEY_DOWN),    ATOM_END,
        KBA(KEY_W,  KEY_S),       ATOM_END,
        GA(GAMEPAD_AXIS_LEFT_Y),  ATOM_END,
        ALTS_END,
    };
    static const DefaultAtom axis_secondary_x[] = {
        KBA(KEY_Q, KEY_E),         ATOM_END,
        GA(GAMEPAD_AXIS_RIGHT_X),  ATOM_END,
        ALTS_END,
    };
    static const DefaultAtom axis_secondary_y[] = {
        GA(GAMEPAD_AXIS_RIGHT_Y), ATOM_END,
        ALTS_END,
    };
    static const DefaultAtom axis_trigger_left[] = {
        KBA(0, KEY_Z),                  ATOM_END,
        GT(GAMEPAD_AXIS_LEFT_TRIGGER),  ATOM_END,
        ALTS_END,
    };
    static const DefaultAtom axis_trigger_right[] = {
        KBA(0, KEY_X),                   ATOM_END,
        GT(GAMEPAD_AXIS_RIGHT_TRIGGER),  ATOM_END,
        ALTS_END,
    };
    /* clang-format on */

    (void)build_action(&store->actions[ACTION_CONFIRM], alloc, confirm);
    (void)build_action(&store->actions[ACTION_CANCEL], alloc, cancel);
    (void)build_action(&store->actions[ACTION_NAV_UP], alloc, nav_up);
    (void)build_action(&store->actions[ACTION_NAV_DOWN], alloc, nav_down);
    (void)build_action(&store->actions[ACTION_NAV_LEFT], alloc, nav_left);
    (void)build_action(&store->actions[ACTION_NAV_RIGHT], alloc, nav_right);
    (void)build_action(&store->actions[ACTION_PAGE_UP], alloc, page_up);
    (void)build_action(&store->actions[ACTION_PAGE_DOWN], alloc, page_down);

    (void)build_action(&store->actions[ACTION_EDITOR_TOGGLE], alloc, editor_toggle);
    (void)build_action(&store->actions[ACTION_EDITOR_OPEN_BLUEPRINTS], alloc, editor_open_blueprints);
    (void)build_action(&store->actions[ACTION_EDITOR_OPEN_TOOLS], alloc, editor_open_tools);
    (void)build_action(&store->actions[ACTION_EDITOR_DELETE], alloc, editor_delete);
    (void)build_action(&store->actions[ACTION_EDITOR_PLACE], alloc, editor_place);
    (void)build_action(&store->actions[ACTION_EDITOR_TYPE_PROPS], alloc, editor_type_props);
    (void)build_action(&store->actions[ACTION_EDITOR_GRAB], alloc, editor_grab);
    (void)build_action(&store->actions[ACTION_EDITOR_HANDLES], alloc, editor_handles);
    (void)build_action(&store->actions[ACTION_EDITOR_WATCH], alloc, editor_watch);
    (void)build_action(&store->actions[ACTION_EDITOR_UNDO], alloc, editor_undo);
    (void)build_action(&store->actions[ACTION_EDITOR_REDO], alloc, editor_redo);

    (void)build_action(&store->actions[ACTION_ATTR_DEC_1], alloc, attr_dec_1);
    (void)build_action(&store->actions[ACTION_ATTR_INC_1], alloc, attr_inc_1);
    (void)build_action(&store->actions[ACTION_ATTR_DEC_10], alloc, attr_dec_10);
    (void)build_action(&store->actions[ACTION_ATTR_INC_10], alloc, attr_inc_10);
    (void)build_action(&store->actions[ACTION_ATTR_DEC_100], alloc, attr_dec_100);
    (void)build_action(&store->actions[ACTION_ATTR_INC_100], alloc, attr_inc_100);

    (void)build_action(&store->actions[ACTION_BLUEPRINT_DUPLICATE], alloc, blueprint_duplicate);
    (void)build_action(&store->actions[ACTION_BLUEPRINT_REMOVE], alloc, blueprint_remove);

    (void)build_action(&store->actions[ACTION_WB_KEYBOARD_MODE], alloc, wb_keyboard_mode);
    (void)build_action(&store->actions[ACTION_WB_APPEND], alloc, wb_append);
    (void)build_action(&store->actions[ACTION_WB_POP], alloc, wb_pop);

    (void)build_action(&store->actions[ACTION_INTERACT], alloc, interact);

    (void)build_action(&store->actions[ACTION_MENU_TOGGLE], alloc, menu_toggle);
    (void)build_action(&store->actions[ACTION_FONT_PREVIEW_TOGGLE], alloc, font_preview_toggle);
    (void)build_action(&store->actions[ACTION_QUIT], alloc, quit);

    (void)build_axis(&store->axes[AXIS_PRIMARY_X], alloc, axis_primary_x);
    (void)build_axis(&store->axes[AXIS_PRIMARY_Y], alloc, axis_primary_y);
    (void)build_axis(&store->axes[AXIS_SECONDARY_X], alloc, axis_secondary_x);
    (void)build_axis(&store->axes[AXIS_SECONDARY_Y], alloc, axis_secondary_y);
    (void)build_axis(&store->axes[AXIS_TRIGGER_LEFT], alloc, axis_trigger_left);
    (void)build_axis(&store->axes[AXIS_TRIGGER_RIGHT], alloc, axis_trigger_right);
}

/* --- TOML loader (deferred to a follow-up commit) ------------------------ */

bool input_func_load_bindings_toml(BindingStore *store, Allocator alloc, ErrorState *err, const char *toml_path)
{
    /* Built-in defaults are the source of truth for stage 1. The TOML
     * overlay parser is implemented in a later stage of the migration
     * (see plans/parsed-floating-dolphin.md). Returning true here means:
     * "no overlay applied, defaults remain". */
    (void)store;
    (void)alloc;
    (void)err;
    (void)toml_path;
    return true;
}
