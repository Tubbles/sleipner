#include "fff.h"
#include "unity.h"

#include "raylib.h"

DEFINE_FFF_GLOBALS;

#include "../src/strv.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/error.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/arena_posix.c" // NOLINT(bugprone-suspicious-include)
#include "../src/attribute.c"   // NOLINT(bugprone-suspicious-include)
#include "../src/entity.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/input.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/input_func.c"  // NOLINT(bugprone-suspicious-include)
#include "../src/map.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/vec.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/editor/attr.c" // NOLINT(bugprone-suspicious-include)

VEC_IMPL(blueprint_child, BlueprintChild)

/* Raylib input fakes — input_capture polls these but the unit tests
 * construct InputState directly via input_state_* helpers, so the
 * fakes never actually fire. Stubbing them keeps the link clean. */
FAKE_VALUE_FUNC(int, SetGamepadMappings, const char *);
FAKE_VALUE_FUNC(bool, IsGamepadAvailable, int);
FAKE_VALUE_FUNC(float, GetGamepadAxisMovement, int, int);
FAKE_VALUE_FUNC(bool, IsKeyPressed, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonPressed, int, int);
FAKE_VALUE_FUNC(bool, IsKeyDown, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonDown, int, int);

/* debug_log is variadic; provide a no-op stub. */
void debug_log(DebugState *dbg, const char *format, ...)
{
    (void)dbg;
    (void)format;
}

/* Cross-file editor fakes: keybindings.c (kept for HUD-hint table that
 * still lives in attr.c — never actually called by the migrated input
 * functions). */
FAKE_VALUE_FUNC(bool, binding_pressed, const EditorBinding *);
FAKE_VALUE_FUNC(bool, binding_held, const EditorBinding *);
FAKE_VALUE_FUNC(bool, binding_modifier_down, const EditorBinding *);

/* Cross-file editor fakes: core.c */
FAKE_VALUE_FUNC(Blueprint *, find_blueprint_by_name, GameState *, const char *);
FAKE_VALUE_FUNC(bool, is_blueprint_attr, const GameState *, const Entity *, int);
FAKE_VALUE_FUNC(Attribute *, attr_at_display_index, GameState *, Entity *, int);

/* Cross-file editor fakes: child.c */
FAKE_VOID_FUNC(propagate_child_tag, GameState *, const Blueprint *, int, const char *);
FAKE_VOID_FUNC(propagate_child_offset, GameState *, const Blueprint *, int);

/* External module fakes */
FAKE_VOID_FUNC(undo_history_new_entry, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint, Strv);

/* TextFormat stub — variadic, cannot use FAKE_VALUE_FUNC */
const char *TextFormat(const char *text, ...)
{
    (void)text;
    return "";
}

#include "test_heap_alloc.h"

static BindingStore test_bindings;
static bool test_bindings_loaded;
static const BindingStore *get_test_bindings(void)
{
    if (!test_bindings_loaded) {
        test_helpers_init();
        input_func_load_defaults(&test_bindings, test_heap_alloc);
        test_bindings_loaded = true;
    }
    return &test_bindings;
}

void setUp(void) {}
void tearDown(void) {}

/* ---- Helpers ------------------------------------------------------------ */

static void test_attr_set_free_local(AttrSet *set)
{
    attr_set_free(&test_heap_alloc, set);
}

/* ---- apply_attr_delta --------------------------------------------------- */

void test_editor_apply_attr_delta_int(void)
{
    GameState state = {0};
    state.gamedata.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));

    /* Point attr_at_display_index fake at the actual attribute */
    attr_at_display_index_fake.return_val = &state.gamedata.current_level.entities.data[0].attrs.entries.data[0];

    EditorState editor_state = {.selected_entity_index = 0, .selected_attr_index = 0};
    apply_attr_delta(&state, &editor_state, 5);

    Attribute *attr = &state.gamedata.current_level.entities.data[0].attrs.entries.data[0];
    TEST_ASSERT_EQUAL_INT(15, attr->value.i);

    test_attr_set_free_local(&state.gamedata.current_level.entities.data[0].attrs);
    vec_entity_free(&state.gamedata.current_level.entities);
}

void test_editor_apply_attr_delta_float(void)
{
    GameState state = {0};
    state.gamedata.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "speed", 10.0F));
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));

    attr_at_display_index_fake.return_val = &state.gamedata.current_level.entities.data[0].attrs.entries.data[0];

    EditorState editor_state = {.selected_entity_index = 0, .selected_attr_index = 0};
    apply_attr_delta(&state, &editor_state, 3);

    Attribute *attr = &state.gamedata.current_level.entities.data[0].attrs.entries.data[0];
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 13.0F, attr->value.f);

    test_attr_set_free_local(&state.gamedata.current_level.entities.data[0].attrs);
    vec_entity_free(&state.gamedata.current_level.entities);
}

void test_editor_apply_attr_delta_null_attr_no_crash(void)
{
    GameState state = {0};
    state.gamedata.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));

    attr_at_display_index_fake.return_val = nullptr;

    EditorState editor_state = {.selected_entity_index = 0, .selected_attr_index = 99};
    apply_attr_delta(&state, &editor_state, 1);

    vec_entity_free(&state.gamedata.current_level.entities);
}

/* ---- read_value_delta --------------------------------------------------- */

void test_editor_read_value_delta_no_input(void)
{
    InputState input = {0};
    TEST_ASSERT_EQUAL_INT(0, read_value_delta(&input, get_test_bindings()));
}

void test_editor_read_value_delta_large_minus(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_LEFT_BRACKET);
    TEST_ASSERT_EQUAL_INT(-EDITOR_ATTR_LARGE_STEP, read_value_delta(&input, get_test_bindings()));
}

void test_editor_read_value_delta_large_plus(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_RIGHT_BRACKET);
    TEST_ASSERT_EQUAL_INT(EDITOR_ATTR_LARGE_STEP, read_value_delta(&input, get_test_bindings()));
}

void test_editor_read_value_delta_huge_minus(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_PAGE_DOWN);
    TEST_ASSERT_EQUAL_INT(-EDITOR_ATTR_HUGE_STEP, read_value_delta(&input, get_test_bindings()));
}

void test_editor_read_value_delta_combined(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_LEFT_BRACKET);
    input_state_press_key(&input, KEY_RIGHT_BRACKET);
    input_state_press_key(&input, KEY_PAGE_DOWN);
    input_state_press_key(&input, KEY_PAGE_UP);
    int expected = -EDITOR_ATTR_LARGE_STEP + EDITOR_ATTR_LARGE_STEP - EDITOR_ATTR_HUGE_STEP + EDITOR_ATTR_HUGE_STEP;
    TEST_ASSERT_EQUAL_INT(expected, read_value_delta(&input, get_test_bindings()));
}

/* ---- read_held_dir ------------------------------------------------------ */

void test_editor_read_held_dir_left_key(void)
{
    InputState input = {0};
    input_state_hold_key(&input, KEY_LEFT);
    TEST_ASSERT_EQUAL_INT(-1, read_held_dir(&input, get_test_bindings()));
}

void test_editor_read_held_dir_right_gamepad(void)
{
    InputState input = {0};
    input_state_hold_gp_button(&input, GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
    TEST_ASSERT_EQUAL_INT(1, read_held_dir(&input, get_test_bindings()));
}

void test_editor_read_held_dir_none(void)
{
    InputState input = {0};
    TEST_ASSERT_EQUAL_INT(0, read_held_dir(&input, get_test_bindings()));
}

/* ---- attr type conversion ----------------------------------------------- */

void test_attr_type_change_int_to_float(void)
{
    Attribute attr = {.type = ATTR_INT, .value = {.i = 42}};
    Allocator alloc = test_heap_alloc;
    AttrConvertedValues values = attr_extract_values(&attr, &alloc);
    attr_apply_converted(&attr, ATTR_FLOAT, values, &alloc);

    TEST_ASSERT_EQUAL_INT(ATTR_FLOAT, attr.type);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 42.0F, attr.value.f);
}

void test_attr_type_change_float_to_int(void)
{
    Attribute attr = {.type = ATTR_FLOAT, .value = {.f = 3.7F}};
    Allocator alloc = test_heap_alloc;
    AttrConvertedValues values = attr_extract_values(&attr, &alloc);
    attr_apply_converted(&attr, ATTR_INT, values, &alloc);

    TEST_ASSERT_EQUAL_INT(ATTR_INT, attr.type);
    TEST_ASSERT_EQUAL_INT(3, attr.value.i);
}

void test_attr_type_change_int_to_bool_nonzero(void)
{
    Attribute attr = {.type = ATTR_INT, .value = {.i = 5}};
    Allocator alloc = test_heap_alloc;
    AttrConvertedValues values = attr_extract_values(&attr, &alloc);
    attr_apply_converted(&attr, ATTR_BOOL, values, &alloc);

    TEST_ASSERT_EQUAL_INT(ATTR_BOOL, attr.type);
    TEST_ASSERT_TRUE(attr.value.b);
}

void test_attr_type_change_int_to_bool_zero(void)
{
    Attribute attr = {.type = ATTR_INT, .value = {.i = 0}};
    Allocator alloc = test_heap_alloc;
    AttrConvertedValues values = attr_extract_values(&attr, &alloc);
    attr_apply_converted(&attr, ATTR_BOOL, values, &alloc);

    TEST_ASSERT_EQUAL_INT(ATTR_BOOL, attr.type);
    TEST_ASSERT_FALSE(attr.value.b);
}

void test_attr_type_change_string_to_int(void)
{
    Str test_str = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&test_str, "42"));
    Attribute attr = {.type = ATTR_STRING, .value = {.str = test_str}};
    AttrConvertedValues values = attr_extract_values(&attr, &test_heap_alloc);
    attr_apply_converted(&attr, ATTR_INT, values, &test_heap_alloc);

    TEST_ASSERT_EQUAL_INT(ATTR_INT, attr.type);
    TEST_ASSERT_EQUAL_INT(42, attr.value.i);
}

void test_attr_type_radial_order_matches_enum(void)
{
    TEST_ASSERT_EQUAL_INT(ATTR_FLOAT, attr_type_radial_order[0]);
    TEST_ASSERT_EQUAL_INT(ATTR_INT, attr_type_radial_order[1]);
    TEST_ASSERT_EQUAL_INT(ATTR_BOOL, attr_type_radial_order[2]);
    TEST_ASSERT_EQUAL_INT(ATTR_STRING, attr_type_radial_order[3]);
}

/* ---- attr_remove -------------------------------------------------------- */

void test_attr_remove_instance_attr(void)
{
    Entity entity = {0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "health", 5));

    attr_remove(&test_heap_alloc, &entity.attrs, "speed");

    TEST_ASSERT_EQUAL_INT(1, entity.attrs.entries.count);
    TEST_ASSERT_EQUAL_STRING("health", entity.attrs.entries.data[0].name.ptr);

    test_attr_set_free_local(&entity.attrs);
}

/* ---- dispatch_child_props ----------------------------------------------- */

void test_dispatch_child_props_tag_mode(void)
{
    GameState state = {0};
    state.gamedata.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1};
    entity.blueprint_name = str_new(test_heap_alloc);
    (void)str_from_cstr(&entity.blueprint_name, "npc");
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));

    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &blueprint.attrs, (AttrStringPair){"name", "npc"}));
    blueprint.children.alloc = test_heap_alloc;
    BlueprintChild child = {0};
    child.blueprint_name = str_new(test_heap_alloc);
    (void)str_from_cstr(&child.blueprint_name, "weapon");
    child.tag = str_new(test_heap_alloc);
    (void)str_from_cstr(&child.tag, "sword");
    TEST_ASSERT_TRUE(vec_blueprint_child_push(&blueprint.children, child));

    find_blueprint_by_name_fake.return_val = &blueprint;
    EditorState editor_state = {.selected_entity_index = 0, .child_edit_index = 0};

    dispatch_child_props(&state, &editor_state, 0);

    TEST_ASSERT_TRUE(editor_state.editing_child_tag);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_WORD_BUILDER, editor_state.sub_mode);
    TEST_ASSERT_EQUAL_STRING("sword", editor_state.word_builder_buf);

    str_free(&state.gamedata.current_level.entities.data[0].blueprint_name);
    vec_entity_free(&state.gamedata.current_level.entities);
    for (int index = 0; index < blueprint.children.count; index++) {
        str_free(&blueprint.children.data[index].blueprint_name);
        str_free(&blueprint.children.data[index].tag);
    }
    vec_blueprint_child_free(&blueprint.children);
    test_attr_set_free_local(&blueprint.attrs);
}

void test_dispatch_child_props_offset_x(void)
{
    GameState state = {0};
    state.gamedata.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1};
    entity.blueprint_name = str_new(test_heap_alloc);
    (void)str_from_cstr(&entity.blueprint_name, "npc");
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));

    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &blueprint.attrs, (AttrStringPair){"name", "npc"}));
    blueprint.children.alloc = test_heap_alloc;
    BlueprintChild child = {.offset = {42.0F, 0.0F}};
    child.blueprint_name = str_new(test_heap_alloc);
    (void)str_from_cstr(&child.blueprint_name, "weapon");
    TEST_ASSERT_TRUE(vec_blueprint_child_push(&blueprint.children, child));

    find_blueprint_by_name_fake.return_val = &blueprint;
    EditorState editor_state = {.selected_entity_index = 0, .child_edit_index = 0};

    dispatch_child_props(&state, &editor_state, 1);

    TEST_ASSERT_TRUE(editor_state.editing_child_offset);
    TEST_ASSERT_EQUAL_INT(0, editor_state.child_edit_axis);
    TEST_ASSERT_EQUAL_INT(42, editor_state.saved_attr_int);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_ATTR_EDIT, editor_state.sub_mode);

    str_free(&state.gamedata.current_level.entities.data[0].blueprint_name);
    vec_entity_free(&state.gamedata.current_level.entities);
    str_free(&blueprint.children.data[0].blueprint_name);
    vec_blueprint_child_free(&blueprint.children);
    test_attr_set_free_local(&blueprint.attrs);
}

void test_dispatch_child_props_invalid(void)
{
    RESET_FAKE(find_blueprint_by_name);
    GameState state = {0};
    EditorState editor_state = {.selected_entity_index = -1};

    dispatch_child_props(&state, &editor_state, 0);
    TEST_ASSERT_EQUAL_INT(0, find_blueprint_by_name_fake.call_count);
}

/* ---- confirm_child_tag_edit --------------------------------------------- */

void test_confirm_child_tag_edit_updates_tag(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    Entity entity = {.parent_index = -1};
    entity.blueprint_name = str_new(alloc);
    (void)str_from_cstr(&entity.blueprint_name, "npc");
    (void)vec_entity_push(&state.gamedata.current_level.entities, entity);

    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &blueprint.attrs, (AttrStringPair){"name", "npc"}));
    blueprint.children = vec_blueprint_child_new(alloc);
    BlueprintChild child = {0};
    child.blueprint_name = str_new(alloc);
    (void)str_from_cstr(&child.blueprint_name, "weapon");
    child.tag = str_new(alloc);
    (void)str_from_cstr(&child.tag, "old_tag");
    TEST_ASSERT_TRUE(vec_blueprint_child_push(&blueprint.children, child));

    find_blueprint_by_name_fake.return_val = &blueprint;
    EditorState editor_state = {
        .selected_entity_index = 0,
        .child_edit_index = 0,
        .editing_child_tag = true,
    };
    strcpy(editor_state.word_builder_buf, "new_tag");
    editor_state.word_builder_len = 7;

    Diag diag = {0};
    UndoHistory undo_history = {0};

    confirm_child_tag_edit(&diag, &state, &editor_state, &undo_history);

    TEST_ASSERT_EQUAL_STRING("new_tag", blueprint.children.data[0].tag.ptr);
    TEST_ASSERT_FALSE(editor_state.editing_child_tag);
    TEST_ASSERT_EQUAL_INT(1, propagate_child_tag_fake.call_count);
    TEST_ASSERT_EQUAL_INT(1, undo_history_new_entry_fake.call_count);

    find_blueprint_by_name_fake.return_val = nullptr;
    test_attr_set_free_local(&blueprint.attrs);
    arena_free(&state.gamedata_arena);
}

void test_confirm_child_tag_edit_invalid(void)
{
    RESET_FAKE(undo_history_new_entry);
    GameState state = {0};
    EditorState editor_state = {
        .selected_entity_index = -1,
        .editing_child_tag = true,
    };
    Diag diag = {0};
    UndoHistory undo_history = {0};

    confirm_child_tag_edit(&diag, &state, &editor_state, &undo_history);

    TEST_ASSERT_EQUAL_INT(0, undo_history_new_entry_fake.call_count);
}

/* ---- reset_attr_hold ---------------------------------------------------- */

void test_reset_attr_hold_clears_fields(void)
{
    EditorState editor_state = {
        .attr_hold_total = 5.0F,
        .attr_hold_subtick = 1.0F,
        .attr_hold_dir = 1,
    };

    reset_attr_hold(&editor_state);

    TEST_ASSERT_EQUAL_FLOAT(0.0F, editor_state.attr_hold_total);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, editor_state.attr_hold_subtick);
    TEST_ASSERT_EQUAL_INT(0, editor_state.attr_hold_dir);
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();

    RUN_TEST(test_editor_apply_attr_delta_int);
    RUN_TEST(test_editor_apply_attr_delta_float);
    RUN_TEST(test_editor_apply_attr_delta_null_attr_no_crash);
    RUN_TEST(test_editor_read_value_delta_no_input);
    RUN_TEST(test_editor_read_value_delta_large_minus);
    RUN_TEST(test_editor_read_value_delta_large_plus);
    RUN_TEST(test_editor_read_value_delta_huge_minus);
    RUN_TEST(test_editor_read_value_delta_combined);
    RUN_TEST(test_editor_read_held_dir_left_key);
    RUN_TEST(test_editor_read_held_dir_right_gamepad);
    RUN_TEST(test_editor_read_held_dir_none);
    RUN_TEST(test_attr_type_change_int_to_float);
    RUN_TEST(test_attr_type_change_float_to_int);
    RUN_TEST(test_attr_type_change_int_to_bool_nonzero);
    RUN_TEST(test_attr_type_change_int_to_bool_zero);
    RUN_TEST(test_attr_type_change_string_to_int);
    RUN_TEST(test_attr_type_radial_order_matches_enum);
    RUN_TEST(test_attr_remove_instance_attr);
    RUN_TEST(test_dispatch_child_props_tag_mode);
    RUN_TEST(test_dispatch_child_props_offset_x);
    RUN_TEST(test_dispatch_child_props_invalid);
    RUN_TEST(test_confirm_child_tag_edit_updates_tag);
    RUN_TEST(test_confirm_child_tag_edit_invalid);
    RUN_TEST(test_reset_attr_hold_clears_fields);

    return UNITY_END();
}
