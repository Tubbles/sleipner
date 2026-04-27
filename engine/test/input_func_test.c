#include "fff.h"
#include "unity.h"

#include "raylib.h"

DEFINE_FFF_GLOBALS;

/* The input function layer is pure with respect to raylib in production —
 * input_capture polls raylib and writes a snapshot, then everything else
 * reads only the snapshot. This test never goes through input_capture: it
 * constructs InputState directly via the test helpers in input.h.
 *
 * raylib polls still need fakes because input.c (which input_capture lives
 * in) is pulled in through the engine library link. */
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
#include "../src/vec.c"        // NOLINT(bugprone-suspicious-include)

#include "test_heap_alloc.h"

#include <math.h>
#include <string.h>

static BindingStore store;

void setUp(void)
{
    test_helpers_init();
    store = (BindingStore){0};
    input_func_load_defaults(&store, test_heap_alloc);
}

void tearDown(void)
{
    /* Heap allocator: releasing nested vec memory is a chore the test
     * harness skips since defaults are recreated per test. Production
     * uses an arena and reclaims on rewind. */
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

/* ---- Single-key actions ------------------------------------------------- */

void test_pressed_fires_on_edge(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_ENTER);
    TEST_ASSERT_TRUE(input_pressed(&input, &store, ACTION_CONFIRM));
}

void test_pressed_false_without_press(void)
{
    InputState input = {0};
    input_state_hold_key(&input, KEY_ENTER); /* held but no edge */
    TEST_ASSERT_FALSE(input_pressed(&input, &store, ACTION_CONFIRM));
}

void test_held_fires_when_key_down(void)
{
    InputState input = {0};
    input_state_hold_key(&input, KEY_ENTER);
    TEST_ASSERT_TRUE(input_held(&input, &store, ACTION_CONFIRM));
}

void test_pressed_via_gamepad_alternative(void)
{
    InputState input = {0};
    input_state_press_gp_button(&input, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    TEST_ASSERT_TRUE(input_pressed(&input, &store, ACTION_CONFIRM));
}

/* ---- Chord semantics (Ctrl+Z) ------------------------------------------ */

void test_chord_does_not_fire_with_main_alone(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_Z);
    TEST_ASSERT_FALSE(input_pressed(&input, &store, ACTION_EDITOR_UNDO));
}

void test_chord_does_not_fire_with_modifier_alone(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_LEFT_CONTROL);
    TEST_ASSERT_FALSE(input_pressed(&input, &store, ACTION_EDITOR_UNDO));
}

void test_chord_fires_when_both_held_and_one_freshly_pressed(void)
{
    InputState input = {0};
    input_state_hold_key(&input, KEY_LEFT_CONTROL);
    input_state_press_key(&input, KEY_Z);
    TEST_ASSERT_TRUE(input_pressed(&input, &store, ACTION_EDITOR_UNDO));
}

void test_chord_fires_either_press_order(void)
{
    /* Modifier pressed last after main was already down — also valid. */
    InputState input = {0};
    input_state_hold_key(&input, KEY_Z);
    input_state_press_key(&input, KEY_LEFT_CONTROL);
    TEST_ASSERT_TRUE(input_pressed(&input, &store, ACTION_EDITOR_UNDO));
}

void test_chord_held_requires_all_parts(void)
{
    InputState input = {0};
    input_state_hold_key(&input, KEY_LEFT_CONTROL); /* missing Z */
    TEST_ASSERT_FALSE(input_held(&input, &store, ACTION_EDITOR_UNDO));
    input_state_hold_key(&input, KEY_Z);
    TEST_ASSERT_TRUE(input_held(&input, &store, ACTION_EDITOR_UNDO));
}

void test_gamepad_chord_fires_on_l1_plus_left(void)
{
    InputState input = {0};
    input_state_hold_gp_button(&input, GAMEPAD_BUTTON_LEFT_TRIGGER_1);
    input_state_press_gp_button(&input, GAMEPAD_BUTTON_LEFT_FACE_LEFT);
    TEST_ASSERT_TRUE(input_pressed(&input, &store, ACTION_EDITOR_UNDO));
}

/* ---- Axis evaluation --------------------------------------------------- */

void test_axis_kb_neg_returns_minus_one(void)
{
    InputState input = {0};
    input_state_hold_key(&input, KEY_LEFT);
    TEST_ASSERT_EQUAL_FLOAT(-1.0F, input_axis(&input, &store, AXIS_PRIMARY_X));
}

void test_axis_kb_pos_returns_plus_one(void)
{
    InputState input = {0};
    input_state_hold_key(&input, KEY_RIGHT);
    TEST_ASSERT_EQUAL_FLOAT(1.0F, input_axis(&input, &store, AXIS_PRIMARY_X));
}

void test_axis_kb_both_keys_cancel_to_zero(void)
{
    InputState input = {0};
    input_state_hold_key(&input, KEY_LEFT);
    input_state_hold_key(&input, KEY_RIGHT);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, input_axis(&input, &store, AXIS_PRIMARY_X));
}

void test_axis_alternative_inputs_clamp_at_one(void)
{
    /* Pressing both LEFT (alt 1 neg) and A (alt 2 neg) sums to -2; expect clamp to -1. */
    InputState input = {0};
    input_state_hold_key(&input, KEY_LEFT);
    input_state_hold_key(&input, KEY_A);
    TEST_ASSERT_EQUAL_FLOAT(-1.0F, input_axis(&input, &store, AXIS_PRIMARY_X));
}

void test_axis_gamepad_value_passes_through(void)
{
    InputState input = {0};
    input_state_set_gp_axis(&input, GAMEPAD_AXIS_LEFT_X, 0.5F);
    TEST_ASSERT_EQUAL_FLOAT(0.5F, input_axis(&input, &store, AXIS_PRIMARY_X));
}

void test_axis_pair_unit_disc_clamp(void)
{
    /* Full 8-way diagonal: LEFT+UP both held. Components sum to (-1, -1)
     * with raw magnitude sqrt(2). After unit-disc clamp, magnitude == 1. */
    InputState input = {0};
    input_state_hold_key(&input, KEY_LEFT);
    input_state_hold_key(&input, KEY_UP);
    Vector2 result = input_axis_pair(&input, &store, AXIS_PRIMARY_X, AXIS_PRIMARY_Y);
    float magnitude = sqrtf((result.x * result.x) + (result.y * result.y));
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 1.0F, magnitude);
    TEST_ASSERT_TRUE(result.x < 0.0F);
    TEST_ASSERT_TRUE(result.y < 0.0F);
}

/* ---- Label rendering --------------------------------------------------- */

void test_label_single_action_renders_gamepad_then_keyboard(void)
{
    char buf[LABEL_BUF_CAP];
    int len = input_func_label(&store, ACTION_CONFIRM, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);
    /* Exact format: "A/Ent" — gamepad first, then keyboard. */
    TEST_ASSERT_EQUAL_STRING("A/Ent", buf);
}

void test_label_chord_renders_with_plus(void)
{
    char buf[LABEL_BUF_CAP];
    (void)input_func_label(&store, ACTION_EDITOR_UNDO, buf, sizeof(buf));
    /* "L1+Left/Ctrl+Z" */
    TEST_ASSERT_EQUAL_STRING("L1+Left/Ctrl+Z", buf);
}

void test_label_keyboard_only_action(void)
{
    char buf[LABEL_BUF_CAP];
    (void)input_func_label(&store, ACTION_EDITOR_HANDLES, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("H", buf);
}

void test_label_truncation_safe(void)
{
    char buf[4]; /* tiny — forces input_func_label to truncate */
    memset(buf, 0xFF, sizeof(buf));
    int len = input_func_label(&store, ACTION_EDITOR_UNDO, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len < (int)sizeof(buf));
    TEST_ASSERT_EQUAL_CHAR('\0', buf[len]);
}

/* ---- Defaults loader sanity ------------------------------------------- */

void test_defaults_populate_every_action(void)
{
    /* Every action must have at least one alternative (or be intentionally
     * empty — only ACTION_EDITOR_OPEN_BLUEPRINTS today). */
    for (int act = 0; act < ACTION_COUNT; act++) {
        if (act == ACTION_EDITOR_OPEN_BLUEPRINTS) {
            continue;
        }
        TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, store.actions[act].alternatives.count, "action has no default binding");
    }
    for (int axis = 0; axis < AXIS_COUNT; axis++) {
        TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, store.axes[axis].alternatives.count, "axis has no default binding");
    }
}

/* ---- Code <-> name translation ---------------------------------------- */

void test_key_from_name_round_trip(void)
{
    TEST_ASSERT_EQUAL_INT(KEY_ENTER, input_func_key_from_name("ENTER"));
    TEST_ASSERT_EQUAL_INT(KEY_LEFT_CONTROL, input_func_key_from_name("LEFT_CONTROL"));
    TEST_ASSERT_EQUAL_INT(-1, input_func_key_from_name("NO_SUCH_KEY"));
    TEST_ASSERT_EQUAL_INT(-1, input_func_key_from_name(NULL));
}

void test_gp_button_from_name_round_trip(void)
{
    TEST_ASSERT_EQUAL_INT(GAMEPAD_BUTTON_RIGHT_FACE_DOWN, input_func_gp_button_from_name("RIGHT_FACE_DOWN"));
    TEST_ASSERT_EQUAL_INT(GAMEPAD_BUTTON_LEFT_TRIGGER_1, input_func_gp_button_from_name("LEFT_TRIGGER_1"));
    TEST_ASSERT_EQUAL_INT(-1, input_func_gp_button_from_name("NOPE"));
}

void test_key_label_returns_short_form(void)
{
    TEST_ASSERT_EQUAL_STRING("Ctrl", input_func_key_label(KEY_LEFT_CONTROL));
    TEST_ASSERT_EQUAL_STRING("Ent", input_func_key_label(KEY_ENTER));
    TEST_ASSERT_EQUAL_STRING("?", input_func_key_label(99999));
}

/* ---- Mutation API ------------------------------------------------------- */

/* Stack-borrowed PhysicalInput pointing at caller-supplied atoms. The
 * mutation API deep-copies via clone_physical, so the .alloc field on
 * the source can stay zero — the source is never pushed to. */
static PhysicalInput physical_view(AtomicInput *atoms, int count)
{
    PhysicalInput view = {0};
    view.parts.data = atoms;
    view.parts.count = count;
    view.parts.capacity = count;
    return view;
}

void test_mutation_set_action_alternative(void)
{
    AtomicInput atoms[] = {{.kind = ATOM_KEY, .int_a = KEY_F1, .scale = 1.0F}};
    PhysicalInput replacement = physical_view(atoms, 1);
    TEST_ASSERT_TRUE(input_func_set_action_alternative(&store, test_heap_alloc, ACTION_CONFIRM, 0, &replacement));

    InputState input = {0};
    input_state_press_key(&input, KEY_F1);
    TEST_ASSERT_TRUE(input_pressed(&input, &store, ACTION_CONFIRM));

    /* alt 0 was overwritten; ENTER alone no longer fires CONFIRM via
     * that alternative. The gamepad alternative is unaffected. */
    InputState enter_only = {0};
    input_state_press_key(&enter_only, KEY_ENTER);
    TEST_ASSERT_FALSE(input_pressed(&enter_only, &store, ACTION_CONFIRM));
}

void test_mutation_add_action_alternative(void)
{
    int before = store.actions[ACTION_CONFIRM].alternatives.count;
    AtomicInput atoms[] = {{.kind = ATOM_KEY, .int_a = KEY_F2, .scale = 1.0F}};
    PhysicalInput new_alt = physical_view(atoms, 1);
    TEST_ASSERT_TRUE(input_func_add_action_alternative(&store, test_heap_alloc, ACTION_CONFIRM, &new_alt));
    TEST_ASSERT_EQUAL_INT(before + 1, store.actions[ACTION_CONFIRM].alternatives.count);

    InputState input = {0};
    input_state_press_key(&input, KEY_F2);
    TEST_ASSERT_TRUE(input_pressed(&input, &store, ACTION_CONFIRM));
}

void test_mutation_remove_action_alternative(void)
{
    int before = store.actions[ACTION_CONFIRM].alternatives.count;
    input_func_remove_action_alternative(&store, ACTION_CONFIRM, 0);
    TEST_ASSERT_EQUAL_INT(before - 1, store.actions[ACTION_CONFIRM].alternatives.count);
}

void test_mutation_clear_action_drops_alternatives(void)
{
    input_func_clear_action(&store, ACTION_CONFIRM);
    TEST_ASSERT_EQUAL_INT(0, store.actions[ACTION_CONFIRM].alternatives.count);

    InputState input = {0};
    input_state_press_key(&input, KEY_ENTER);
    TEST_ASSERT_FALSE(input_pressed(&input, &store, ACTION_CONFIRM));
}

void test_mutation_set_action_deep_copies_chord(void)
{
    AtomicInput atoms[2] = {
        {.kind = ATOM_KEY, .int_a = KEY_LEFT_CONTROL, .scale = 1.0F},
        {.kind = ATOM_KEY, .int_a = KEY_F3, .scale = 1.0F},
    };
    PhysicalInput chord = physical_view(atoms, 2);
    TEST_ASSERT_TRUE(input_func_set_action_alternative(&store, test_heap_alloc, ACTION_INTERACT, 0, &chord));

    /* Mutate the caller's atoms after the deep copy; store should be
     * unaffected. If the store had aliased the parts pointer the
     * binding would shift with the caller's edit. */
    atoms[1].int_a = KEY_F10;

    InputState input = {0};
    input_state_hold_key(&input, KEY_LEFT_CONTROL);
    input_state_press_key(&input, KEY_F3);
    TEST_ASSERT_TRUE(input_pressed(&input, &store, ACTION_INTERACT));
}

void test_mutation_reset_action_restores_defaults(void)
{
    input_func_clear_action(&store, ACTION_CONFIRM);
    TEST_ASSERT_EQUAL_INT(0, store.actions[ACTION_CONFIRM].alternatives.count);

    input_func_reset_action_to_defaults(&store, test_heap_alloc, ACTION_CONFIRM);
    TEST_ASSERT_EQUAL_INT(2, store.actions[ACTION_CONFIRM].alternatives.count);

    InputState input = {0};
    input_state_press_key(&input, KEY_ENTER);
    TEST_ASSERT_TRUE(input_pressed(&input, &store, ACTION_CONFIRM));
}

void test_mutation_axis_alternative_round_trip(void)
{
    AtomicInput atoms[] = {{.kind = ATOM_KB_AXIS, .int_a = KEY_F1, .int_b = KEY_F2, .scale = 1.0F}};
    PhysicalInput replacement = physical_view(atoms, 1);
    TEST_ASSERT_TRUE(input_func_set_axis_alternative(&store, test_heap_alloc, AXIS_PRIMARY_X, 0, &replacement));

    InputState input = {0};
    input_state_hold_key(&input, KEY_F2);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 1.0F, input_axis(&input, &store, AXIS_PRIMARY_X));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_pressed_fires_on_edge);
    RUN_TEST(test_pressed_false_without_press);
    RUN_TEST(test_held_fires_when_key_down);
    RUN_TEST(test_pressed_via_gamepad_alternative);

    RUN_TEST(test_chord_does_not_fire_with_main_alone);
    RUN_TEST(test_chord_does_not_fire_with_modifier_alone);
    RUN_TEST(test_chord_fires_when_both_held_and_one_freshly_pressed);
    RUN_TEST(test_chord_fires_either_press_order);
    RUN_TEST(test_chord_held_requires_all_parts);
    RUN_TEST(test_gamepad_chord_fires_on_l1_plus_left);

    RUN_TEST(test_axis_kb_neg_returns_minus_one);
    RUN_TEST(test_axis_kb_pos_returns_plus_one);
    RUN_TEST(test_axis_kb_both_keys_cancel_to_zero);
    RUN_TEST(test_axis_alternative_inputs_clamp_at_one);
    RUN_TEST(test_axis_gamepad_value_passes_through);
    RUN_TEST(test_axis_pair_unit_disc_clamp);

    RUN_TEST(test_label_single_action_renders_gamepad_then_keyboard);
    RUN_TEST(test_label_chord_renders_with_plus);
    RUN_TEST(test_label_keyboard_only_action);
    RUN_TEST(test_label_truncation_safe);

    RUN_TEST(test_defaults_populate_every_action);

    RUN_TEST(test_key_from_name_round_trip);
    RUN_TEST(test_gp_button_from_name_round_trip);
    RUN_TEST(test_key_label_returns_short_form);

    RUN_TEST(test_mutation_set_action_alternative);
    RUN_TEST(test_mutation_add_action_alternative);
    RUN_TEST(test_mutation_remove_action_alternative);
    RUN_TEST(test_mutation_clear_action_drops_alternatives);
    RUN_TEST(test_mutation_set_action_deep_copies_chord);
    RUN_TEST(test_mutation_reset_action_restores_defaults);
    RUN_TEST(test_mutation_axis_alternative_round_trip);

    return UNITY_END();
}
