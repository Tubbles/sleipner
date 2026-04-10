#include "fff.h"
#include "unity.h"

#include "../src/strv.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/error.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/arena_posix.c" // NOLINT(bugprone-suspicious-include)
#include "../src/attribute.c"   // NOLINT(bugprone-suspicious-include)
#include "../src/entity.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/map.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/editor/attr.c" // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

VEC_IMPL(blueprint_child, BlueprintChild)

/* raylib input fakes (read_held_dir calls IsKeyDown/IsGamepadButtonDown directly) */
FAKE_VALUE_FUNC(bool, IsKeyDown, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonDown, int, int);

/* Cross-file editor fakes: draw.c */
FAKE_VALUE_FUNC(bool, toggle_pressed, ToggleBinding);

/* Cross-file editor fakes: core.c */
FAKE_VALUE_FUNC(Blueprint *, find_blueprint_by_name, GameState *, const char *);
FAKE_VALUE_FUNC(bool, is_blueprint_attr, const Entity *, int);
FAKE_VALUE_FUNC(Attribute *, attr_at_display_index, GameState *, Entity *, int);

/* Cross-file editor fakes: child.c */
FAKE_VOID_FUNC(propagate_child_tag, GameState *, const Blueprint *, int, const char *);
FAKE_VOID_FUNC(propagate_child_offset, GameState *, const Blueprint *, int);

/* External module fakes */
FAKE_VALUE_FUNC(Vector2, blueprint_get_collision_offset, const Blueprint *);
FAKE_VALUE_FUNC(Vector2, blueprint_get_collision_size, const Blueprint *);
FAKE_VOID_FUNC(undo_history_new_entry, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint, Strv);

/* TextFormat stub — variadic, cannot use FAKE_VALUE_FUNC */
const char *TextFormat(const char *text, ...)
{
    (void)text;
    return "";
}

#include "test_heap_alloc.h"

void setUp(void) {}
void tearDown(void) {}

/* ---- Helpers ------------------------------------------------------------ */

static void test_attr_set_free_local(AttrSet *set)
{
    attr_set_free(&test_heap_alloc, set);
}

static void reset_input_fakes(void)
{
    RESET_FAKE(IsKeyDown);
    RESET_FAKE(IsGamepadButtonDown);
    RESET_FAKE(toggle_pressed);
    FFF_RESET_HISTORY(); // NOLINT(bugprone-multi-level-implicit-pointer-conversion)
}

static int target_key_for_press;
static bool press_specific_toggle(ToggleBinding binding)
{
    return binding.key == target_key_for_press;
}

static int target_key_for_down;
static bool down_specific_key(int key)
{
    return key == target_key_for_down;
}

static int target_button_for_down;
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static bool down_specific_button(int gamepad, int button)
{
    (void)gamepad;
    return button == target_button_for_down;
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
    reset_input_fakes();
    TEST_ASSERT_EQUAL_INT(0, read_value_delta());
}

void test_editor_read_value_delta_large_minus(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_LEFT_BRACKET;
    toggle_pressed_fake.custom_fake = press_specific_toggle;
    TEST_ASSERT_EQUAL_INT(-EDITOR_ATTR_LARGE_STEP, read_value_delta());
}

void test_editor_read_value_delta_large_plus(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_RIGHT_BRACKET;
    toggle_pressed_fake.custom_fake = press_specific_toggle;
    TEST_ASSERT_EQUAL_INT(EDITOR_ATTR_LARGE_STEP, read_value_delta());
}

void test_editor_read_value_delta_huge_minus(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_PAGE_DOWN;
    toggle_pressed_fake.custom_fake = press_specific_toggle;
    TEST_ASSERT_EQUAL_INT(-EDITOR_ATTR_HUGE_STEP, read_value_delta());
}

void test_editor_read_value_delta_combined(void)
{
    reset_input_fakes();
    toggle_pressed_fake.return_val = true;
    int expected = -EDITOR_ATTR_LARGE_STEP + EDITOR_ATTR_LARGE_STEP - EDITOR_ATTR_HUGE_STEP + EDITOR_ATTR_HUGE_STEP;
    TEST_ASSERT_EQUAL_INT(expected, read_value_delta());
}

/* ---- read_held_dir ------------------------------------------------------ */

void test_editor_read_held_dir_left_key(void)
{
    reset_input_fakes();
    target_key_for_down = KEY_LEFT;
    IsKeyDown_fake.custom_fake = down_specific_key;
    TEST_ASSERT_EQUAL_INT(-1, read_held_dir());
}

void test_editor_read_held_dir_right_gamepad(void)
{
    reset_input_fakes();
    target_button_for_down = GAMEPAD_BUTTON_LEFT_FACE_RIGHT;
    IsGamepadButtonDown_fake.custom_fake = down_specific_button;
    TEST_ASSERT_EQUAL_INT(1, read_held_dir());
}

void test_editor_read_held_dir_none(void)
{
    reset_input_fakes();
    TEST_ASSERT_EQUAL_INT(0, read_held_dir());
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

/* ---- propagate_collision_to_entities ------------------------------------- */

void test_propagate_collision_updates_matching(void)
{
    GameState state = {0};
    state.gamedata.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1, .position = {10.0F, 20.0F}};
    entity.blueprint_name = str_new(test_heap_alloc);
    (void)str_from_cstr(&entity.blueprint_name, "chest");
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));

    AttrSet bp_attrs = {0};
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &bp_attrs, (AttrStringPair){"name", "chest"}));
    Blueprint blueprint = {.attrs = bp_attrs};

    blueprint_get_collision_offset_fake.return_val = (Vector2){2.0F, 3.0F};
    blueprint_get_collision_size_fake.return_val = (Vector2){16.0F, 16.0F};

    propagate_collision_to_entities(&state, &blueprint);

    Entity *updated = &state.gamedata.current_level.entities.data[0];
    TEST_ASSERT_EQUAL_FLOAT(2.0F, updated->collision_offset.x);
    TEST_ASSERT_EQUAL_FLOAT(3.0F, updated->collision_offset.y);
    TEST_ASSERT_EQUAL_FLOAT(16.0F, updated->collision_size.x);
    TEST_ASSERT_EQUAL_FLOAT(16.0F, updated->collision_size.y);

    str_free(&state.gamedata.current_level.entities.data[0].blueprint_name);
    vec_entity_free(&state.gamedata.current_level.entities);
    test_attr_set_free_local(&bp_attrs);
}

void test_propagate_collision_skips_nonmatching(void)
{
    GameState state = {0};
    state.gamedata.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1, .collision_offset = {99.0F, 99.0F}};
    entity.blueprint_name = str_new(test_heap_alloc);
    (void)str_from_cstr(&entity.blueprint_name, "tree");
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));

    AttrSet bp_attrs = {0};
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &bp_attrs, (AttrStringPair){"name", "chest"}));
    Blueprint blueprint = {.attrs = bp_attrs};

    propagate_collision_to_entities(&state, &blueprint);

    TEST_ASSERT_EQUAL_FLOAT(99.0F, state.gamedata.current_level.entities.data[0].collision_offset.x);

    str_free(&state.gamedata.current_level.entities.data[0].blueprint_name);
    vec_entity_free(&state.gamedata.current_level.entities);
    test_attr_set_free_local(&bp_attrs);
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
    RUN_TEST(test_propagate_collision_updates_matching);
    RUN_TEST(test_propagate_collision_skips_nonmatching);
    RUN_TEST(test_dispatch_child_props_tag_mode);
    RUN_TEST(test_dispatch_child_props_offset_x);
    RUN_TEST(test_dispatch_child_props_invalid);
    RUN_TEST(test_confirm_child_tag_edit_updates_tag);
    RUN_TEST(test_confirm_child_tag_edit_invalid);
    RUN_TEST(test_reset_attr_hold_clears_fields);

    return UNITY_END();
}
