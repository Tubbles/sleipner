#include "inventory_screen.h"

#include "alloc.h"
#include "blur.h"
#include "input.h"
#include "input_func.h"
#include "map.h"
#include "raylib.h"
#include "rule.h"
#include "strv.h"

#include <stdbool.h>

VEC_IMPL(inventory_item_entry, InventoryItemEntry)

#define INVENTORY_VIGNETTE_ALPHA 200
#define INVENTORY_CELL_ALPHA 160
#define INVENTORY_CELL_HIGHLIGHT_ALPHA 220
#define INVENTORY_TEXT_DEFAULT 220
#define INVENTORY_TEXT_HIGHLIGHT_R 255
#define INVENTORY_TEXT_HIGHLIGHT_G 230
#define INVENTORY_TEXT_HIGHLIGHT_B 120

void inventory_screen_init(InventoryScreen *screen)
{
    *screen = (InventoryScreen){0};
}

vec_inventory_item_entry inventory_screen_collect_items(const ItemSet *items, Allocator alloc)
{
    vec_inventory_item_entry entries = vec_inventory_item_entry_new(alloc);
    for (int index = 0; index < items->counts.capacity; index++) {
        if (items->counts.entries[index].state != MAP_ENTRY_OCCUPIED) {
            continue;
        }
        InventoryItemEntry entry = {
            .name = items->counts.entries[index].key,
            .count = items->counts.entries[index].value,
        };
        (void)vec_inventory_item_entry_push(&entries, entry);
    }
    return entries;
}

void inventory_screen_open(InventoryScreen *screen, const ItemSet *items, Allocator alloc)
{
    screen->open = true;
    screen->cursor = 0;
    screen->entries = inventory_screen_collect_items(items, alloc);
}

void inventory_screen_close(InventoryScreen *screen)
{
    screen->open = false;
    screen->blur_captured = false;
}

bool inventory_screen_is_open(const InventoryScreen *screen)
{
    return screen->open;
}

int inventory_screen_grid_nav(int cursor, InventoryGridShape shape, InventoryNavDirection direction)
{
    if (shape.item_count <= 0) {
        return 0;
    }
    int next = cursor;
    switch (direction) {
    case INVENTORY_NAV_LEFT:
        next = cursor - 1;
        break;
    case INVENTORY_NAV_RIGHT:
        next = cursor + 1;
        break;
    case INVENTORY_NAV_UP:
        next = cursor - shape.columns;
        break;
    case INVENTORY_NAV_DOWN:
        next = cursor + shape.columns;
        break;
    }
    if (next < 0) {
        return 0;
    }
    if (next >= shape.item_count) {
        return shape.item_count - 1;
    }
    return next;
}

InventoryCellPosition inventory_screen_cell_position(int cell_index, InventoryGridLayout layout)
{
    int column = cell_index % layout.columns;
    int row = cell_index / layout.columns;
    return (InventoryCellPosition){
        .x = column * (layout.cell_width + layout.margin),
        .y = row * (layout.cell_height + layout.margin),
    };
}

void inventory_screen_handle_input(InventoryScreen *screen,
                                   const InputState *input,
                                   const BindingStore *bindings,
                                   bool *close_requested)
{
    InventoryGridShape shape = {.columns = INVENTORY_GRID_COLUMNS, .item_count = screen->entries.count};
    if (input_pressed(input, bindings, ACTION_NAV_LEFT)) {
        screen->cursor = inventory_screen_grid_nav(screen->cursor, shape, INVENTORY_NAV_LEFT);
    }
    if (input_pressed(input, bindings, ACTION_NAV_RIGHT)) {
        screen->cursor = inventory_screen_grid_nav(screen->cursor, shape, INVENTORY_NAV_RIGHT);
    }
    if (input_pressed(input, bindings, ACTION_NAV_UP)) {
        screen->cursor = inventory_screen_grid_nav(screen->cursor, shape, INVENTORY_NAV_UP);
    }
    if (input_pressed(input, bindings, ACTION_NAV_DOWN)) {
        screen->cursor = inventory_screen_grid_nav(screen->cursor, shape, INVENTORY_NAV_DOWN);
    }
    if (input_pressed(input, bindings, ACTION_CANCEL)) {
        *close_requested = true;
    }
}

/* shape.columns doubles here as the grid's fixed column count; shape.item_count
 * is what actually determines row count. Reuses InventoryGridShape (rather than
 * two bare int parameters) for the same swappable-parameters reason as
 * inventory_screen_grid_nav above. */
static int inventory_screen_row_count(InventoryGridShape shape)
{
    if (shape.item_count <= 0) {
        return 1;
    }
    return (shape.item_count + shape.columns - 1) / shape.columns;
}

static void draw_empty_state(Font ui_font, Rectangle panel)
{
    const char *label = "Empty";
    Vector2 measured = MeasureTextEx(ui_font, label, INVENTORY_FONT_SIZE, INVENTORY_LETTER_SPACING);
    float text_x = panel.x + ((panel.width - measured.x) / 2.0F);
    float text_y = panel.y + ((panel.height - measured.y) / 2.0F);
    Color color = {INVENTORY_TEXT_DEFAULT, INVENTORY_TEXT_DEFAULT, INVENTORY_TEXT_DEFAULT, 255};
    DrawTextEx(ui_font, label, (Vector2){text_x, text_y}, INVENTORY_FONT_SIZE, INVENTORY_LETTER_SPACING, color);
}

static void draw_item_cell(
    const InventoryItemEntry *entry, bool selected, Font ui_font, InventoryGridLayout layout, Vector2 cell_origin)
{
    Color fill = selected ? (Color){INVENTORY_TEXT_HIGHLIGHT_R, INVENTORY_TEXT_HIGHLIGHT_G, INVENTORY_TEXT_HIGHLIGHT_B,
                                    INVENTORY_CELL_HIGHLIGHT_ALPHA}
                          : (Color){0, 0, 0, INVENTORY_CELL_ALPHA};
    DrawRectangle((int)cell_origin.x, (int)cell_origin.y, layout.cell_width, layout.cell_height, fill);

    const char *label = TextFormat("%.*s x%d", (int)entry->name.len, entry->name.ptr, entry->count);
    Vector2 measured = MeasureTextEx(ui_font, label, INVENTORY_FONT_SIZE, INVENTORY_LETTER_SPACING);
    float text_x = cell_origin.x + (((float)layout.cell_width - measured.x) / 2.0F);
    float text_y = cell_origin.y + (((float)layout.cell_height - measured.y) / 2.0F);
    Color normal_color = {INVENTORY_TEXT_DEFAULT, INVENTORY_TEXT_DEFAULT, INVENTORY_TEXT_DEFAULT, 255};
    Color text_color = selected ? BLACK : normal_color;
    DrawTextEx(ui_font, label, (Vector2){text_x, text_y}, INVENTORY_FONT_SIZE, INVENTORY_LETTER_SPACING, text_color);
}

/* rows precedes layout (a struct) rather than sitting beside screen_width,
 * so the only adjacent same-type pair left is (screen_width, screen_height)
 * -- the same trailing pair menu_render/settings_render already use without
 * a swappable-parameters warning. */
static Rectangle compute_panel_rect(int rows, InventoryGridLayout layout, int screen_width, int screen_height)
{
    int grid_width = (layout.columns * layout.cell_width) + ((layout.columns - 1) * layout.margin);
    int grid_height = (rows * layout.cell_height) + ((rows - 1) * layout.margin);
    int panel_width = grid_width + (INVENTORY_PANEL_PADDING * 2);
    int panel_height = grid_height + (INVENTORY_PANEL_PADDING * 2);
    return (Rectangle){
        .x = (float)(screen_width - panel_width) / 2.0F,
        .y = (float)(screen_height - panel_height) / 2.0F,
        .width = (float)panel_width,
        .height = (float)panel_height,
    };
}

void inventory_screen_render(
    const InventoryScreen *screen, Font ui_font, const BlurPipeline *blur, int screen_width, int screen_height)
{
    if (!screen->open) {
        return;
    }
    blur_draw(blur, (Rectangle){0, 0, (float)screen_width, (float)screen_height});

    InventoryGridLayout layout = {
        .columns = INVENTORY_GRID_COLUMNS,
        .cell_width = INVENTORY_CELL_WIDTH,
        .cell_height = INVENTORY_CELL_HEIGHT,
        .margin = INVENTORY_CELL_MARGIN,
    };
    int item_count = screen->entries.count;
    InventoryGridShape shape = {.columns = layout.columns, .item_count = item_count};
    int rows = inventory_screen_row_count(shape);
    Rectangle panel = compute_panel_rect(rows, layout, screen_width, screen_height);
    DrawRectangle((int)panel.x, (int)panel.y, (int)panel.width, (int)panel.height,
                  (Color){0, 0, 0, INVENTORY_VIGNETTE_ALPHA});

    if (item_count == 0) {
        draw_empty_state(ui_font, panel);
        return;
    }

    Vector2 grid_origin = {panel.x + INVENTORY_PANEL_PADDING, panel.y + INVENTORY_PANEL_PADDING};
    for (int index = 0; index < item_count; index++) {
        InventoryCellPosition position = inventory_screen_cell_position(index, layout);
        Vector2 cell_origin = {grid_origin.x + (float)position.x, grid_origin.y + (float)position.y};
        draw_item_cell(&screen->entries.data[index], index == screen->cursor, ui_font, layout, cell_origin);
    }
}

void inventory_screen_cleanup(InventoryScreen *screen)
{
    *screen = (InventoryScreen){0};
}
