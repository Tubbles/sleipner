#include "menu.h"

#include "blur.h"
#include "input.h"
#include "input_func.h"
#include "raylib.h"

#include <stddef.h>

#define MENU_LINE_PADDING 16
#define MENU_BLOCK_VPAD 32
#define MENU_BLOCK_MIN_WIDTH 480
#define MENU_VIGNETTE_ALPHA 160
#define MENU_SELECTED_FONT_BUMP 8
#define MENU_LETTER_SPACING 2.0F
#define MENU_TEXT_DEFAULT 220
#define MENU_TEXT_HIGHLIGHT_R 255
#define MENU_TEXT_HIGHLIGHT_G 230
#define MENU_TEXT_HIGHLIGHT_B 120

static const char *entry_label(MenuEntry entry)
{
    switch (entry) {
    case MENU_ENTRY_RESUME:
        return "RESUME";
    case MENU_ENTRY_SAVE:
        return "SAVE";
    case MENU_ENTRY_RESTORE:
        return "RESTORE";
    case MENU_ENTRY_SAVE_GAME:
        return "SAVE GAME";
    case MENU_ENTRY_LOAD_GAME:
        return "LOAD GAME";
    case MENU_ENTRY_HOST_GAME:
        return "HOST GAME";
    case MENU_ENTRY_JOIN_GAME:
        return "JOIN GAME";
    case MENU_ENTRY_INVENTORY:
        return "INVENTORY";
    case MENU_ENTRY_SETTINGS:
        return "SETTINGS";
    case MENU_ENTRY_TOGGLE_DEBUG_OVERLAY:
        return "TOGGLE DEBUG OVERLAY";
    case MENU_ENTRY_QUIT:
        return "QUIT";
    case MENU_ENTRY_COUNT:
        break;
    }
    return "";
}

static MenuAction action_for_entry(MenuEntry entry)
{
    switch (entry) {
    case MENU_ENTRY_RESUME:
        return MENU_ACTION_RESUME;
    case MENU_ENTRY_SAVE:
        return MENU_ACTION_SAVE;
    case MENU_ENTRY_RESTORE:
        return MENU_ACTION_RESTORE;
    case MENU_ENTRY_SAVE_GAME:
        return MENU_ACTION_OPEN_SAVE_MENU;
    case MENU_ENTRY_LOAD_GAME:
        return MENU_ACTION_OPEN_LOAD_MENU;
    case MENU_ENTRY_HOST_GAME:
        return MENU_ACTION_HOST_GAME;
    case MENU_ENTRY_JOIN_GAME:
        return MENU_ACTION_JOIN_GAME;
    case MENU_ENTRY_INVENTORY:
        return MENU_ACTION_OPEN_INVENTORY;
    case MENU_ENTRY_SETTINGS:
        return MENU_ACTION_OPEN_SETTINGS;
    case MENU_ENTRY_TOGGLE_DEBUG_OVERLAY:
        return MENU_ACTION_TOGGLE_DEBUG_OVERLAY;
    case MENU_ENTRY_QUIT:
        return MENU_ACTION_QUIT;
    case MENU_ENTRY_COUNT:
        break;
    }
    return MENU_ACTION_NONE;
}

void menu_init(MenuState *menu)
{
    *menu = (MenuState){0};
}

void menu_set_font(MenuState *menu, Font font)
{
    menu->font = font;
    menu->font_loaded = true;
}

void menu_open(MenuState *menu)
{
    menu->open = true;
    menu->selected = MENU_ENTRY_RESUME;
}

void menu_close(MenuState *menu)
{
    menu->open = false;
    menu->blur_captured = false;
}

MenuAction menu_handle_input(MenuState *menu, const InputState *input, const BindingStore *bindings)
{
    if (input_pressed(input, bindings, ACTION_NAV_UP)) {
        if (menu->selected > 0) {
            menu->selected--;
        }
    }
    if (input_pressed(input, bindings, ACTION_NAV_DOWN)) {
        if (menu->selected < MENU_ENTRY_COUNT - 1) {
            menu->selected++;
        }
    }
    if (input_pressed(input, bindings, ACTION_CANCEL)) {
        return MENU_ACTION_RESUME;
    }
    if (input_pressed(input, bindings, ACTION_CONFIRM)) {
        return action_for_entry((MenuEntry)menu->selected);
    }
    return MENU_ACTION_NONE;
}

void menu_render(const MenuState *menu, const BlurPipeline *blur, int screen_width, int screen_height)
{
    if (!menu->open) {
        return;
    }
    blur_draw(blur, (Rectangle){0, 0, (float)screen_width, (float)screen_height});

    /* Vignette panel behind the list — semi-transparent dark rectangle
     * sized to the entry block plus a margin. */
    int line_height = MENU_FONT_SIZE + MENU_LINE_PADDING;
    int block_height = (line_height * MENU_ENTRY_COUNT) + (MENU_BLOCK_VPAD * 2);
    int block_width = screen_width / 2;
    if (block_width < MENU_BLOCK_MIN_WIDTH) {
        block_width = MENU_BLOCK_MIN_WIDTH;
    }
    int block_x = (screen_width - block_width) / 2;
    int block_y = (screen_height - block_height) / 2;
    DrawRectangle(block_x, block_y, block_width, block_height, (Color){0, 0, 0, MENU_VIGNETTE_ALPHA});

    int first_line_y = block_y + MENU_BLOCK_VPAD;
    Color highlight = {MENU_TEXT_HIGHLIGHT_R, MENU_TEXT_HIGHLIGHT_G, MENU_TEXT_HIGHLIGHT_B, 255};
    Color normal = {MENU_TEXT_DEFAULT, MENU_TEXT_DEFAULT, MENU_TEXT_DEFAULT, 255};
    for (int index = 0; index < MENU_ENTRY_COUNT; index++) {
        const char *label = entry_label((MenuEntry)index);
        bool selected = (index == menu->selected);
        float font_size = selected ? (float)(MENU_FONT_SIZE + MENU_SELECTED_FONT_BUMP) : (float)MENU_FONT_SIZE;
        Vector2 measured = MeasureTextEx(menu->font, label, font_size, MENU_LETTER_SPACING);
        int line_x = block_x + ((block_width - (int)measured.x) / 2);
        int line_y = first_line_y + (index * line_height);
        Color color = selected ? highlight : normal;
        DrawTextEx(menu->font, label, (Vector2){(float)line_x, (float)line_y}, font_size, MENU_LETTER_SPACING, color);
    }
}

void menu_cleanup(MenuState *menu)
{
    *menu = (MenuState){0};
}
