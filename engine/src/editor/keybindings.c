#include "editor/keybindings.h"

#include "raylib.h"

#include <stdio.h>
#include <string.h>

#define LABEL_BUF_CAP 32
#define OVERFLOW_TAIL_CAP 16

typedef struct {
    int code;
    const char *label;
} CodeName;

typedef struct {
    const CodeName *entries;
    int count;
} NameTable;

/* Keyboard key labels. Only the keys actually bound in editor submodes are
 * listed — anything else falls back to "?". Order is not significant. */
static const CodeName keyboard_names[] = {
    {KEY_ENTER, "Ent"},
    {KEY_ESCAPE, "Esc"},
    {KEY_TAB, "Tab"},
    {KEY_SPACE, "Spc"},
    {KEY_DELETE, "Del"},
    {KEY_BACKSPACE, "Bksp"},
    {KEY_UP, "Up"},
    {KEY_DOWN, "Dn"},
    {KEY_LEFT, "Left"},
    {KEY_RIGHT, "Right"},
    {KEY_PAGE_UP, "PgUp"},
    {KEY_PAGE_DOWN, "PgDn"},
    {KEY_LEFT_SHIFT, "Shift"},
    {KEY_LEFT_CONTROL, "Ctrl"},
    {KEY_LEFT_ALT, "Alt"},
    {KEY_LEFT_BRACKET, "["},
    {KEY_RIGHT_BRACKET, "]"},
    {KEY_F1, "F1"},
    {KEY_F2, "F2"},
    {KEY_F3, "F3"},
    {KEY_F4, "F4"},
    {KEY_F5, "F5"},
    {KEY_F6, "F6"},
    {KEY_F7, "F7"},
    {KEY_F8, "F8"},
    {KEY_F9, "F9"},
    {KEY_F10, "F10"},
    {KEY_A, "A"},
    {KEY_B, "B"},
    {KEY_C, "C"},
    {KEY_D, "D"},
    {KEY_E, "E"},
    {KEY_G, "G"},
    {KEY_H, "H"},
    {KEY_P, "P"},
    {KEY_Q, "Q"},
    {KEY_S, "S"},
    {KEY_W, "W"},
    {KEY_X, "X"},
    {KEY_Y, "Y"},
    {KEY_Z, "Z"},
};

static const CodeName gamepad_names[] = {
    {GAMEPAD_BUTTON_RIGHT_FACE_DOWN, "A"},   {GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, "B"},
    {GAMEPAD_BUTTON_RIGHT_FACE_LEFT, "X"},   {GAMEPAD_BUTTON_RIGHT_FACE_UP, "Y"},
    {GAMEPAD_BUTTON_LEFT_FACE_DOWN, "Dn"},   {GAMEPAD_BUTTON_LEFT_FACE_UP, "Up"},
    {GAMEPAD_BUTTON_LEFT_FACE_LEFT, "Left"}, {GAMEPAD_BUTTON_LEFT_FACE_RIGHT, "Right"},
    {GAMEPAD_BUTTON_LEFT_TRIGGER_1, "L1"},   {GAMEPAD_BUTTON_LEFT_TRIGGER_2, "L2"},
    {GAMEPAD_BUTTON_RIGHT_TRIGGER_1, "R1"},  {GAMEPAD_BUTTON_RIGHT_TRIGGER_2, "R2"},
    {GAMEPAD_BUTTON_LEFT_THUMB, "L3"},       {GAMEPAD_BUTTON_RIGHT_THUMB, "R3"},
    {GAMEPAD_BUTTON_MIDDLE_LEFT, "Sel"},     {GAMEPAD_BUTTON_MIDDLE_RIGHT, "Start"},
};

static const NameTable keyboard_table = {keyboard_names, (int)(sizeof(keyboard_names) / sizeof(keyboard_names[0]))};
static const NameTable gamepad_table = {gamepad_names, (int)(sizeof(gamepad_names) / sizeof(gamepad_names[0]))};

static const char *lookup_name(NameTable table, int code)
{
    for (int index = 0; index < table.count; index++) {
        if (table.entries[index].code == code) {
            return table.entries[index].label;
        }
    }
    return "?";
}

static bool keyboard_modifier_down(int modifier_key)
{
    return modifier_key == 0 || IsKeyDown(modifier_key);
}

static bool gamepad_modifier_down(int modifier_button)
{
    return modifier_button == 0 || IsGamepadButtonDown(0, modifier_button);
}

bool binding_modifier_down(const EditorBinding *action)
{
    if (action->modifier.key == 0 && action->modifier.gamepad_button == 0) {
        return true;
    }
    if (action->modifier.key != 0 && IsKeyDown(action->modifier.key)) {
        return true;
    }
    return action->modifier.gamepad_button != 0 && IsGamepadButtonDown(0, action->modifier.gamepad_button);
}

bool binding_pressed(const EditorBinding *action)
{
    bool kb_main = action->binding.key != 0 && IsKeyPressed(action->binding.key);
    if (kb_main && keyboard_modifier_down(action->modifier.key)) {
        return true;
    }
    bool gp_main = action->binding.gamepad_button != 0 && IsGamepadButtonPressed(0, action->binding.gamepad_button);
    return gp_main && gamepad_modifier_down(action->modifier.gamepad_button);
}

bool binding_held(const EditorBinding *action)
{
    bool kb_main = action->binding.key != 0 && IsKeyDown(action->binding.key);
    if (kb_main && keyboard_modifier_down(action->modifier.key)) {
        return true;
    }
    bool gp_main = action->binding.gamepad_button != 0 && IsGamepadButtonDown(0, action->binding.gamepad_button);
    return gp_main && gamepad_modifier_down(action->modifier.gamepad_button);
}

static int append_str(char *out, int cap, int len, const char *text)
{
    if (len >= cap - 1) {
        return len;
    }
    int remain = cap - 1 - len;
    int text_len = (int)strlen(text);
    if (text_len > remain) {
        text_len = remain;
    }
    memcpy(out + len, text, (size_t)text_len);
    return len + text_len;
}

int binding_label(const EditorBinding *action, char *out, int cap)
{
    if (cap <= 0) {
        return 0;
    }
    int len = 0;
    bool has_gp_main = action->binding.gamepad_button != 0;
    bool has_kb_main = action->binding.key != 0;
    /* Gamepad half first — project is gamepad-first per DESIGN.md. */
    if (has_gp_main) {
        if (action->modifier.gamepad_button != 0) {
            len = append_str(out, cap, len, lookup_name(gamepad_table, action->modifier.gamepad_button));
            len = append_str(out, cap, len, "+");
        }
        len = append_str(out, cap, len, lookup_name(gamepad_table, action->binding.gamepad_button));
    }
    if (has_gp_main && has_kb_main) {
        len = append_str(out, cap, len, "/");
    }
    if (has_kb_main) {
        if (action->modifier.key != 0) {
            len = append_str(out, cap, len, lookup_name(keyboard_table, action->modifier.key));
            len = append_str(out, cap, len, "+");
        }
        len = append_str(out, cap, len, lookup_name(keyboard_table, action->binding.key));
    }
    out[len] = '\0';
    return len;
}

int binding_table_render(const EditorBindingTable *table, char *out, int cap)
{
    if (cap <= 0) {
        return 0;
    }
    int len = 0;
    if (table->mode_label != nullptr && table->mode_label[0] != '\0') {
        len = append_str(out, cap, len, "[");
        len = append_str(out, cap, len, table->mode_label);
        len = append_str(out, cap, len, "]");
    }
    for (int index = 0; index < table->count; index++) {
        const EditorBinding *action = &table->actions[index];
        if (action->binding.key == 0 && action->binding.gamepad_button == 0) {
            continue;
        }
        if (action->description == nullptr || action->description[0] == '\0') {
            continue;
        }
        int before = len;
        if (len > 0) {
            len = append_str(out, cap, len, "  |  ");
        }
        char label_buf[LABEL_BUF_CAP];
        (void)binding_label(action, label_buf, (int)sizeof(label_buf));
        len = append_str(out, cap, len, label_buf);
        len = append_str(out, cap, len, ": ");
        len = append_str(out, cap, len, action->description);
        if (len >= cap - 1) {
            len = before;
            int remaining = table->count - index;
            char tail[OVERFLOW_TAIL_CAP];
            (void)snprintf(tail, sizeof(tail), "  +%d more", remaining);
            len = append_str(out, cap, len, tail);
            break;
        }
    }
    out[len] = '\0';
    return len;
}
