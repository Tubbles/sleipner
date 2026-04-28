#pragma once

#include "input.h"
#include "input_func.h"
#include "raylib.h"

#include <stdbool.h>

/* Two-level radial character picker.
 *
 * The widget is generic: it owns no buffer of its own. The caller passes in
 * a borrowed buffer (`buf`, `len`, `cap`) and the widget appends or
 * backspaces characters into it. group/selected hold the two-level radial
 * navigation state (level 1 = pick a group, level 2 = pick a char within
 * the group).
 *
 * The host (editor word builder, settings path edit) decides what
 * "exit requested" means by inspecting the result code returned by
 * keyboard_widget_handle_input. The widget never mutates host state. */

#define KEYBOARD_GROUP_COUNT 9         /* number of character groups */
#define KEYBOARD_MAX_CHARS_PER_GROUP 5 /* max characters in any single group */

typedef struct {
    int width;
    int height;
} KbScreenSize;

typedef struct {
    int group;    /* -1 = top-level group select; 0..KEYBOARD_GROUP_COUNT-1 = inside that group */
    int selected; /* radial sector highlighted by the stick (-1 = dead zone) */
    char *buf;    /* borrowed; host owns the storage */
    int *len;     /* borrowed; current character count, never including the null terminator */
    int cap;      /* buf capacity in bytes (writable area is [0, cap-1) since cap-1 holds '\0') */
} KeyboardWidget;

typedef enum {
    KB_RESULT_NONE,
    KB_RESULT_TYPED,
    KB_RESULT_BACKSPACED,
    KB_RESULT_EXIT_REQUESTED, /* CANCEL pressed at top-level group with empty buffer */
} KeyboardWidgetResult;

/* Wire the widget to a caller-owned buffer. group/selected are reset to
 * top-level (no group, no selection). Call once when entering the screen
 * that hosts the widget. */
void keyboard_widget_reset(KeyboardWidget *widget, char *buf, int *len, int cap);

/* Run one frame of input. Returns a discriminator the host uses to react to
 * exit / typed / backspace events as appropriate for its own state machine. */
[[nodiscard]] KeyboardWidgetResult
keyboard_widget_handle_input(KeyboardWidget *widget, const InputState *input, const BindingStore *bindings);

/* Draw the radial, the buffer-status line, and (when in level 2) the
 * group indicator. Caller positions the widget at screen center. */
void keyboard_widget_draw(const KeyboardWidget *widget, KbScreenSize screen, Font ui_font);
