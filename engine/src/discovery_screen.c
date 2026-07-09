#include "discovery_screen.h"

#include "blur.h"
#include "input.h"
#include "input_func.h"
#include "net.h" // NetAddr
#include "network.h"
#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

#define DISCOVERY_SCREEN_VIGNETTE_ALPHA 200
#define DISCOVERY_SCREEN_TEXT_DEFAULT 220
#define DISCOVERY_SCREEN_TEXT_HIGHLIGHT_R 255
#define DISCOVERY_SCREEN_TEXT_HIGHLIGHT_G 230
#define DISCOVERY_SCREEN_TEXT_HIGHLIGHT_B 120
#define IPV4_OCTET_SHIFT_0 24
#define IPV4_OCTET_SHIFT_1 16
#define IPV4_OCTET_SHIFT_2 8
#define IPV4_OCTET_MASK 0xFF

void discovery_screen_init(DiscoveryScreen *screen)
{
    *screen = (DiscoveryScreen){0};
}

int discovery_screen_nav(DiscoveryScreenCursor state, DiscoveryScreenNavDirection direction)
{
    if (state.entry_count <= 0) {
        return 0;
    }
    int next = state.cursor + (direction == DISCOVERY_SCREEN_NAV_DOWN ? 1 : -1);
    if (next < 0) {
        return 0;
    }
    if (next >= state.entry_count) {
        return state.entry_count - 1;
    }
    return next;
}

void discovery_screen_open(DiscoveryScreen *screen)
{
    screen->open = true;
    screen->cursor = 0;
}

void discovery_screen_close(DiscoveryScreen *screen)
{
    screen->open = false;
    screen->blur_captured = false;
}

bool discovery_screen_is_open(const DiscoveryScreen *screen)
{
    return screen->open;
}

/* Re-clamp screen->cursor to entry_count -- see discovery_screen_handle_input's
 * doc comment (discovery_screen.h) for why this must run even on a frame
 * with no nav input at all. */
static void discovery_screen_clamp_cursor(DiscoveryScreen *screen, int entry_count)
{
    if (entry_count <= 0) {
        screen->cursor = 0;
        return;
    }
    if (screen->cursor >= entry_count) {
        screen->cursor = entry_count - 1;
    }
}

void discovery_screen_handle_input(DiscoveryScreen *screen,
                                   const InputState *input,
                                   const BindingStore *bindings,
                                   const JoinList *join_list,
                                   bool *close_requested,
                                   int *confirmed_index)
{
    int entry_count = join_list->count;
    discovery_screen_clamp_cursor(screen, entry_count);

    DiscoveryScreenCursor cursor_state = {.cursor = screen->cursor, .entry_count = entry_count};
    if (input_pressed(input, bindings, ACTION_NAV_UP)) {
        screen->cursor = discovery_screen_nav(cursor_state, DISCOVERY_SCREEN_NAV_UP);
    }
    if (input_pressed(input, bindings, ACTION_NAV_DOWN)) {
        screen->cursor = discovery_screen_nav(cursor_state, DISCOVERY_SCREEN_NAV_DOWN);
    }
    if (input_pressed(input, bindings, ACTION_CANCEL)) {
        *close_requested = true;
    }
    if (entry_count > 0 && input_pressed(input, bindings, ACTION_CONFIRM)) {
        *confirmed_index = screen->cursor;
    }
}

/* "a.b.c.d:port" from a host-byte-order NetAddr -- the reverse of
 * net.c's net_addr_from_ipv4_string, but presentation-only (TextFormat's
 * internal ring buffer, immediate-use only, same convention save_screen.c's
 * save_screen_entry_name already relies on), so it lives here rather than
 * as a net.h API. */
static const char *discovery_screen_addr_text(NetAddr addr)
{
    uint8_t octet_0 = (uint8_t)((addr.host >> IPV4_OCTET_SHIFT_0) & IPV4_OCTET_MASK);
    uint8_t octet_1 = (uint8_t)((addr.host >> IPV4_OCTET_SHIFT_1) & IPV4_OCTET_MASK);
    uint8_t octet_2 = (uint8_t)((addr.host >> IPV4_OCTET_SHIFT_2) & IPV4_OCTET_MASK);
    uint8_t octet_3 = (uint8_t)(addr.host & IPV4_OCTET_MASK);
    return TextFormat("%u.%u.%u.%u:%u", octet_0, octet_1, octet_2, octet_3, addr.port);
}

static const char *discovery_screen_row_text(const DiscoveredHost *host)
{
    return TextFormat("%s  %s", host->name, discovery_screen_addr_text(host->addr));
}

void discovery_screen_render(const DiscoveryScreen *screen,
                             const JoinList *join_list,
                             Font ui_font,
                             const BlurPipeline *blur,
                             int screen_width,
                             int screen_height)
{
    if (!screen->open) {
        return;
    }
    blur_draw(blur, (Rectangle){0, 0, (float)screen_width, (float)screen_height});

    int entry_count = join_list->count;
    int row_count = entry_count > 0 ? entry_count : 1; /* one row for "Searching..." */
    int line_height = DISCOVERY_SCREEN_FONT_SIZE + DISCOVERY_SCREEN_LINE_PADDING;
    int block_height = (line_height * row_count) + (DISCOVERY_SCREEN_BLOCK_VPAD * 2);
    int block_width = screen_width / 2;
    if (block_width < DISCOVERY_SCREEN_BLOCK_MIN_WIDTH) {
        block_width = DISCOVERY_SCREEN_BLOCK_MIN_WIDTH;
    }
    int block_x = (screen_width - block_width) / 2;
    int block_y = (screen_height - block_height) / 2;
    DrawRectangle(block_x, block_y, block_width, block_height, (Color){0, 0, 0, DISCOVERY_SCREEN_VIGNETTE_ALPHA});

    Color normal = {DISCOVERY_SCREEN_TEXT_DEFAULT, DISCOVERY_SCREEN_TEXT_DEFAULT, DISCOVERY_SCREEN_TEXT_DEFAULT, 255};
    const char *header = "JOIN GAME";
    Vector2 header_measured =
        MeasureTextEx(ui_font, header, DISCOVERY_SCREEN_HEADER_FONT_SIZE, DISCOVERY_SCREEN_LETTER_SPACING);
    int header_x = block_x + ((block_width - (int)header_measured.x) / 2);
    int header_y = block_y - (int)header_measured.y - DISCOVERY_SCREEN_HEADER_GAP;
    DrawTextEx(ui_font, header, (Vector2){(float)header_x, (float)header_y}, DISCOVERY_SCREEN_HEADER_FONT_SIZE,
               DISCOVERY_SCREEN_LETTER_SPACING, normal);

    int first_line_y = block_y + DISCOVERY_SCREEN_BLOCK_VPAD;
    Color highlight = {DISCOVERY_SCREEN_TEXT_HIGHLIGHT_R, DISCOVERY_SCREEN_TEXT_HIGHLIGHT_G,
                       DISCOVERY_SCREEN_TEXT_HIGHLIGHT_B, 255};

    if (entry_count == 0) {
        const char *label = "Searching...";
        Vector2 measured = MeasureTextEx(ui_font, label, DISCOVERY_SCREEN_FONT_SIZE, DISCOVERY_SCREEN_LETTER_SPACING);
        int line_x = block_x + ((block_width - (int)measured.x) / 2);
        DrawTextEx(ui_font, label, (Vector2){(float)line_x, (float)first_line_y}, DISCOVERY_SCREEN_FONT_SIZE,
                   DISCOVERY_SCREEN_LETTER_SPACING, normal);
        return;
    }

    for (int index = 0; index < entry_count; index++) {
        bool selected = (index == screen->cursor);
        const char *label = discovery_screen_row_text(&join_list->hosts[index]);
        float font_size = selected ? (float)(DISCOVERY_SCREEN_FONT_SIZE + DISCOVERY_SCREEN_SELECTED_FONT_BUMP)
                                   : (float)DISCOVERY_SCREEN_FONT_SIZE;
        Vector2 measured = MeasureTextEx(ui_font, label, font_size, DISCOVERY_SCREEN_LETTER_SPACING);
        int line_x = block_x + ((block_width - (int)measured.x) / 2);
        int line_y = first_line_y + (index * line_height);
        Color color = selected ? highlight : normal;
        DrawTextEx(ui_font, label, (Vector2){(float)line_x, (float)line_y}, font_size, DISCOVERY_SCREEN_LETTER_SPACING,
                   color);
    }
}

void discovery_screen_cleanup(DiscoveryScreen *screen)
{
    *screen = (DiscoveryScreen){0};
}
