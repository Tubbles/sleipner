#include "save_screen.h"

#include "alloc.h"
#include "blur.h"
#include "input.h"
#include "input_func.h"
#include "raylib.h"
#include "str.h"
#include "strv.h"

#include <stdbool.h>
#include <stdio.h>

#define SAVE_SCREEN_VIGNETTE_ALPHA 200
#define SAVE_SCREEN_TEXT_DEFAULT 220
#define SAVE_SCREEN_TEXT_DISABLED 120
#define SAVE_SCREEN_TEXT_HIGHLIGHT_R 255
#define SAVE_SCREEN_TEXT_HIGHLIGHT_G 230
#define SAVE_SCREEN_TEXT_HIGHLIGHT_B 120

void save_screen_init(SaveScreen *screen)
{
    *screen = (SaveScreen){0};
}

int save_screen_entry_count(SaveScreenMode mode)
{
    return mode == SAVE_SCREEN_MODE_LOAD ? SAVE_SLOT_COUNT + 1 : SAVE_SLOT_COUNT;
}

bool save_screen_entry_is_autosave(int entry_index)
{
    return entry_index >= SAVE_SLOT_COUNT;
}

bool save_screen_entry_exists(const SaveScreen *screen, int entry_index)
{
    if (save_screen_entry_is_autosave(entry_index)) {
        return screen->autosave_exists;
    }
    return screen->slot_exists[entry_index];
}

int save_screen_nav(SaveScreenCursor state, SaveScreenNavDirection direction)
{
    if (state.entry_count <= 0) {
        return 0;
    }
    int next = state.cursor + (direction == SAVE_SCREEN_NAV_DOWN ? 1 : -1);
    if (next < 0) {
        return 0;
    }
    if (next >= state.entry_count) {
        return state.entry_count - 1;
    }
    return next;
}

Str save_screen_slot_path(Strv saves_dir, int slot_index, Allocator alloc)
{
    Str path = str_new(alloc);
    if (!str_append_strv(&path, saves_dir)) {
        return path;
    }
    char filename[SAVE_SCREEN_FILENAME_BUF_SIZE];
    (void)snprintf(filename, sizeof(filename), "save_%d.toml", slot_index + 1);
    (void)str_append_cstr(&path, filename);
    return path;
}

Str save_screen_autosave_path(Strv saves_dir, Allocator alloc)
{
    Str path = str_new(alloc);
    if (!str_append_strv(&path, saves_dir)) {
        return path;
    }
    (void)str_append_cstr(&path, SAVE_SCREEN_AUTOSAVE_FILENAME);
    return path;
}

void save_screen_open(SaveScreen *screen, SaveScreenMode mode, Strv saves_dir, Allocator alloc)
{
    screen->open = true;
    screen->mode = mode;
    screen->cursor = 0;
    for (int index = 0; index < SAVE_SLOT_COUNT; index++) {
        Str path = save_screen_slot_path(saves_dir, index, alloc);
        screen->slot_exists[index] = path.ptr != nullptr && FileExists(path.ptr);
    }
    Str autosave_path = save_screen_autosave_path(saves_dir, alloc);
    screen->autosave_exists = autosave_path.ptr != nullptr && FileExists(autosave_path.ptr);
}

void save_screen_close(SaveScreen *screen)
{
    screen->open = false;
    screen->blur_captured = false;
}

bool save_screen_is_open(const SaveScreen *screen)
{
    return screen->open;
}

void save_screen_handle_input(SaveScreen *screen,
                              const InputState *input,
                              const BindingStore *bindings,
                              bool *close_requested,
                              int *confirmed_slot)
{
    SaveScreenCursor cursor_state = {.cursor = screen->cursor, .entry_count = save_screen_entry_count(screen->mode)};
    if (input_pressed(input, bindings, ACTION_NAV_UP)) {
        screen->cursor = save_screen_nav(cursor_state, SAVE_SCREEN_NAV_UP);
    }
    if (input_pressed(input, bindings, ACTION_NAV_DOWN)) {
        screen->cursor = save_screen_nav(cursor_state, SAVE_SCREEN_NAV_DOWN);
    }
    if (input_pressed(input, bindings, ACTION_CANCEL)) {
        *close_requested = true;
    }
    if (input_pressed(input, bindings, ACTION_CONFIRM)) {
        *confirmed_slot = screen->cursor;
    }
}

static const char *save_screen_mode_header(SaveScreenMode mode)
{
    return mode == SAVE_SCREEN_MODE_SAVE ? "SAVE GAME" : "LOAD GAME";
}

static const char *save_screen_entry_name(int entry_index)
{
    if (save_screen_entry_is_autosave(entry_index)) {
        return "AUTOSAVE";
    }
    return TextFormat("SLOT %d", entry_index + 1);
}

static const char *save_screen_entry_status(bool exists)
{
    return exists ? "(saved)" : "(empty)";
}

void save_screen_render(
    const SaveScreen *screen, Font ui_font, const BlurPipeline *blur, int screen_width, int screen_height)
{
    if (!screen->open) {
        return;
    }
    blur_draw(blur, (Rectangle){0, 0, (float)screen_width, (float)screen_height});

    int entry_count = save_screen_entry_count(screen->mode);
    int line_height = SAVE_SCREEN_FONT_SIZE + SAVE_SCREEN_LINE_PADDING;
    int block_height = (line_height * entry_count) + (SAVE_SCREEN_BLOCK_VPAD * 2);
    int block_width = screen_width / 2;
    if (block_width < SAVE_SCREEN_BLOCK_MIN_WIDTH) {
        block_width = SAVE_SCREEN_BLOCK_MIN_WIDTH;
    }
    int block_x = (screen_width - block_width) / 2;
    int block_y = (screen_height - block_height) / 2;
    DrawRectangle(block_x, block_y, block_width, block_height, (Color){0, 0, 0, SAVE_SCREEN_VIGNETTE_ALPHA});

    Color normal = {SAVE_SCREEN_TEXT_DEFAULT, SAVE_SCREEN_TEXT_DEFAULT, SAVE_SCREEN_TEXT_DEFAULT, 255};
    const char *header = save_screen_mode_header(screen->mode);
    Vector2 header_measured = MeasureTextEx(ui_font, header, SAVE_SCREEN_HEADER_FONT_SIZE, SAVE_SCREEN_LETTER_SPACING);
    int header_x = block_x + ((block_width - (int)header_measured.x) / 2);
    int header_y = block_y - (int)header_measured.y - SAVE_SCREEN_HEADER_GAP;
    DrawTextEx(ui_font, header, (Vector2){(float)header_x, (float)header_y}, SAVE_SCREEN_HEADER_FONT_SIZE,
               SAVE_SCREEN_LETTER_SPACING, normal);

    int first_line_y = block_y + SAVE_SCREEN_BLOCK_VPAD;
    Color highlight = {SAVE_SCREEN_TEXT_HIGHLIGHT_R, SAVE_SCREEN_TEXT_HIGHLIGHT_G, SAVE_SCREEN_TEXT_HIGHLIGHT_B, 255};
    Color disabled = {SAVE_SCREEN_TEXT_DISABLED, SAVE_SCREEN_TEXT_DISABLED, SAVE_SCREEN_TEXT_DISABLED, 255};
    for (int index = 0; index < entry_count; index++) {
        bool exists = save_screen_entry_exists(screen, index);
        bool selected = (index == screen->cursor);
        const char *label = TextFormat("%s %s", save_screen_entry_name(index), save_screen_entry_status(exists));
        float font_size =
            selected ? (float)(SAVE_SCREEN_FONT_SIZE + SAVE_SCREEN_SELECTED_FONT_BUMP) : (float)SAVE_SCREEN_FONT_SIZE;
        Vector2 measured = MeasureTextEx(ui_font, label, font_size, SAVE_SCREEN_LETTER_SPACING);
        int line_x = block_x + ((block_width - (int)measured.x) / 2);
        int line_y = first_line_y + (index * line_height);
        Color color = disabled;
        if (selected) {
            color = highlight;
        } else if (exists) {
            color = normal;
        }
        DrawTextEx(ui_font, label, (Vector2){(float)line_x, (float)line_y}, font_size, SAVE_SCREEN_LETTER_SPACING,
                   color);
    }
}

void save_screen_cleanup(SaveScreen *screen)
{
    *screen = (SaveScreen){0};
}
