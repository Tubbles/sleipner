#include "fff.h"
#include "unity.h"

#include "../src/strv.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/error.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/arena_posix.c" // NOLINT(bugprone-suspicious-include)
#include "../src/attribute.c"   // NOLINT(bugprone-suspicious-include)
#include "../src/entity.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/map.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/editor/core.c" // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

/* Cross-file editor fakes: draw.c */
FAKE_VALUE_FUNC(bool, toggle_pressed, ToggleBinding);
FAKE_VOID_FUNC(update_editor_camera, Camera2D *, InputState, float);
FAKE_VALUE_FUNC(int, find_nearest_entity, const Level *, Vector2);

/* Cross-file editor fakes: attr.c */
FAKE_VOID_FUNC(dispatch_attr_type_change, GameState *, EditorState *, int, UndoHistory *);
FAKE_VOID_FUNC(dispatch_child_props, GameState *, EditorState *, int);
FAKE_VOID_FUNC(propagate_collision_to_entities, GameState *, const Blueprint *);

/* Cross-file editor fakes: child.c */
FAKE_VOID_FUNC(remove_blueprint_child, GameState *, EditorState *, UndoHistory *, int);
FAKE_VALUE_FUNC(int, find_child_entity, const Level *, int, const char *, const char *);

/* Cross-file editor fakes: widgets.c */
FAKE_VOID_FUNC(fuzzy_finder_build_items, GameState *, EditorState *);

/* External module fakes */
FAKE_VALUE_FUNC(const AttrSet *, entity_resolve_defaults, const GameState *, int);
FAKE_VALUE_FUNC(const Blueprint *, blueprint_find, const BlueprintTable *, const char *);
FAKE_VOID_FUNC(undo_history_new_entry, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint, Strv);
FAKE_VOID_FUNC(undo_history_step_back, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint);
FAKE_VOID_FUNC(undo_history_step_forward, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint);
FAKE_VALUE_FUNC(Strv, undo_history_description, const UndoHistory *);

/* TextFormat stub — variadic, cannot use FAKE_VALUE_FUNC */
const char *TextFormat(const char *text, ...)
{
    (void)text;
    return "";
}

/* debug_log stub — variadic with __attribute__((format)) */
void debug_log(DebugState *dbg, const char *format, ...)
{
    (void)dbg;
    (void)format;
}

/* VEC_IMPL / MAP_IMPL for types needed by test setup/cleanup */
VEC_IMPL(blueprint_child, BlueprintChild)
VEC_IMPL(blueprint, Blueprint)
MAP_IMPL(entity_ruleset, int, vec_rule, map_hash_int, map_eq_int)

#include "test_heap_alloc.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---- Helpers ------------------------------------------------------------ */

static void test_attr_set_free_local(AttrSet *set)
{
    attr_set_free(&test_heap_alloc, set);
}

static void test_blueprint_free_local(Blueprint *blueprint)
{
    for (int index = 0; index < blueprint->children.count; index++) {
        str_free(&test_heap_alloc, &blueprint->children.data[index].blueprint_name);
        str_free(&test_heap_alloc, &blueprint->children.data[index].tag);
    }
    vec_blueprint_child_free(&blueprint->children);
    test_attr_set_free_local(&blueprint->attrs);
}

static void test_blueprint_table_free_local(BlueprintTable *table)
{
    for (int index = 0; index < table->entries.count; index++) {
        test_blueprint_free_local(&table->entries.data[index]);
    }
    vec_blueprint_free(&table->entries);
}

static Blueprint make_named_blueprint(const char *name)
{
    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &blueprint.attrs, (AttrStringPair){"name", name}));
    return blueprint;
}

/* ---- place_visible_count ------------------------------------------------ */

void test_editor_place_visible_count_known_height(void)
{
    int screen_height = 480;
    int expected = (screen_height - HINTS_BAR_HEIGHT - EDITOR_PANEL_LINE_HEIGHT) / EDITOR_PANEL_LINE_HEIGHT;
    TEST_ASSERT_EQUAL_INT(expected, place_visible_count(screen_height));
}

void test_editor_place_visible_count_small_height(void)
{
    int result = place_visible_count(HINTS_BAR_HEIGHT + EDITOR_PANEL_LINE_HEIGHT);
    TEST_ASSERT_EQUAL_INT(0, result);
}

/* ---- find_blueprint_by_name --------------------------------------------- */

void test_editor_find_blueprint_by_name_found(void)
{
    GameState state = {0};
    state.gamedata.blueprints.entries.alloc = test_heap_alloc;
    Blueprint blueprint = make_named_blueprint("chest");
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, blueprint));

    Blueprint *result = find_blueprint_by_name(&state, "chest");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("chest", attr_get_string(&result->attrs, "name"));

    test_blueprint_table_free_local(&state.gamedata.blueprints);
}

void test_editor_find_blueprint_by_name_not_found(void)
{
    GameState state = {0};
    state.gamedata.blueprints.entries.alloc = test_heap_alloc;
    Blueprint blueprint = make_named_blueprint("chest");
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, blueprint));

    TEST_ASSERT_NULL(find_blueprint_by_name(&state, "nonexistent"));

    test_blueprint_table_free_local(&state.gamedata.blueprints);
}

void test_editor_find_blueprint_by_name_empty_table(void)
{
    GameState state = {0};
    TEST_ASSERT_NULL(find_blueprint_by_name(&state, "anything"));
}

/* ---- total_attr_count --------------------------------------------------- */

void test_editor_total_attr_count_instance_only(void)
{
    GameState state = {0};
    Entity entity = {0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "health", 5));

    TEST_ASSERT_EQUAL_INT(2, total_attr_count(&state, &entity));

    test_attr_set_free_local(&entity.attrs);
}

void test_editor_total_attr_count_with_blueprint(void)
{
    GameState state = {0};
    state.gamedata.blueprints.entries.alloc = test_heap_alloc;
    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &blueprint.attrs, (AttrStringPair){"name", "test_bp"}));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint.attrs, "bp_attr", 42));
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, blueprint);

    Entity entity = {0};
    entity.id = 1;
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &entity.blueprint_name, "test_bp"));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "inst_attr", 10));
    Str bp_name = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &bp_name, "test_bp"));
    (void)map_int_str_set(&state.gamedata.entity_blueprints, entity.id, bp_name, &test_heap_alloc);

    entity_resolve_defaults_fake.return_val = &state.gamedata.blueprints.entries.data[0].attrs;
    TEST_ASSERT_EQUAL_INT(2 + 1, total_attr_count(&state, &entity));
    entity_resolve_defaults_fake.return_val = nullptr;

    test_attr_set_free_local(&entity.attrs);
    test_attr_set_free_local(&blueprint.attrs);
    str_free(&test_heap_alloc, &entity.blueprint_name);
    str_free(&test_heap_alloc, &bp_name);
    map_int_str_free(&state.gamedata.entity_blueprints, &test_heap_alloc);
    vec_blueprint_free(&state.gamedata.blueprints.entries);
}

/* ---- is_blueprint_attr -------------------------------------------------- */

void test_editor_is_blueprint_attr_false_for_instance(void)
{
    Entity entity = {0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));

    TEST_ASSERT_FALSE(is_blueprint_attr(&entity, 0));

    test_attr_set_free_local(&entity.attrs);
}

void test_editor_is_blueprint_attr_true_for_blueprint(void)
{
    Entity entity = {0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));

    TEST_ASSERT_TRUE(is_blueprint_attr(&entity, 1));

    test_attr_set_free_local(&entity.attrs);
}

/* ---- attr_at_display_index ---------------------------------------------- */

void test_editor_attr_at_display_index_instance(void)
{
    GameState state = {0};
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 42));

    Attribute *attr = attr_at_display_index(&state, &entity, 0);
    TEST_ASSERT_NOT_NULL(attr);
    TEST_ASSERT_EQUAL_INT(42, attr->value.i);

    test_attr_set_free_local(&entity.attrs);
}

void test_editor_attr_at_display_index_blueprint(void)
{
    GameState state = {0};
    state.gamedata.blueprints.entries.alloc = test_heap_alloc;

    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, (Blueprint){0}));
    Blueprint *blueprint = &state.gamedata.blueprints.entries.data[0];
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &blueprint->attrs, (AttrStringPair){"name", "test_bp"}));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint->attrs, "bp_val", 99));

    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &entity.blueprint_name, "test_bp"));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "inst_val", 10));

    int blueprint_attr_index = entity.attrs.entries.count + 1;
    Attribute *attr = attr_at_display_index(&state, &entity, blueprint_attr_index);
    TEST_ASSERT_NOT_NULL(attr);
    TEST_ASSERT_EQUAL_INT(99, attr->value.i);

    str_free(&test_heap_alloc, &entity.blueprint_name);
    test_attr_set_free_local(&entity.attrs);
    test_blueprint_table_free_local(&state.gamedata.blueprints);
}

void test_editor_attr_at_display_index_out_of_range(void)
{
    GameState state = {0};
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));

    Attribute *attr = attr_at_display_index(&state, &entity, 99);
    TEST_ASSERT_NULL(attr);

    test_attr_set_free_local(&entity.attrs);
}

/* ---- mark_deleted_descendants ------------------------------------------- */

void test_editor_mark_deleted_root_marks_child(void)
{
    Level level = {.entities.alloc = test_heap_alloc};
    Entity root = {.parent_index = -1};
    Entity child = {.parent_index = 0};
    Entity other = {.parent_index = -1};
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, root));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, child));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, other));

    bool is_deleted[] = {true, false, false};
    mark_deleted_descendants(&level, is_deleted, 3);

    TEST_ASSERT_TRUE(is_deleted[0]);
    TEST_ASSERT_TRUE(is_deleted[1]);
    TEST_ASSERT_FALSE(is_deleted[2]);

    vec_entity_free(&level.entities);
}

void test_editor_mark_deleted_chain(void)
{
    Level level = {.entities.alloc = test_heap_alloc};
    Entity grandparent = {.parent_index = -1};
    Entity parent_entity = {.parent_index = 0};
    Entity grandchild = {.parent_index = 1};
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, grandparent));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, parent_entity));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, grandchild));

    bool is_deleted[] = {true, false, false};
    mark_deleted_descendants(&level, is_deleted, 3);

    TEST_ASSERT_TRUE(is_deleted[0]);
    TEST_ASSERT_TRUE(is_deleted[1]);
    TEST_ASSERT_TRUE(is_deleted[2]);

    vec_entity_free(&level.entities);
}

void test_editor_mark_deleted_sibling_untouched(void)
{
    Level level = {.entities.alloc = test_heap_alloc};
    Entity root_a = {.parent_index = -1};
    Entity child_a = {.parent_index = 0};
    Entity root_b = {.parent_index = -1};
    Entity child_b = {.parent_index = 2};
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, root_a));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, child_a));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, root_b));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, child_b));

    bool is_deleted[] = {true, false, false, false};
    mark_deleted_descendants(&level, is_deleted, 4);

    TEST_ASSERT_TRUE(is_deleted[0]);
    TEST_ASSERT_TRUE(is_deleted[1]);
    TEST_ASSERT_FALSE(is_deleted[2]);
    TEST_ASSERT_FALSE(is_deleted[3]);

    vec_entity_free(&level.entities);
}

/* ---- tree_section_total ------------------------------------------------- */

void test_tree_section_total_root_no_children(void)
{
    Entity entity = {.parent_index = -1};
    Blueprint blueprint = {.children = {0}};
    TEST_ASSERT_EQUAL_INT(1, tree_section_total(&entity, &blueprint));
}

void test_tree_section_total_root_with_children(void)
{
    Entity entity = {.parent_index = -1};
    BlueprintChild children[2] = {0};
    Blueprint blueprint = {.children = {.data = children, .count = 2}};
    TEST_ASSERT_EQUAL_INT(3, tree_section_total(&entity, &blueprint));
}

void test_tree_section_total_child_entity(void)
{
    Entity entity = {.parent_index = 0};
    Blueprint blueprint = {.children = {0}};
    TEST_ASSERT_EQUAL_INT(2, tree_section_total(&entity, &blueprint));
}

/* ---- tree_is_parent_row ------------------------------------------------- */

void test_tree_is_parent_row_true(void)
{
    Entity entity = {.parent_index = 0};
    TEST_ASSERT_TRUE(tree_is_parent_row(&entity, 0));
}

void test_tree_is_parent_row_false(void)
{
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_FALSE(tree_is_parent_row(&entity, 0));
}

/* ---- tree_child_index --------------------------------------------------- */

void test_tree_child_index_mapping(void)
{
    Entity root = {.parent_index = -1};
    TEST_ASSERT_EQUAL_INT(0, tree_child_index(&root, 0));
    TEST_ASSERT_EQUAL_INT(1, tree_child_index(&root, 1));

    Entity child = {.parent_index = 0};
    TEST_ASSERT_EQUAL_INT(-1, tree_child_index(&child, 0));
    TEST_ASSERT_EQUAL_INT(0, tree_child_index(&child, 1));
}

/* ---- tree_is_add_child_row ---------------------------------------------- */

void test_tree_is_add_child_sentinel(void)
{
    Entity entity = {.parent_index = -1};
    BlueprintChild children[2] = {0};
    Blueprint blueprint = {.children = {.data = children, .count = 2}};
    TEST_ASSERT_FALSE(tree_is_add_child_row(&entity, &blueprint, 0));
    TEST_ASSERT_FALSE(tree_is_add_child_row(&entity, &blueprint, 1));
    TEST_ASSERT_TRUE(tree_is_add_child_row(&entity, &blueprint, 2));
}

/* ---- delete_selected_entity --------------------------------------------- */

void test_editor_delete_entity_removes_map_entries(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.scratch_arena));
    state.gamedata.current_level.entities.alloc = test_heap_alloc;
    state.gamedata.entity_blueprints = (map_int_str){0};
    state.gamedata.rule_table = (map_entity_ruleset){0};

    Entity entity_a = {.id = 10, .parent_index = -1};
    Entity entity_b = {.id = 20, .parent_index = -1};
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity_a));
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity_b));

    Str name_a = {0};
    Str name_b = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &name_a, "blueprint_a"));
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &name_b, "blueprint_b"));
    TEST_ASSERT_TRUE(map_int_str_set(&state.gamedata.entity_blueprints, 10, name_a, &test_heap_alloc));
    TEST_ASSERT_TRUE(map_int_str_set(&state.gamedata.entity_blueprints, 20, name_b, &test_heap_alloc));

    vec_rule rules_a = {0};
    vec_rule rules_b = {0};
    TEST_ASSERT_TRUE(map_entity_ruleset_set(&state.gamedata.rule_table, 10, rules_a, &test_heap_alloc));
    TEST_ASSERT_TRUE(map_entity_ruleset_set(&state.gamedata.rule_table, 20, rules_b, &test_heap_alloc));

    EditorState editor_state = {.selected_entity_index = 0};
    WatchList watches = {0};
    state.gamedata.player_index = -1;

    delete_selected_entity(&state, &editor_state, &watches);

    TEST_ASSERT_EQUAL_INT(1, state.gamedata.current_level.entities.count);
    TEST_ASSERT_NULL(map_int_str_get(&state.gamedata.entity_blueprints, 10));
    TEST_ASSERT_NOT_NULL(map_int_str_get(&state.gamedata.entity_blueprints, 20));
    TEST_ASSERT_NULL(map_entity_ruleset_get(&state.gamedata.rule_table, 10));
    TEST_ASSERT_NOT_NULL(map_entity_ruleset_get(&state.gamedata.rule_table, 20));

    str_free(&test_heap_alloc, &name_a);
    str_free(&test_heap_alloc, &name_b);
    vec_entity_free(&state.gamedata.current_level.entities);
    map_int_str_free(&state.gamedata.entity_blueprints, &test_heap_alloc);
    map_entity_ruleset_free(&state.gamedata.rule_table, &test_heap_alloc);
    arena_reset(&state.scratch_arena);
}

/* ---- handle_browse_navigate --------------------------------------------- */

void test_navigate_tree_to_attr_boundary(void)
{
    GameState state = {0};
    ErrorState err = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    Entity entity = {0};
    entity.parent_index = -1;
    (void)str_from_cstr(&alloc, &entity.blueprint_name, "wagon");
    state.gamedata.current_level.entities = vec_entity_new(alloc);
    (void)vec_entity_push(&state.gamedata.current_level.entities, entity);

    Blueprint wagon = make_named_blueprint("wagon");
    state.gamedata.blueprints.entries = vec_blueprint_new(test_heap_alloc);
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, wagon);

    blueprint_find_fake.return_val = &state.gamedata.blueprints.entries.data[0];

    EditorState editor_state = {0};
    editor_state.selected_entity_index = 0;
    editor_state.selected_tree_index = 0;
    editor_state.selected_attr_index = -1;

    handle_browse_navigate(&state, &editor_state, 1);

    TEST_ASSERT_EQUAL_INT(-1, editor_state.selected_tree_index);
    TEST_ASSERT_EQUAL_INT(0, editor_state.selected_attr_index);

    test_blueprint_table_free_local(&state.gamedata.blueprints);
    arena_free(&state.gamedata_arena);
}

void test_navigate_attr_to_tree_boundary(void)
{
    GameState state = {0};
    ErrorState err = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    Entity entity = {0};
    entity.parent_index = -1;
    (void)str_from_cstr(&alloc, &entity.blueprint_name, "wagon");
    state.gamedata.current_level.entities = vec_entity_new(alloc);
    (void)vec_entity_push(&state.gamedata.current_level.entities, entity);

    Blueprint wagon = make_named_blueprint("wagon");
    state.gamedata.blueprints.entries = vec_blueprint_new(test_heap_alloc);
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, wagon);

    blueprint_find_fake.return_val = &state.gamedata.blueprints.entries.data[0];

    EditorState editor_state = {0};
    editor_state.selected_entity_index = 0;
    editor_state.selected_tree_index = -1;
    editor_state.selected_attr_index = 0;

    handle_browse_navigate(&state, &editor_state, -1);

    TEST_ASSERT_EQUAL_INT(0, editor_state.selected_tree_index);
    TEST_ASSERT_EQUAL_INT(-1, editor_state.selected_attr_index);

    test_blueprint_table_free_local(&state.gamedata.blueprints);
    arena_free(&state.gamedata_arena);
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();

    RUN_TEST(test_editor_place_visible_count_known_height);
    RUN_TEST(test_editor_place_visible_count_small_height);
    RUN_TEST(test_editor_find_blueprint_by_name_found);
    RUN_TEST(test_editor_find_blueprint_by_name_not_found);
    RUN_TEST(test_editor_find_blueprint_by_name_empty_table);
    RUN_TEST(test_editor_total_attr_count_instance_only);
    RUN_TEST(test_editor_total_attr_count_with_blueprint);
    RUN_TEST(test_editor_is_blueprint_attr_false_for_instance);
    RUN_TEST(test_editor_is_blueprint_attr_true_for_blueprint);
    RUN_TEST(test_editor_attr_at_display_index_instance);
    RUN_TEST(test_editor_attr_at_display_index_blueprint);
    RUN_TEST(test_editor_attr_at_display_index_out_of_range);
    RUN_TEST(test_editor_mark_deleted_root_marks_child);
    RUN_TEST(test_editor_mark_deleted_chain);
    RUN_TEST(test_editor_mark_deleted_sibling_untouched);
    RUN_TEST(test_tree_section_total_root_no_children);
    RUN_TEST(test_tree_section_total_root_with_children);
    RUN_TEST(test_tree_section_total_child_entity);
    RUN_TEST(test_tree_is_parent_row_true);
    RUN_TEST(test_tree_is_parent_row_false);
    RUN_TEST(test_tree_child_index_mapping);
    RUN_TEST(test_tree_is_add_child_sentinel);
    RUN_TEST(test_editor_delete_entity_removes_map_entries);
    RUN_TEST(test_navigate_tree_to_attr_boundary);
    RUN_TEST(test_navigate_attr_to_tree_boundary);

    return UNITY_END();
}
