#include "fff.h"
#include "unity.h"

#include "blur.h"
#include "raylib.h"

DEFINE_FFF_GLOBALS;

/* save_screen_handle_input reads through input_pressed, same as
 * inventory_screen_handle_input/menu_handle_input -- construct
 * InputState directly via the input_state_* helpers rather than driving
 * these fakes; they exist only to satisfy the linker (input.c's
 * input_capture polls them, unused by these tests). */
FAKE_VALUE_FUNC(int, SetGamepadMappings, const char *);
FAKE_VALUE_FUNC(bool, IsGamepadAvailable, int);
FAKE_VALUE_FUNC(float, GetGamepadAxisMovement, int, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonPressed, int, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonDown, int, int);
FAKE_VALUE_FUNC(bool, IsKeyDown, int);
FAKE_VALUE_FUNC(bool, IsKeyPressed, int);

/* save_screen_open scans slots via FileExists -- fake it to control
 * which slots/autosave "exist" per test. */
FAKE_VALUE_FUNC(bool, FileExists, const char *);

/* raylib draw fakes -- used only by save_screen_render, which these
 * pure-core-focused tests never call. Present purely to satisfy the
 * linker, same rationale as inventory_screen_test.c/menu_test.c. */
FAKE_VOID_FUNC(DrawRectangle, int, int, int, int, Color);
FAKE_VOID_FUNC(DrawTextEx, Font, const char *, Vector2, float, float, Color);
FAKE_VALUE_FUNC(Vector2, MeasureTextEx, Font, const char *, float, float);
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

#include "../src/strv.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/error.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/arena_posix.c" // NOLINT(bugprone-suspicious-include)
#include "../src/input.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/input_func.c"  // NOLINT(bugprone-suspicious-include)
#include "../src/save_screen.c" // NOLINT(bugprone-suspicious-include)

#include "test_heap_alloc.h"

static BindingStore store;

void setUp(void)
{
    test_helpers_init();
    RESET_FAKE(FileExists);
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

/* ---- save_screen_entry_count / save_screen_entry_is_autosave ------------ */

void test_entry_count_save_mode_is_slot_count(void)
{
    TEST_ASSERT_EQUAL_INT(SAVE_SLOT_COUNT, save_screen_entry_count(SAVE_SCREEN_MODE_SAVE));
}

void test_entry_count_load_mode_adds_autosave_row(void)
{
    TEST_ASSERT_EQUAL_INT(SAVE_SLOT_COUNT + 1, save_screen_entry_count(SAVE_SCREEN_MODE_LOAD));
}

void test_entry_is_autosave_true_at_slot_count_index(void)
{
    TEST_ASSERT_TRUE(save_screen_entry_is_autosave(SAVE_SLOT_COUNT));
}

void test_entry_is_autosave_false_for_numbered_slots(void)
{
    for (int index = 0; index < SAVE_SLOT_COUNT; index++) {
        TEST_ASSERT_FALSE(save_screen_entry_is_autosave(index));
    }
}

/* ---- save_screen_nav ----------------------------------------------------- */

void test_nav_down_advances_cursor(void)
{
    TEST_ASSERT_EQUAL_INT(1, save_screen_nav((SaveScreenCursor){.cursor = 0, .entry_count = 4}, SAVE_SCREEN_NAV_DOWN));
}

void test_nav_down_clamps_at_last_entry(void)
{
    TEST_ASSERT_EQUAL_INT(3, save_screen_nav((SaveScreenCursor){.cursor = 3, .entry_count = 4}, SAVE_SCREEN_NAV_DOWN));
}

void test_nav_up_retreats_cursor(void)
{
    TEST_ASSERT_EQUAL_INT(1, save_screen_nav((SaveScreenCursor){.cursor = 2, .entry_count = 4}, SAVE_SCREEN_NAV_UP));
}

void test_nav_up_clamps_at_first_entry(void)
{
    TEST_ASSERT_EQUAL_INT(0, save_screen_nav((SaveScreenCursor){.cursor = 0, .entry_count = 4}, SAVE_SCREEN_NAV_UP));
}

void test_nav_on_zero_entries_is_noop(void)
{
    TEST_ASSERT_EQUAL_INT(0, save_screen_nav((SaveScreenCursor){.cursor = 0, .entry_count = 0}, SAVE_SCREEN_NAV_DOWN));
}

/* ---- save_screen_slot_path / save_screen_autosave_path ------------------- */

void test_slot_path_first_slot_is_one_based(void)
{
    Str path = save_screen_slot_path(strv_from_cstr("/saves/"), 0, test_heap_alloc);
    TEST_ASSERT_EQUAL_STRING("/saves/save_1.toml", path.ptr);
    str_free(&path);
}

void test_slot_path_third_slot(void)
{
    Str path = save_screen_slot_path(strv_from_cstr("/saves/"), 2, test_heap_alloc);
    TEST_ASSERT_EQUAL_STRING("/saves/save_3.toml", path.ptr);
    str_free(&path);
}

void test_autosave_path_appends_fixed_filename(void)
{
    Str path = save_screen_autosave_path(strv_from_cstr("/saves/"), test_heap_alloc);
    TEST_ASSERT_EQUAL_STRING("/saves/autosave.toml", path.ptr);
    str_free(&path);
}

/* ---- save_screen_open -----------------------------------------------------
 *
 * save_screen_open builds and discards a transient Str per slot probed --
 * fine against an arena in production (orphaned bytes are reclaimed on the
 * next rewind, see CLAUDE.md's arena growth rules) but a real leak under
 * ASan/LSan if called with test_heap_alloc, which has no such reclaim.
 * These two tests use a scratch Arena (arena_posix.c, reset at the end)
 * instead, mirroring editor_widgets_test.c's own arena_init/arena_reset
 * pattern for the same reason. */

void test_open_resets_cursor_and_sets_mode(void)
{
    ErrorState err_state = {0};
    Arena arena = {0};
    TEST_ASSERT_TRUE(arena_init(&err_state, &arena));

    SaveScreen screen = {.cursor = 2};
    FileExists_fake.return_val = false;

    save_screen_open(&screen, SAVE_SCREEN_MODE_LOAD, strv_from_cstr("/saves/"), allocator_arena(&arena));

    TEST_ASSERT_TRUE(screen.open);
    TEST_ASSERT_EQUAL_INT(SAVE_SCREEN_MODE_LOAD, screen.mode);
    TEST_ASSERT_EQUAL_INT(0, screen.cursor);

    arena_reset(&arena);
}

/* FileExists is called once per numbered slot (in order) then once more
 * for the autosave file -- SET_RETURN_SEQ drives that fixed call order
 * to mark slot 1 and the autosave as present, slots 2/3 absent. */
void test_open_scans_every_slot_and_autosave(void)
{
    static bool sequence[SAVE_SLOT_COUNT + 1] = {true, false, false, true};
    SET_RETURN_SEQ(FileExists, sequence, SAVE_SLOT_COUNT + 1);

    ErrorState err_state = {0};
    Arena arena = {0};
    TEST_ASSERT_TRUE(arena_init(&err_state, &arena));

    SaveScreen screen = {0};
    save_screen_open(&screen, SAVE_SCREEN_MODE_LOAD, strv_from_cstr("/saves/"), allocator_arena(&arena));

    TEST_ASSERT_TRUE(screen.slot_exists[0]);
    TEST_ASSERT_FALSE(screen.slot_exists[1]);
    TEST_ASSERT_FALSE(screen.slot_exists[2]);
    TEST_ASSERT_TRUE(screen.autosave_exists);
    TEST_ASSERT_EQUAL_INT(SAVE_SLOT_COUNT + 1, FileExists_fake.call_count);

    arena_reset(&arena);
}

/* ---- save_screen_entry_exists --------------------------------------------- */

void test_entry_exists_reads_slot_exists_for_numbered_slot(void)
{
    SaveScreen screen = {.slot_exists = {true, false, false}};
    TEST_ASSERT_TRUE(save_screen_entry_exists(&screen, 0));
    TEST_ASSERT_FALSE(save_screen_entry_exists(&screen, 1));
}

void test_entry_exists_reads_autosave_flag_for_autosave_row(void)
{
    SaveScreen screen = {.autosave_exists = true};
    TEST_ASSERT_TRUE(save_screen_entry_exists(&screen, SAVE_SLOT_COUNT));
}

/* ---- save_screen_handle_input --------------------------------------------- */

void test_handle_input_down_advances_cursor(void)
{
    SaveScreen screen = {.open = true, .mode = SAVE_SCREEN_MODE_LOAD, .cursor = 0};
    InputState input = {0};
    input_state_press_key(&input, KEY_DOWN);
    bool close_requested = false;
    int confirmed_slot = -1;

    save_screen_handle_input(&screen, &input, &store, &close_requested, &confirmed_slot);

    TEST_ASSERT_EQUAL_INT(1, screen.cursor);
    TEST_ASSERT_FALSE(close_requested);
    TEST_ASSERT_EQUAL_INT(-1, confirmed_slot);
}

void test_handle_input_cancel_requests_close(void)
{
    SaveScreen screen = {.open = true, .mode = SAVE_SCREEN_MODE_SAVE, .cursor = 1};
    InputState input = {0};
    input_state_press_key(&input, KEY_ESCAPE);
    bool close_requested = false;
    int confirmed_slot = -1;

    save_screen_handle_input(&screen, &input, &store, &close_requested, &confirmed_slot);

    TEST_ASSERT_TRUE(close_requested);
    TEST_ASSERT_EQUAL_INT(-1, confirmed_slot);
}

void test_handle_input_confirm_reports_cursor_slot(void)
{
    SaveScreen screen = {.open = true, .mode = SAVE_SCREEN_MODE_SAVE, .cursor = 2};
    InputState input = {0};
    input_state_press_key(&input, KEY_ENTER);
    bool close_requested = false;
    int confirmed_slot = -1;

    save_screen_handle_input(&screen, &input, &store, &close_requested, &confirmed_slot);

    TEST_ASSERT_FALSE(close_requested);
    TEST_ASSERT_EQUAL_INT(2, confirmed_slot);
}

void test_handle_input_no_input_leaves_confirmed_slot_untouched(void)
{
    SaveScreen screen = {.open = true, .mode = SAVE_SCREEN_MODE_SAVE, .cursor = 1};
    InputState input = {0};
    bool close_requested = false;
    int confirmed_slot = -1;

    save_screen_handle_input(&screen, &input, &store, &close_requested, &confirmed_slot);

    TEST_ASSERT_EQUAL_INT(-1, confirmed_slot);
    TEST_ASSERT_EQUAL_INT(1, screen.cursor);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_entry_count_save_mode_is_slot_count);
    RUN_TEST(test_entry_count_load_mode_adds_autosave_row);
    RUN_TEST(test_entry_is_autosave_true_at_slot_count_index);
    RUN_TEST(test_entry_is_autosave_false_for_numbered_slots);
    RUN_TEST(test_nav_down_advances_cursor);
    RUN_TEST(test_nav_down_clamps_at_last_entry);
    RUN_TEST(test_nav_up_retreats_cursor);
    RUN_TEST(test_nav_up_clamps_at_first_entry);
    RUN_TEST(test_nav_on_zero_entries_is_noop);
    RUN_TEST(test_slot_path_first_slot_is_one_based);
    RUN_TEST(test_slot_path_third_slot);
    RUN_TEST(test_autosave_path_appends_fixed_filename);
    RUN_TEST(test_open_resets_cursor_and_sets_mode);
    RUN_TEST(test_open_scans_every_slot_and_autosave);
    RUN_TEST(test_entry_exists_reads_slot_exists_for_numbered_slot);
    RUN_TEST(test_entry_exists_reads_autosave_flag_for_autosave_row);
    RUN_TEST(test_handle_input_down_advances_cursor);
    RUN_TEST(test_handle_input_cancel_requests_close);
    RUN_TEST(test_handle_input_confirm_reports_cursor_slot);
    RUN_TEST(test_handle_input_no_input_leaves_confirmed_slot_untouched);
    return UNITY_END();
}
