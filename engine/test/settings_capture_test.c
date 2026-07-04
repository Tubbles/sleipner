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
 * path-edit code paths, neither of which these capture-focused tests
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
 * path-edit keyboard-mode paths call these; capture logic never does. */
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
 * These tests drive handle_capture_action / handle_capture_axis_first
 * directly against a real BindingStore, using the input_state_* test
 * helpers to hand-build InputState frames — no raylib polling, no
 * screen/render path. */

static SettingsState settings;
static BindingStore store;

void setUp(void)
{
    test_helpers_init();
    settings_init(&settings);
    store = (BindingStore){0};
    input_func_load_defaults(&store, test_heap_alloc);
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

static bool physical_contains(const PhysicalInput *physical, AtomKind kind, int int_a)
{
    for (int index = 0; index < physical->parts.count; index++) {
        const AtomicInput *atom = &physical->parts.data[index];
        if (atom->kind == kind && atom->int_a == int_a) {
            return true;
        }
    }
    return false;
}

static const PhysicalInput *last_action_alternative(InputAction action)
{
    int last_index = store.actions[action].alternatives.count - 1;
    return &store.actions[action].alternatives.data[last_index];
}

static const PhysicalInput *last_axis_alternative(InputAxis axis)
{
    int last_index = store.axes[axis].alternatives.count - 1;
    return &store.axes[axis].alternatives.data[last_index];
}

/* ---- ACTION-mode capture: opening-press exclusion ------------------------- */

/* The key that opens capture (ACTION_CONFIRM = ENTER by default) must not
 * leak into the bound chord: enter_capture snapshots it into
 * capture_prev_held, so it reads as "already held", not a fresh edge. */
void test_capture_action_excludes_opening_press(void)
{
    settings.target_kind = SETTINGS_TARGET_ACTION;
    settings.target_index = ACTION_CONFIRM;

    InputState open_frame = {0};
    input_state_hold_key(&open_frame, KEY_ENTER); /* the CONFIRM press that opened capture */
    enter_capture(&settings, &open_frame, -1);
    TEST_ASSERT_EQUAL_INT(1, settings.capture_prev_held_count);

    InputState still_held_plus_new_press = {0};
    input_state_hold_key(&still_held_plus_new_press, KEY_ENTER); /* not a fresh edge */
    input_state_hold_key(&still_held_plus_new_press, KEY_F1);    /* fresh edge */
    handle_capture_action(&settings, &still_held_plus_new_press, &store, test_heap_alloc);
    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_CAPTURE, (int)settings.screen); /* still held, no finalize */

    InputState release_all = {0};
    handle_capture_action(&settings, &release_all, &store, test_heap_alloc);

    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_DETAIL, (int)settings.screen);
    const PhysicalInput *bound = last_action_alternative(ACTION_CONFIRM);
    TEST_ASSERT_EQUAL_INT(1, bound->parts.count);
    TEST_ASSERT_TRUE(physical_contains(bound, ATOM_KEY, KEY_F1));
    TEST_ASSERT_FALSE(physical_contains(bound, ATOM_KEY, KEY_ENTER));
}

/* ---- ACTION-mode capture: re-press of the opening key ---------------------- */

/* A later, fresh press of the same key that opened capture (after a full
 * release) IS a real edge and can be bound — proving the exclusion above is
 * frame-by-frame edge detection, not a permanent baseline exclusion. */
void test_capture_action_includes_repress_of_opening_key(void)
{
    settings.target_kind = SETTINGS_TARGET_ACTION;
    settings.target_index = ACTION_CONFIRM;

    InputState open_frame = {0};
    input_state_hold_key(&open_frame, KEY_ENTER);
    enter_capture(&settings, &open_frame, -1);

    InputState released = {0}; /* ENTER let go, nothing held yet */
    handle_capture_action(&settings, &released, &store, test_heap_alloc);
    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_CAPTURE, (int)settings.screen); /* chord empty, no finalize */
    TEST_ASSERT_EQUAL_INT(0, settings.capture_chord_count);

    InputState repress = {0};
    input_state_hold_key(&repress, KEY_ENTER); /* re-press: a fresh edge now */
    input_state_hold_key(&repress, KEY_F1);
    handle_capture_action(&settings, &repress, &store, test_heap_alloc);
    TEST_ASSERT_EQUAL_INT(2, settings.capture_chord_count);

    InputState release_all = {0};
    handle_capture_action(&settings, &release_all, &store, test_heap_alloc);

    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_DETAIL, (int)settings.screen);
    const PhysicalInput *bound = last_action_alternative(ACTION_CONFIRM);
    TEST_ASSERT_EQUAL_INT(2, bound->parts.count);
    TEST_ASSERT_TRUE(physical_contains(bound, ATOM_KEY, KEY_ENTER));
    TEST_ASSERT_TRUE(physical_contains(bound, ATOM_KEY, KEY_F1));
}

/* ---- ACTION-mode capture: out-of-order release ----------------------------- */

/* Press A, then press B alongside it, then release them in the opposite
 * order (A first, B second). The chord is {A, B} regardless of release
 * order, and only finalizes once the held set is fully empty. */
void test_capture_action_out_of_order_release(void)
{
    settings.target_kind = SETTINGS_TARGET_ACTION;
    settings.target_index = ACTION_CONFIRM;

    InputState open_frame = {0}; /* opened with nothing held (e.g. a gamepad alt) */
    enter_capture(&settings, &open_frame, -1);

    InputState press_first = {0};
    input_state_hold_key(&press_first, KEY_F1);
    handle_capture_action(&settings, &press_first, &store, test_heap_alloc);

    InputState press_second = {0};
    input_state_hold_key(&press_second, KEY_F1);
    input_state_hold_key(&press_second, KEY_F2);
    handle_capture_action(&settings, &press_second, &store, test_heap_alloc);
    TEST_ASSERT_EQUAL_INT(2, settings.capture_chord_count);

    InputState release_first_key = {0};
    input_state_hold_key(&release_first_key, KEY_F2); /* F1 released, F2 still held */
    handle_capture_action(&settings, &release_first_key, &store, test_heap_alloc);
    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_CAPTURE, (int)settings.screen); /* one atom still held */

    InputState release_all = {0};
    handle_capture_action(&settings, &release_all, &store, test_heap_alloc);

    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_DETAIL, (int)settings.screen);
    const PhysicalInput *bound = last_action_alternative(ACTION_CONFIRM);
    TEST_ASSERT_EQUAL_INT(2, bound->parts.count);
    TEST_ASSERT_TRUE(physical_contains(bound, ATOM_KEY, KEY_F1));
    TEST_ASSERT_TRUE(physical_contains(bound, ATOM_KEY, KEY_F2));
}

/* ---- ACTION-mode capture: single key --------------------------------------- */

void test_capture_action_single_key(void)
{
    settings.target_kind = SETTINGS_TARGET_ACTION;
    settings.target_index = ACTION_CONFIRM;

    InputState open_frame = {0};
    enter_capture(&settings, &open_frame, -1);

    InputState press = {0};
    input_state_hold_key(&press, KEY_F1);
    handle_capture_action(&settings, &press, &store, test_heap_alloc);

    InputState release = {0};
    handle_capture_action(&settings, &release, &store, test_heap_alloc);

    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_DETAIL, (int)settings.screen);
    const PhysicalInput *bound = last_action_alternative(ACTION_CONFIRM);
    TEST_ASSERT_EQUAL_INT(1, bound->parts.count);
    TEST_ASSERT_TRUE(physical_contains(bound, ATOM_KEY, KEY_F1));
}

/* ---- ACTION-mode capture: duplicate-binding warning (D17) ------------------ */

/* Binding CONFIRM to F5, which is EDITOR_TOGGLE's default key, must still
 * succeed (no hard block -- the function layer resolves order by design,
 * see input_func.h) but should surface a non-blocking "Also bound to"
 * toast naming the other action. */
void test_capture_action_conflict_shows_warning_toast(void)
{
    settings.target_kind = SETTINGS_TARGET_ACTION;
    settings.target_index = ACTION_CONFIRM;

    InputState open_frame = {0};
    enter_capture(&settings, &open_frame, -1);

    InputState press = {0};
    input_state_hold_key(&press, KEY_F5); /* EDITOR_TOGGLE's default binding */
    handle_capture_action(&settings, &press, &store, test_heap_alloc);

    InputState release = {0};
    handle_capture_action(&settings, &release, &store, test_heap_alloc);

    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_DETAIL, (int)settings.screen);
    const PhysicalInput *bound = last_action_alternative(ACTION_CONFIRM);
    TEST_ASSERT_TRUE(physical_contains(bound, ATOM_KEY, KEY_F5)); /* bind still succeeded */
    TEST_ASSERT_EQUAL_STRING("Also bound to EDITOR_TOGGLE", settings.toast_text);
}

/* Binding to a key no default action uses shows the plain "Bound" toast,
 * not a warning. */
void test_capture_action_no_conflict_shows_bound_toast(void)
{
    settings.target_kind = SETTINGS_TARGET_ACTION;
    settings.target_index = ACTION_CONFIRM;

    InputState open_frame = {0};
    enter_capture(&settings, &open_frame, -1);

    InputState press = {0};
    input_state_hold_key(&press, KEY_F1); /* unused by any default binding */
    handle_capture_action(&settings, &press, &store, test_heap_alloc);

    InputState release = {0};
    handle_capture_action(&settings, &release, &store, test_heap_alloc);

    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_DETAIL, (int)settings.screen);
    const PhysicalInput *bound = last_action_alternative(ACTION_CONFIRM);
    TEST_ASSERT_TRUE(physical_contains(bound, ATOM_KEY, KEY_F1));
    TEST_ASSERT_EQUAL_STRING("Bound", settings.toast_text);
}

/* ---- AXIS-mode capture: unchanged ------------------------------------------
 *
 * Axis capture (SETTINGS_CAPTURE_AXIS_FIRST/SECOND) is a separate path from
 * the release-edge rework above and keeps its own capture_axis_armed
 * arming-frame wait. This asserts its single-atom finalize still works. */
void test_capture_axis_first_unchanged(void)
{
    settings.target_kind = SETTINGS_TARGET_AXIS;
    settings.target_index = AXIS_PRIMARY_X;

    InputState open_frame = {0};
    enter_capture(&settings, &open_frame, -1);
    TEST_ASSERT_EQUAL_INT((int)SETTINGS_CAPTURE_AXIS_FIRST, (int)settings.capture_mode);
    TEST_ASSERT_FALSE(settings.capture_axis_armed);

    InputState no_input = {0};
    handle_capture_axis_first(&settings, &no_input, &store, test_heap_alloc);
    TEST_ASSERT_TRUE(settings.capture_axis_armed);
    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_CAPTURE, (int)settings.screen);

    InputState axis_frame = {0};
    input_state_set_gp_axis(&axis_frame, GAMEPAD_AXIS_LEFT_Y, 1.0F);
    handle_capture_axis_first(&settings, &axis_frame, &store, test_heap_alloc);

    TEST_ASSERT_EQUAL_INT(SETTINGS_SCREEN_DETAIL, (int)settings.screen);
    const PhysicalInput *bound = last_axis_alternative(AXIS_PRIMARY_X);
    TEST_ASSERT_EQUAL_INT(1, bound->parts.count);
    TEST_ASSERT_TRUE(physical_contains(bound, ATOM_GP_AXIS, GAMEPAD_AXIS_LEFT_Y));
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();

    RUN_TEST(test_capture_action_excludes_opening_press);
    RUN_TEST(test_capture_action_includes_repress_of_opening_key);
    RUN_TEST(test_capture_action_out_of_order_release);
    RUN_TEST(test_capture_action_single_key);
    RUN_TEST(test_capture_action_conflict_shows_warning_toast);
    RUN_TEST(test_capture_action_no_conflict_shows_bound_toast);
    RUN_TEST(test_capture_axis_first_unchanged);

    return UNITY_END();
}
