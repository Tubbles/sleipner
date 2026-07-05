#include "fff.h"
#include "unity.h"

#include "../src/strv.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/input.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/input_func.c" // NOLINT(bugprone-suspicious-include)
#include "../src/vec.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/settings.c"   // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

/* raylib input fakes — input.c's input_capture polls these, but these
 * tests construct InputState directly via the input_state_* helpers; the
 * fakes never fire. */
FAKE_VALUE_FUNC(int, SetGamepadMappings, const char *);
FAKE_VALUE_FUNC(bool, IsGamepadAvailable, int);
FAKE_VALUE_FUNC(float, GetGamepadAxisMovement, int, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonPressed, int, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonDown, int, int);
FAKE_VALUE_FUNC(bool, IsKeyDown, int);
FAKE_VALUE_FUNC(bool, IsKeyPressed, int);

/* raylib draw / filesystem fakes — used only by settings.c's render and
 * path-edit code paths, neither of which these General-tab-focused tests
 * exercise. Present purely to satisfy the linker. */
FAKE_VALUE_FUNC(bool, DirectoryExists, const char *);
FAKE_VALUE_FUNC(const char *, GetFileName, const char *);
FAKE_VALUE_FUNC(const char *, GetPrevDirectoryPath, const char *);
FAKE_VALUE_FUNC(const char *, GetWorkingDirectory);
FAKE_VALUE_FUNC(const char *, GetApplicationDirectory);
FAKE_VALUE_FUNC(FilePathList, LoadDirectoryFilesEx, const char *, const char *, bool);
FAKE_VOID_FUNC(UnloadDirectoryFiles, FilePathList);
FAKE_VOID_FUNC(DrawRectangle, int, int, int, int, Color);
FAKE_VOID_FUNC(DrawTextEx, Font, const char *, Vector2, float, float, Color);
FAKE_VALUE_FUNC(Vector2, MeasureTextEx, Font, const char *, float, float);

/* Cross-file fakes: blur.c / keyboard_widget.c. Settings' render and
 * path-edit keyboard-mode paths call these; General-tab nav/adjust never
 * does. */
FAKE_VOID_FUNC(blur_draw, const BlurPipeline *, Rectangle);
FAKE_VOID_FUNC(keyboard_widget_reset, KeyboardWidget *, char *, int *, int);
FAKE_VALUE_FUNC(
    KeyboardWidgetResult, keyboard_widget_handle_input, KeyboardWidget *, const InputState *, const BindingStore *);
FAKE_VOID_FUNC(keyboard_widget_draw, const KeyboardWidget *, KbScreenSize, Font);

/* debug_log stub — variadic with __attribute__((format)) */
void debug_log(DebugState *dbg, const char *format, ...)
{
    (void)dbg;
    (void)format;
}

#include "test_heap_alloc.h"

/* --- Setup / teardown ------------------------------------------------------
 *
 * These tests drive the General tab through the public settings_handle_input
 * entry point (screen == SETTINGS_SCREEN_LIST, tab == SETTINGS_TAB_GENERAL),
 * using the input_state_* test helpers to hand-build InputState frames — no
 * raylib polling, no render path. S6.13a / D32 / F31. */

static SettingsState settings;
static BindingStore store;
static Preferences preferences;
static bool close_requested;

void setUp(void)
{
    test_helpers_init();
    settings_init(&settings);
    settings.tab = SETTINGS_TAB_GENERAL;
    settings.screen = SETTINGS_SCREEN_LIST;
    store = (BindingStore){0};
    input_func_load_defaults(&store, test_heap_alloc);
    preferences = (Preferences){0};
    preferences.master_volume = PREFERENCES_VOLUME_DEFAULT;
    preferences.music_volume = PREFERENCES_VOLUME_DEFAULT;
    preferences.sfx_volume = PREFERENCES_VOLUME_DEFAULT;
    close_requested = false;
}

void tearDown(void)
{
    /* Heap allocator: releasing nested vec memory is a chore the test
     * harness skips since defaults are recreated per test. Production
     * uses an arena and reclaims on rewind. */
    for (int action_index = 0; action_index < ACTION_COUNT; action_index++) {
        for (int alt_index = 0; alt_index < store.actions[action_index].alternatives.count; alt_index++) {
            vec_atomic_input_free(&store.actions[action_index].alternatives.data[alt_index].parts);
        }
        vec_physical_input_free(&store.actions[action_index].alternatives);
    }
    for (int axis_index = 0; axis_index < AXIS_COUNT; axis_index++) {
        for (int alt_index = 0; alt_index < store.axes[axis_index].alternatives.count; alt_index++) {
            vec_atomic_input_free(&store.axes[axis_index].alternatives.data[alt_index].parts);
        }
        vec_physical_input_free(&store.axes[axis_index].alternatives);
    }
}

/* ---- Helpers -------------------------------------------------------------- */

static void press_nav_down(void)
{
    InputState frame = {0};
    input_state_press_key(&frame, KEY_DOWN);
    settings_handle_input(&settings, &frame, &store, &preferences, test_heap_alloc, &close_requested);
}

static void press_nav_left(void)
{
    InputState frame = {0};
    input_state_press_key(&frame, KEY_LEFT);
    settings_handle_input(&settings, &frame, &store, &preferences, test_heap_alloc, &close_requested);
}

static void press_nav_right(void)
{
    InputState frame = {0};
    input_state_press_key(&frame, KEY_RIGHT);
    settings_handle_input(&settings, &frame, &store, &preferences, test_heap_alloc, &close_requested);
}

/* ---- NAV_DOWN traversal (F31: the clamp is live now that there are 4 rows) */

void test_general_nav_down_traverses_all_four_rows_and_clamps(void)
{
    TEST_ASSERT_EQUAL_INT(GENERAL_ROW_DATA_DIR, settings.general_index);

    press_nav_down();
    TEST_ASSERT_EQUAL_INT(GENERAL_ROW_MASTER_VOLUME, settings.general_index);

    press_nav_down();
    TEST_ASSERT_EQUAL_INT(GENERAL_ROW_MUSIC_VOLUME, settings.general_index);

    press_nav_down();
    TEST_ASSERT_EQUAL_INT(GENERAL_ROW_SFX_VOLUME, settings.general_index);

    /* One more press must clamp at the last row (GENERAL_TOTAL_ROWS == 4
     * now that the volume rows exist), not walk off the end. */
    press_nav_down();
    TEST_ASSERT_EQUAL_INT(GENERAL_ROW_SFX_VOLUME, settings.general_index);
    TEST_ASSERT_EQUAL_INT(4, GENERAL_TOTAL_ROWS);
}

/* ---- NAV_LEFT/RIGHT volume adjustment -------------------------------------- */

void test_general_nav_left_decreases_master_volume_and_marks_dirty(void)
{
    settings.general_index = GENERAL_ROW_MASTER_VOLUME;
    TEST_ASSERT_FALSE(settings.save_preferences_requested);

    press_nav_left();

    TEST_ASSERT_EQUAL_FLOAT(0.9F, preferences.master_volume);
    TEST_ASSERT_TRUE(settings.save_preferences_requested);
}

void test_general_nav_right_increases_music_volume_and_clamps_at_max(void)
{
    settings.general_index = GENERAL_ROW_MUSIC_VOLUME;
    preferences.music_volume = 0.95F;

    press_nav_right();

    TEST_ASSERT_EQUAL_FLOAT(1.0F, preferences.music_volume);
    TEST_ASSERT_TRUE(settings.save_preferences_requested);
}

void test_general_nav_left_clamps_sfx_volume_at_min(void)
{
    settings.general_index = GENERAL_ROW_SFX_VOLUME;
    preferences.sfx_volume = 0.05F;

    press_nav_left();

    TEST_ASSERT_EQUAL_FLOAT(0.0F, preferences.sfx_volume);
    TEST_ASSERT_TRUE(settings.save_preferences_requested);
}

void test_general_nav_left_right_no_op_on_data_dir_row(void)
{
    settings.general_index = GENERAL_ROW_DATA_DIR;

    press_nav_left();
    press_nav_right();

    TEST_ASSERT_EQUAL_FLOAT(PREFERENCES_VOLUME_DEFAULT, preferences.master_volume);
    TEST_ASSERT_EQUAL_FLOAT(PREFERENCES_VOLUME_DEFAULT, preferences.music_volume);
    TEST_ASSERT_EQUAL_FLOAT(PREFERENCES_VOLUME_DEFAULT, preferences.sfx_volume);
    TEST_ASSERT_FALSE(settings.save_preferences_requested);
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();

    RUN_TEST(test_general_nav_down_traverses_all_four_rows_and_clamps);
    RUN_TEST(test_general_nav_left_decreases_master_volume_and_marks_dirty);
    RUN_TEST(test_general_nav_right_increases_music_volume_and_clamps_at_max);
    RUN_TEST(test_general_nav_left_clamps_sfx_volume_at_min);
    RUN_TEST(test_general_nav_left_right_no_op_on_data_dir_row);

    return UNITY_END();
}
