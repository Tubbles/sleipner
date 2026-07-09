#pragma once

#include "blur.h"
#include "input.h"
#include "input_func.h"
#include "network.h" // JoinList
#include "raylib.h"

#include <stdbool.h>

/* Pause-menu "Join Game" LAN discovery list (S8.3b).
 *
 * Reached from the pause menu's "Join Game" entry (MENU_ACTION_JOIN_GAME,
 * frame.c), right after frame.c's dispatch_menu_action has called
 * network_start_discovering (network.h) to bind a real listen socket and
 * flip GameState.network.mode to NET_DISCOVERING. Mirrors save_screen.h's
 * lifecycle -- modal, world frozen while open -- with one deliberate
 * difference: unlike the save slot picker's fixed-at-open-time scan, this
 * screen's row list (GameState.network.join_list) keeps changing while
 * open. frame.c's run_discovery_screen_frame drives discovery_client_tick
 * every frame the screen is open specifically so newly-beaconing hosts
 * appear and stale ones drop off live, not just at open time -- see its
 * doc comment for the full reasoning.
 *
 * This module itself never touches GameState/NetworkState directly (same
 * boundary save_screen.c draws around SaveScreen): discovery_screen_handle_input
 * takes the current JoinList by pointer and reports a confirmed row index
 * back to the caller, exactly the shape save_screen_handle_input's
 * confirmed_slot out-param already uses. frame.c is what resolves that
 * index into an actual NetAddr and flips mode to NET_JOINING. */

#define DISCOVERY_SCREEN_FONT_SIZE 48
#define DISCOVERY_SCREEN_HEADER_FONT_SIZE 56
#define DISCOVERY_SCREEN_LINE_PADDING 16
#define DISCOVERY_SCREEN_BLOCK_VPAD 32
#define DISCOVERY_SCREEN_BLOCK_MIN_WIDTH 480
#define DISCOVERY_SCREEN_HEADER_GAP 24
#define DISCOVERY_SCREEN_SELECTED_FONT_BUMP 6
#define DISCOVERY_SCREEN_LETTER_SPACING 2.0F

typedef enum {
    DISCOVERY_SCREEN_NAV_UP,
    DISCOVERY_SCREEN_NAV_DOWN,
} DiscoveryScreenNavDirection;

/* Bundles cursor + entry_count for discovery_screen_nav below -- same
 * swappable-parameters dodge as save_screen.h's SaveScreenCursor. */
typedef struct {
    int cursor;
    int entry_count;
} DiscoveryScreenCursor;

typedef struct {
    bool open;
    int cursor;
    /* Lazy blur capture, mirrors SaveScreen.blur_captured /
     * MenuState.blur_captured. */
    bool blur_captured;
} DiscoveryScreen;

void discovery_screen_init(DiscoveryScreen *screen);

/* Move `state.cursor` by one row in `direction`, clamped to
 * [0, state.entry_count) with no wrap -- identical contract to
 * save_screen_nav. Returns 0 unconditionally when state.entry_count <= 0
 * (the "Searching..." empty-list state). Pure, no raylib. */
[[nodiscard]] int discovery_screen_nav(DiscoveryScreenCursor state, DiscoveryScreenNavDirection direction);

/* Reset cursor to 0 and mark the screen open. Does not touch join_list --
 * that lives on NetworkState, populated by discovery_client_tick before
 * this screen ever sees it (network_start_discovering clears it fresh on
 * NET_DISCOVERING entry). */
void discovery_screen_open(DiscoveryScreen *screen);

/* Mark the screen closed. */
void discovery_screen_close(DiscoveryScreen *screen);

[[nodiscard]] bool discovery_screen_is_open(const DiscoveryScreen *screen);

/* Move the cursor on NAV_UP/DOWN, close on CANCEL (raises
 * *close_requested). On CONFIRM, writes the current cursor row into
 * *confirmed_index (caller must pre-set it to a sentinel like -1 --
 * this function never clears it, only sets it on an actual confirm
 * press while the list is non-empty). A CONFIRM press while join_list is
 * empty ("Searching...", nothing to select) is a no-op: *confirmed_index
 * stays untouched.
 *
 * screen->cursor is defensively re-clamped to join_list's CURRENT count
 * on every call, before processing this frame's input -- unlike
 * save_screen's fixed-at-open-time slot count, join_list can shrink
 * between frames (a host's beacon timing out, network.c's
 * join_list_evict_timed_out) while this screen sits open with no nav
 * input at all, and an unclamped cursor would then index past the
 * shrunk list the next time render or a confirm reads it. */
void discovery_screen_handle_input(DiscoveryScreen *screen,
                                   const InputState *input,
                                   const BindingStore *bindings,
                                   const JoinList *join_list,
                                   bool *close_requested,
                                   int *confirmed_index);

/* Draw the blurred backdrop, a vignette panel, a "JOIN GAME" header, and
 * one row per discovered host ("<name>  <ip>:<port>"), highlighting the
 * cursor row. An empty join_list renders a single centered "Searching..."
 * line instead of an empty panel. Caller is responsible for being inside
 * BeginDrawing(). */
void discovery_screen_render(const DiscoveryScreen *screen,
                             const JoinList *join_list,
                             Font ui_font,
                             const BlurPipeline *blur,
                             int screen_width,
                             int screen_height);

/* Reset to a zero-initialised state. Safe on an already zero-initialised
 * screen. */
void discovery_screen_cleanup(DiscoveryScreen *screen);
