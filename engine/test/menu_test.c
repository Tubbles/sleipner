#include "fff.h"
#include "unity.h"

#include "../src/menu.c" // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

/* The menu only consults the binding system; it never reads raylib
 * input directly. Faking binding_pressed alone is enough to drive
 * navigation, confirm and cancel through the unit test. */
FAKE_VALUE_FUNC(bool, binding_pressed, const EditorBinding *);
FAKE_VALUE_FUNC(bool, binding_held, const EditorBinding *);
FAKE_VALUE_FUNC(bool, binding_modifier_down, const EditorBinding *);

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

void setUp(void)
{
    RESET_FAKE(binding_pressed);
    RESET_FAKE(binding_held);
    RESET_FAKE(binding_modifier_down);
    FFF_RESET_HISTORY(); // NOLINT(bugprone-multi-level-implicit-pointer-conversion)
}

void tearDown(void) {}

/* binding_pressed fake parameterised by which action is "the one that
 * fired this frame". Only the matching MENU_ACT_* enum value returns
 * true; all others return false. */
static int target_action_for_press;
static bool press_specific_action(const EditorBinding *action)
{
    int index = (int)(action - menu_actions);
    return index == target_action_for_press;
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
    target_action_for_press = MENU_ACT_DOWN;
    binding_pressed_fake.custom_fake = press_specific_action;
    MenuAction result = menu_handle_input(&menu);
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_NONE, result);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, menu.selected);
}

void test_menu_up_retreats_selection(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_RESTORE};
    target_action_for_press = MENU_ACT_UP;
    binding_pressed_fake.custom_fake = press_specific_action;
    MenuAction result = menu_handle_input(&menu);
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_NONE, result);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_SAVE, menu.selected);
}

void test_menu_down_clamps_at_last_entry(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_QUIT};
    target_action_for_press = MENU_ACT_DOWN;
    binding_pressed_fake.custom_fake = press_specific_action;
    (void)menu_handle_input(&menu);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_QUIT, menu.selected);
}

void test_menu_up_clamps_at_first_entry(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_RESUME};
    target_action_for_press = MENU_ACT_UP;
    binding_pressed_fake.custom_fake = press_specific_action;
    (void)menu_handle_input(&menu);
    TEST_ASSERT_EQUAL_INT(MENU_ENTRY_RESUME, menu.selected);
}

/* ---- menu_handle_input dispatch ----------------------------------------- */

void test_menu_confirm_on_resume_returns_resume(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_RESUME};
    target_action_for_press = MENU_ACT_CONFIRM;
    binding_pressed_fake.custom_fake = press_specific_action;
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_RESUME, menu_handle_input(&menu));
}

void test_menu_confirm_on_save_returns_save(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_SAVE};
    target_action_for_press = MENU_ACT_CONFIRM;
    binding_pressed_fake.custom_fake = press_specific_action;
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_SAVE, menu_handle_input(&menu));
}

void test_menu_confirm_on_restore_returns_restore(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_RESTORE};
    target_action_for_press = MENU_ACT_CONFIRM;
    binding_pressed_fake.custom_fake = press_specific_action;
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_RESTORE, menu_handle_input(&menu));
}

void test_menu_confirm_on_toggle_debug_returns_toggle_debug(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_TOGGLE_DEBUG_OVERLAY};
    target_action_for_press = MENU_ACT_CONFIRM;
    binding_pressed_fake.custom_fake = press_specific_action;
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_TOGGLE_DEBUG_OVERLAY, menu_handle_input(&menu));
}

void test_menu_confirm_on_quit_returns_quit(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_QUIT};
    target_action_for_press = MENU_ACT_CONFIRM;
    binding_pressed_fake.custom_fake = press_specific_action;
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_QUIT, menu_handle_input(&menu));
}

void test_menu_cancel_returns_resume_regardless_of_selection(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_QUIT};
    target_action_for_press = MENU_ACT_CANCEL;
    binding_pressed_fake.custom_fake = press_specific_action;
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_RESUME, menu_handle_input(&menu));
}

void test_menu_handle_input_returns_none_when_no_input(void)
{
    MenuState menu = {.open = true, .selected = MENU_ENTRY_SAVE};
    binding_pressed_fake.return_val = false;
    TEST_ASSERT_EQUAL_INT(MENU_ACTION_NONE, menu_handle_input(&menu));
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
    RUN_TEST(test_menu_confirm_on_toggle_debug_returns_toggle_debug);
    RUN_TEST(test_menu_confirm_on_quit_returns_quit);
    RUN_TEST(test_menu_cancel_returns_resume_regardless_of_selection);
    RUN_TEST(test_menu_handle_input_returns_none_when_no_input);
    return UNITY_END();
}
