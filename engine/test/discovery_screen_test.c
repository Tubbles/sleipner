#include "fff.h"
#include "unity.h"

#include "blur.h"
#include "raylib.h"

DEFINE_FFF_GLOBALS;

/* discovery_screen_handle_input reads through input_pressed, same as
 * save_screen_handle_input/menu_handle_input -- construct InputState
 * directly via the input_state_* helpers rather than driving these
 * fakes; they exist only to satisfy the linker (input.c's input_capture
 * polls them, unused by these tests). */
FAKE_VALUE_FUNC(int, SetGamepadMappings, const char *);
FAKE_VALUE_FUNC(bool, IsGamepadAvailable, int);
FAKE_VALUE_FUNC(float, GetGamepadAxisMovement, int, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonPressed, int, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonDown, int, int);
FAKE_VALUE_FUNC(bool, IsKeyDown, int);
FAKE_VALUE_FUNC(bool, IsKeyPressed, int);

/* raylib draw fakes -- used only by discovery_screen_render, which these
 * pure-core-focused tests never call. Present purely to satisfy the
 * linker, same rationale as save_screen_test.c/menu_test.c. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
FAKE_VOID_FUNC(DrawRectangle, int, int, int, int, Color);
FAKE_VOID_FUNC(DrawTextEx, Font, const char *, Vector2, float, float, Color);
FAKE_VALUE_FUNC(Vector2, MeasureTextEx, Font, const char *, float, float);
/* NOLINTEND(bugprone-easily-swappable-parameters) */
FAKE_VOID_FUNC(blur_draw, const BlurPipeline *, Rectangle);

/* TextFormat stub -- variadic, cannot use FAKE_VALUE_FUNC. */
const char *TextFormat(const char *text, ...)
{
    (void)text;
    return "";
}

#include "debug.h"
void debug_log(DebugState *dbg, const char *format, ...)
{
    (void)dbg;
    (void)format;
}

#include "../src/discovery_screen.c" // NOLINT(bugprone-suspicious-include)
#include "../src/input.c"            // NOLINT(bugprone-suspicious-include)
#include "../src/input_func.c"       // NOLINT(bugprone-suspicious-include)

#include "test_heap_alloc.h"

#include <stdint.h>
#include <stdio.h>

static BindingStore store;

void setUp(void)
{
    test_helpers_init();
    store = (BindingStore){0};
    input_func_load_defaults(&store, test_heap_alloc);
}

void tearDown(void)
{
    for (int act = 0; act < ACTION_COUNT; act++) {
        for (int alt = 0; alt < store.actions[act].alternatives.count; alt++) {
            vec_atomic_input_free(&store.actions[act].alternatives.data[alt].parts);
        }
        vec_physical_input_free(&store.actions[act].alternatives);
    }
    for (int axis = 0; axis < AXIS_COUNT; axis++) {
        for (int alt = 0; alt < store.axes[axis].alternatives.count; alt++) {
            vec_atomic_input_free(&store.axes[axis].alternatives.data[alt].parts);
        }
        vec_physical_input_free(&store.axes[axis].alternatives);
    }
}

static JoinList make_join_list(int count)
{
    JoinList list = {0};
    for (int index = 0; index < count; index++) {
        list.hosts[index].addr = net_addr_make((uint32_t)(1000 + index), (uint16_t)(9000 + index));
        (void)snprintf(list.hosts[index].name, sizeof(list.hosts[index].name), "Host%d", index);
    }
    list.count = count;
    return list;
}

/* ---- discovery_screen_nav ------------------------------------------------ */

void test_nav_down_advances_cursor(void)
{
    TEST_ASSERT_EQUAL_INT(
        1, discovery_screen_nav((DiscoveryScreenCursor){.cursor = 0, .entry_count = 4}, DISCOVERY_SCREEN_NAV_DOWN));
}

void test_nav_down_clamps_at_last_entry(void)
{
    TEST_ASSERT_EQUAL_INT(
        3, discovery_screen_nav((DiscoveryScreenCursor){.cursor = 3, .entry_count = 4}, DISCOVERY_SCREEN_NAV_DOWN));
}

void test_nav_up_retreats_cursor(void)
{
    TEST_ASSERT_EQUAL_INT(
        1, discovery_screen_nav((DiscoveryScreenCursor){.cursor = 2, .entry_count = 4}, DISCOVERY_SCREEN_NAV_UP));
}

void test_nav_up_clamps_at_first_entry(void)
{
    TEST_ASSERT_EQUAL_INT(
        0, discovery_screen_nav((DiscoveryScreenCursor){.cursor = 0, .entry_count = 4}, DISCOVERY_SCREEN_NAV_UP));
}

void test_nav_on_zero_entries_is_noop(void)
{
    TEST_ASSERT_EQUAL_INT(
        0, discovery_screen_nav((DiscoveryScreenCursor){.cursor = 0, .entry_count = 0}, DISCOVERY_SCREEN_NAV_DOWN));
}

/* ---- discovery_screen_open ------------------------------------------------ */

void test_open_resets_cursor_and_sets_open(void)
{
    DiscoveryScreen screen = {.cursor = 3};
    discovery_screen_open(&screen);
    TEST_ASSERT_TRUE(screen.open);
    TEST_ASSERT_EQUAL_INT(0, screen.cursor);
}

/* ---- discovery_screen_handle_input ---------------------------------------- */

void test_handle_input_down_advances_cursor(void)
{
    DiscoveryScreen screen = {.open = true, .cursor = 0};
    JoinList list = make_join_list(3);
    InputState input = {0};
    input_state_press_key(&input, KEY_DOWN);
    bool close_requested = false;
    int confirmed_index = -1;

    discovery_screen_handle_input(&screen, &input, &store, &list, &close_requested, &confirmed_index);

    TEST_ASSERT_EQUAL_INT(1, screen.cursor);
    TEST_ASSERT_FALSE(close_requested);
    TEST_ASSERT_EQUAL_INT(-1, confirmed_index);
}

void test_handle_input_cancel_requests_close(void)
{
    DiscoveryScreen screen = {.open = true, .cursor = 1};
    JoinList list = make_join_list(2);
    InputState input = {0};
    input_state_press_key(&input, KEY_ESCAPE);
    bool close_requested = false;
    int confirmed_index = -1;

    discovery_screen_handle_input(&screen, &input, &store, &list, &close_requested, &confirmed_index);

    TEST_ASSERT_TRUE(close_requested);
    TEST_ASSERT_EQUAL_INT(-1, confirmed_index);
}

void test_handle_input_confirm_reports_cursor_index(void)
{
    DiscoveryScreen screen = {.open = true, .cursor = 1};
    JoinList list = make_join_list(2);
    InputState input = {0};
    input_state_press_key(&input, KEY_ENTER);
    bool close_requested = false;
    int confirmed_index = -1;

    discovery_screen_handle_input(&screen, &input, &store, &list, &close_requested, &confirmed_index);

    TEST_ASSERT_FALSE(close_requested);
    TEST_ASSERT_EQUAL_INT(1, confirmed_index);
}

/* CONFIRM on an empty ("Searching...") list has nothing to select --
 * must be a no-op, not report cursor 0 as if a real host had been
 * chosen. */
void test_handle_input_confirm_on_empty_list_is_noop(void)
{
    DiscoveryScreen screen = {.open = true, .cursor = 0};
    JoinList list = {0};
    InputState input = {0};
    input_state_press_key(&input, KEY_ENTER);
    bool close_requested = false;
    int confirmed_index = -1;

    discovery_screen_handle_input(&screen, &input, &store, &list, &close_requested, &confirmed_index);

    TEST_ASSERT_EQUAL_INT(-1, confirmed_index);
}

void test_handle_input_no_input_leaves_confirmed_index_untouched(void)
{
    DiscoveryScreen screen = {.open = true, .cursor = 1};
    JoinList list = make_join_list(2);
    InputState input = {0};
    bool close_requested = false;
    int confirmed_index = -1;

    discovery_screen_handle_input(&screen, &input, &store, &list, &close_requested, &confirmed_index);

    TEST_ASSERT_EQUAL_INT(-1, confirmed_index);
    TEST_ASSERT_EQUAL_INT(1, screen.cursor);
}

/* The join list can shrink between frames (a host's beacon timing out,
 * network.c's join_list_evict_timed_out) while the screen sits open with
 * no nav input at all -- the cursor must re-clamp defensively every
 * call, not just on a NAV press, or a later render/confirm would index
 * past the shrunk list. */
void test_handle_input_clamps_cursor_when_list_shrinks(void)
{
    DiscoveryScreen screen = {.open = true, .cursor = 3};
    JoinList list = make_join_list(1);
    InputState input = {0};
    bool close_requested = false;
    int confirmed_index = -1;

    discovery_screen_handle_input(&screen, &input, &store, &list, &close_requested, &confirmed_index);

    TEST_ASSERT_EQUAL_INT(0, screen.cursor);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nav_down_advances_cursor);
    RUN_TEST(test_nav_down_clamps_at_last_entry);
    RUN_TEST(test_nav_up_retreats_cursor);
    RUN_TEST(test_nav_up_clamps_at_first_entry);
    RUN_TEST(test_nav_on_zero_entries_is_noop);
    RUN_TEST(test_open_resets_cursor_and_sets_open);
    RUN_TEST(test_handle_input_down_advances_cursor);
    RUN_TEST(test_handle_input_cancel_requests_close);
    RUN_TEST(test_handle_input_confirm_reports_cursor_index);
    RUN_TEST(test_handle_input_confirm_on_empty_list_is_noop);
    RUN_TEST(test_handle_input_no_input_leaves_confirmed_index_untouched);
    RUN_TEST(test_handle_input_clamps_cursor_when_list_shrinks);
    return UNITY_END();
}
