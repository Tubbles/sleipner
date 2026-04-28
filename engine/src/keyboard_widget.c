#include "keyboard_widget.h"

#include "input.h"
#include "input_func.h"
#include "raylib.h"

#include <math.h>
#include <stdbool.h>

/* Visual constants. The radial picker in editor/widgets.c uses the same
 * numeric values for visual consistency; if these are tuned, the radial
 * picker should be re-tuned to match. The two are kept independent so
 * settings (which uses keyboard_widget) does not transitively pull in
 * editor headers. */
#define KB_INNER_RADIUS 50.0F
#define KB_OUTER_RADIUS 140.0F
#define KB_BG_PADDING 4.0F
#define KB_FONT_SIZE 32
#define KB_TOAST_FONT_SIZE 32
#define KB_FULL_CIRCLE_DEG 360.0F
#define KB_NORTH_OFFSET_DEG 90.0F
#define KB_DEG_TO_RAD 0.01745329252F
#define KB_STICK_THRESHOLD 0.3F

static const Color kb_bg_color = {30, 30, 40, 220};
static const Color kb_text_color = {200, 200, 200, 255};
static const Color kb_highlight_color = {255, 200, 50, 200};

static const char keyboard_groups[KEYBOARD_GROUP_COUNT][KEYBOARD_MAX_CHARS_PER_GROUP] = {
    {'a', 'e', 'i', 'o', 'u'},   /* vowels */
    {'t', 'n', 's', 'r', '\0'},  /* common 1 */
    {'l', 'd', 'h', 'c', '\0'},  /* common 2 */
    {'m', 'p', 'f', 'g', '\0'},  /* medium */
    {'b', 'w', 'v', 'k', '\0'},  /* less common */
    {'j', 'x', 'y', 'z', 'q'},   /* rare */
    {'0', '1', '2', '3', '4'},   /* digits low */
    {'5', '6', '7', '8', '9'},   /* digits high */
    {'_', '-', '.', '\0', '\0'}, /* punctuation */
};

static const int keyboard_group_sizes[KEYBOARD_GROUP_COUNT] = {5, 4, 4, 4, 4, 5, 5, 5, 3};

static const char *const keyboard_group_labels[KEYBOARD_GROUP_COUNT] = {
    "aeiou", "tnsr", "ldhc", "mpfg", "bwvk", "jxyzq", "01234", "56789", "_-.",
};

static int sector_from_stick(Vector2 stick, int item_count)
{
    float magnitude = sqrtf((stick.x * stick.x) + (stick.y * stick.y));
    if (magnitude < KB_STICK_THRESHOLD) {
        return -1;
    }
    float angle = atan2f(stick.y, stick.x);
    float two_pi = 2.0F * (float)M_PI;
    float normalized = fmodf(angle + ((float)M_PI / 2.0F) + two_pi, two_pi);
    return (int)(normalized * (float)item_count / two_pi) % item_count;
}

static bool kb_type_char(KeyboardWidget *widget, char character)
{
    if (*widget->len >= widget->cap - 1) {
        return false;
    }
    widget->buf[*widget->len] = character;
    (*widget->len)++;
    widget->buf[*widget->len] = '\0';
    return true;
}

static bool kb_backspace(KeyboardWidget *widget)
{
    if (*widget->len <= 0) {
        return false;
    }
    (*widget->len)--;
    widget->buf[*widget->len] = '\0';
    return true;
}

static int kb_item_count(const KeyboardWidget *widget)
{
    if (widget->group < 0) {
        return KEYBOARD_GROUP_COUNT;
    }
    return keyboard_group_sizes[widget->group];
}

static const char *kb_item_label(const KeyboardWidget *widget, int index)
{
    if (widget->group < 0) {
        if (index >= 0 && index < KEYBOARD_GROUP_COUNT) {
            return keyboard_group_labels[index];
        }
        return "";
    }
    if (index >= 0 && index < keyboard_group_sizes[widget->group]) {
        return TextFormat("%c", keyboard_groups[widget->group][index]);
    }
    return "";
}

void keyboard_widget_reset(KeyboardWidget *widget, char *buf, int *len, int cap)
{
    widget->group = -1;
    widget->selected = -1;
    widget->buf = buf;
    widget->len = len;
    widget->cap = cap;
}

KeyboardWidgetResult
keyboard_widget_handle_input(KeyboardWidget *widget, const InputState *input, const BindingStore *bindings)
{
    int total = kb_item_count(widget);
    Vector2 stick = input_axis_pair(input, bindings, AXIS_PRIMARY_X, AXIS_PRIMARY_Y);
    widget->selected = sector_from_stick(stick, total);

    if (input_pressed(input, bindings, ACTION_CONFIRM) && widget->selected >= 0) {
        if (widget->group < 0) {
            widget->group = widget->selected;
            widget->selected = -1;
            return KB_RESULT_NONE;
        }
        char character = keyboard_groups[widget->group][widget->selected];
        widget->group = -1;
        widget->selected = -1;
        if (kb_type_char(widget, character)) {
            return KB_RESULT_TYPED;
        }
        return KB_RESULT_NONE;
    }

    if (input_pressed(input, bindings, ACTION_CANCEL)) {
        if (widget->group >= 0) {
            widget->group = -1;
            return KB_RESULT_NONE;
        }
        if (kb_backspace(widget)) {
            return KB_RESULT_BACKSPACED;
        }
        return KB_RESULT_EXIT_REQUESTED;
    }

    return KB_RESULT_NONE;
}

void keyboard_widget_draw(const KeyboardWidget *widget, KbScreenSize screen, Font ui_font)
{
    int center_x = screen.width / 2;
    int center_y = screen.height / 2;
    DrawCircle(center_x, center_y, KB_OUTER_RADIUS + KB_BG_PADDING, kb_bg_color);

    int total = kb_item_count(widget);
    float sector_deg = KB_FULL_CIRCLE_DEG / (float)total;
    for (int index = 0; index < total; index++) {
        float start = ((float)index * sector_deg) - KB_NORTH_OFFSET_DEG;
        float end = start + sector_deg;
        bool selected = (index == widget->selected);
        Color sector_color = selected ? kb_highlight_color
                                      : (Color){kb_highlight_color.r, kb_highlight_color.g, kb_highlight_color.b,
                                                kb_highlight_color.a / 2};
        DrawRing((Vector2){(float)center_x, (float)center_y}, KB_INNER_RADIUS, KB_OUTER_RADIUS, start, end, 16,
                 sector_color);
        float mid_deg = start + (sector_deg / 2.0F);
        float mid_rad = mid_deg * KB_DEG_TO_RAD;
        float label_radius = (KB_INNER_RADIUS + KB_OUTER_RADIUS) / 2.0F;
        int label_x = center_x + (int)(cosf(mid_rad) * label_radius);
        int label_y = center_y + (int)(sinf(mid_rad) * label_radius);
        const char *label = kb_item_label(widget, index);
        Vector2 label_size = MeasureTextEx(ui_font, label, (float)KB_FONT_SIZE, 1);
        DrawTextEx(ui_font, label,
                   (Vector2){(float)label_x - (label_size.x / 2.0F), (float)label_y - ((float)KB_FONT_SIZE / 2.0F)},
                   (float)KB_FONT_SIZE, 1, sector_color);
    }
    DrawCircle(center_x, center_y, KB_INNER_RADIUS, kb_bg_color);

    /* Buffer status line above the radial. */
    const char *buffer_text = TextFormat("> %s|", widget->buf ? widget->buf : "");
    Vector2 buffer_size = MeasureTextEx(ui_font, buffer_text, (float)KB_TOAST_FONT_SIZE, 1);
    int buffer_y = center_y - (int)KB_OUTER_RADIUS - KB_TOAST_FONT_SIZE - (int)KB_BG_PADDING;
    DrawTextEx(ui_font, buffer_text, (Vector2){(float)center_x - (buffer_size.x / 2.0F), (float)buffer_y},
               (float)KB_TOAST_FONT_SIZE, 1, WHITE);

    /* Group indicator below the radial when in level 2. */
    if (widget->group >= 0) {
        const char *group_text = TextFormat("[ %s ]", keyboard_group_labels[widget->group]);
        Vector2 group_size = MeasureTextEx(ui_font, group_text, (float)KB_FONT_SIZE, 1);
        int group_y = center_y + (int)KB_OUTER_RADIUS + (int)KB_BG_PADDING;
        DrawTextEx(ui_font, group_text, (Vector2){(float)center_x - (group_size.x / 2.0F), (float)group_y},
                   (float)KB_FONT_SIZE, 1, kb_text_color);
    }
}
