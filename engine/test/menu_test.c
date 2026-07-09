#include "fff.h"
#include "unity.h"

#include "raylib.h"

DEFINE_FFF_GLOBALS;

/* menu_handle_input now consults input_func, not binding_pressed. The
 * test constructs an InputState directly via the helpers in input.h and
 * passes a defaults-loaded BindingStore. */

FAKE_VALUE_FUNC(int, SetGamepadMappings, const char *);
FAKE_VALUE_FUNC(bool, IsGamepadAvailable, int);
FAKE_VALUE_FUNC(float, GetGamepadAxisMovement, int, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonPressed, int, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonDown, int, int);
FAKE_VALUE_FUNC(bool, IsKeyDown, int);
FAKE_VALUE_FUNC(bool, IsKeyPressed, int);

#include "debug.h"
void debug_log(DebugState *dbg, const char *format, ...)
{
    (void)dbg;
    (void)format;
}

#include "../src/input.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/input_func.c" // NOLINT(bugprone-suspicious-include)
#include "../src/menu.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/vec.c"        // NOLINT(bugprone-suspicious-include)

#include "test_heap_alloc.h"

/* menu.c includes blur.h for menu_render but only references
 * blur_draw at runtime. The unit tests never call menu_render, so a
 * stub keeps the symbol resolved without dragging in raylib state. */
void blur_draw(const BlurPipeline *blur, Rectangle dst)
{
    (void)blur;
    (void)dst;
}

/* Raylib draw / font primitives used by menu_render and menu_cleanup.
 * The unit tests never exercise rendering; stubs keep the symbols
 * resolved without linking raylib into the unit-test binary. The
 * NOLINT covers the easily-swappable-parameters warning that fires on
 * raylib's own API shape — we cannot change the upstream signatures. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
void DrawRectangle(int posX, int posY, int width, int height, Color color)
{
    (void)posX;
    (void)posY;
    (void)width;
    (void)height;
    (void)color;
}
Vector2 MeasureTextEx(Font font, const char *text, float fontSize, float spacing)
{
    (void)font;
    (void)text;
    (void)fontSize;
    (void)spacing;
    return (Vector2){0, 0};
}
void DrawTextEx(Font font, const char *text, Vector2 position, float fontSize, float spacing, Color tint)
{
    (void)font;
    (void)text;
    (void)position;
    (void)fontSize;
    (void)spacing;
    (void)tint;
}
/* NOLINTEND(bugprone-easily-swappable-parameters) */
void UnloadFont(Font font)
{
    (void)font;
}

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

/* ---- menu_open / menu_close --------------------------------------------- */

void test_menu_open_resets_selection_to_resume(void)
{
    MenuState menu = {0};
    menu_init(&menu);
    menu.selected = MENU_ENTRY_QUIT;
    menu_open(&menu);
    TEST_ASSERT_TRUE(menu.open);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_RESUME, menu.selected);
}

void test_menu_close_clears_open_flag(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_SAVE};
    menu_close(&menu);
    TEST_ASSERT_FALSE(menu.open);
}

/* ---- menu_handle_input navigation --------------------------------------- */

void test_menu_down_advances_selection(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_RESUME};
    InputState input = {0};
    input_state_press_key(&input, KEY_DOWN);
    MenuAction result = menu_handle_input(&menu, &input, &store);
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_NONE, result);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, menu.selected);
}

void test_menu_up_retreats_selection(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_RESTORE};
    InputState input = {0};
    input_state_press_key(&input, KEY_UP);
    MenuAction result = menu_handle_input(&menu, &input, &store);
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_NONE, result);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, menu.selected);
}

void test_menu_down_clamps_at_last_entry(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_QUIT};
    InputState input = {0};
    input_state_press_key(&input, KEY_DOWN);
    (void)menu_handle_input(&menu, &input, &store);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_QUIT, menu.selected);
}

void test_menu_up_clamps_at_first_entry(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_RESUME};
    InputState input = {0};
    input_state_press_key(&input, KEY_UP);
    (void)menu_handle_input(&menu, &input, &store);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_RESUME, menu.selected);
}

/* ---- menu_handle_input dispatch ----------------------------------------- */

void test_menu_confirm_on_resume_returns_resume(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_RESUME};
    InputState input = {0};
    input_state_press_key(&input, KEY_ENTER);
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_RESUME, menu_handle_input(&menu, &input, &store));
}

void test_menu_confirm_on_save_returns_save(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_SAVE};
    InputState input = {0};
    input_state_press_key(&input, KEY_ENTER);
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_SAVE, menu_handle_input(&menu, &input, &store));
}

void test_menu_confirm_on_restore_returns_restore(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_RESTORE};
    InputState input = {0};
    input_state_press_key(&input, KEY_ENTER);
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_RESTORE, menu_handle_input(&menu, &input, &store));
}

void test_menu_confirm_on_save_game_returns_open_save_menu(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_SAVE_GAME};
    InputState input = {0};
    input_state_press_key(&input, KEY_ENTER);
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_OPEN_SAVE_MENU, menu_handle_input(&menu, &input, &store));
}

void test_menu_confirm_on_load_game_returns_open_load_menu(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_LOAD_GAME};
    InputState input = {0};
    input_state_press_key(&input, KEY_ENTER);
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_OPEN_LOAD_MENU, menu_handle_input(&menu, &input, &store));
}

void test_menu_confirm_on_host_game_returns_host_game(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_HOST_GAME};
    InputState input = {0};
    input_state_press_key(&input, KEY_ENTER);
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_HOST_GAME, menu_handle_input(&menu, &input, &store));
}

void test_menu_confirm_on_join_game_returns_join_game(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_JOIN_GAME};
    InputState input = {0};
    input_state_press_key(&input, KEY_ENTER);
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_JOIN_GAME, menu_handle_input(&menu, &input, &store));
}

void test_menu_confirm_on_toggle_debug_returns_toggle_debug(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_TOGGLE_DEBUG_OVERLAY};
    InputState input = {0};
    input_state_press_key(&input, KEY_ENTER);
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_TOGGLE_DEBUG_OVERLAY, menu_handle_input(&menu, &input, &store));
}

void test_menu_confirm_on_quit_returns_quit(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_QUIT};
    InputState input = {0};
    input_state_press_key(&input, KEY_ENTER);
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_QUIT, menu_handle_input(&menu, &input, &store));
}

void test_menu_cancel_returns_resume_regardless_of_selection(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_QUIT};
    InputState input = {0};
    input_state_press_key(&input, KEY_ESCAPE);
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_RESUME, menu_handle_input(&menu, &input, &store));
}

void test_menu_handle_input_returns_none_when_no_input(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_SAVE};
    InputState input = {0};
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_NONE, menu_handle_input(&menu, &input, &store));
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, menu.selected);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_menu_open_resets_selection_to_resume);
    RUN_TEST(test_menu_close_clears_open_flag);
    RUN_TEST(test_menu_down_advances_selection);
    RUN_TEST(test_menu_up_retreats_selection);
    RUN_TEST(test_menu_down_clamps_at_last_entry);
    RUN_TEST(test_menu_up_clamps_at_first_entry);
    RUN_TEST(test_menu_confirm_on_resume_returns_resume);
    RUN_TEST(test_menu_confirm_on_save_returns_save);
    RUN_TEST(test_menu_confirm_on_restore_returns_restore);
    RUN_TEST(test_menu_confirm_on_save_game_returns_open_save_menu);
    RUN_TEST(test_menu_confirm_on_load_game_returns_open_load_menu);
    RUN_TEST(test_menu_confirm_on_host_game_returns_host_game);
    RUN_TEST(test_menu_confirm_on_join_game_returns_join_game);
    RUN_TEST(test_menu_confirm_on_toggle_debug_returns_toggle_debug);
    RUN_TEST(test_menu_confirm_on_quit_returns_quit);
    RUN_TEST(test_menu_cancel_returns_resume_regardless_of_selection);
    RUN_TEST(test_menu_handle_input_returns_none_when_no_input);
    return UNITY_END();
}
