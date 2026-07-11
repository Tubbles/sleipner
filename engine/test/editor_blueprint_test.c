#include "fff.h"
#include "unity.h"

#include "../src/strv.c"             // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"              // NOLINT(bugprone-suspicious-include)
#include "../src/error.c"            // NOLINT(bugprone-suspicious-include)
#include "../src/arena_posix.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/attribute.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/entity.c"           // NOLINT(bugprone-suspicious-include)
#include "../src/map.c"              // NOLINT(bugprone-suspicious-include)
#include "../src/editor/blueprint.c" // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

/* Cross-file editor fakes: input_func.c. blueprint.c calls input_pressed;
 * tests don't drive input here, so a fake returning false everywhere is
 * sufficient. */
FAKE_VALUE_FUNC(bool, input_pressed, const InputState *, const BindingStore *, InputAction);
FAKE_VALUE_FUNC(bool, input_held, const InputState *, const BindingStore *, InputAction);
FAKE_VALUE_FUNC(float, input_axis, const InputState *, const BindingStore *, InputAxis);
FAKE_VALUE_FUNC(Vector2, input_axis_pair, const InputState *, const BindingStore *, InputAxis, InputAxis);

/* Cross-file editor fakes: attr.c */
FAKE_VOID_FUNC(dispatch_attr_type_change, GameState *, EditorState *, int, UndoHistory *);
FAKE_VOID_FUNC(dispatch_child_props, GameState *, EditorState *, int);
/* Cross-file editor fakes: child.c */
FAKE_VOID_FUNC(remove_blueprint_child, GameState *, EditorState *, UndoHistory *, int);

/* Cross-file editor fakes: core.c */
FAKE_VALUE_FUNC(Blueprint *, find_blueprint_by_name, GameState *, const char *);
/* S8.7h1: blueprint.c's undo/redo trigger sites reroute a connected client's
 * step to the host through these core.c helpers; faked here (default false =
 * host/offline) so the existing local-step tests still fall through to
 * undo_history_step_back. The end-to-end reroute is covered in integration_test.c. */
FAKE_VALUE_FUNC(bool, editor_client_reroute_undo, GameState *, EditorState *);
FAKE_VALUE_FUNC(bool, editor_client_reroute_redo, GameState *, EditorState *);
/* Cross-file fake: level.c (the descendant walk lives in the level layer) */
FAKE_VOID_FUNC(level_mark_deleted_descendants, const Level *, bool *, int);

/* Cross-file editor fakes: widgets.c */
FAKE_VOID_FUNC(fuzzy_finder_build_items, GameState *, EditorState *);

/* External module fakes */
FAKE_VOID_FUNC(undo_history_new_entry, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint, Strv);
FAKE_VOID_FUNC(undo_history_step_back, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint);
FAKE_VOID_FUNC(undo_history_step_forward, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint);

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
VEC_IMPL(rule, Rule)
MAP_IMPL(entity_ruleset, int, vec_rule, map_hash_int, map_eq_int)

#include "test_heap_alloc.h"

#include <string.h>

void setUp(void)
{
    RESET_FAKE(dispatch_attr_type_change);
    RESET_FAKE(dispatch_child_props);
    RESET_FAKE(remove_blueprint_child);
    RESET_FAKE(find_blueprint_by_name);
    RESET_FAKE(editor_client_reroute_undo);
    RESET_FAKE(editor_client_reroute_redo);
    RESET_FAKE(level_mark_deleted_descendants);
    RESET_FAKE(fuzzy_finder_build_items);
    RESET_FAKE(undo_history_new_entry);
    RESET_FAKE(undo_history_step_back);
    RESET_FAKE(undo_history_step_forward);
}
void tearDown(void) {}

/* ---- Helpers ------------------------------------------------------------ */

static void test_attr_set_free_local(AttrSet *set)
{
    attr_set_free(&test_heap_alloc, set);
}

static void test_blueprint_free_local(Blueprint *blueprint)
{
    for (int index = 0; index < blueprint->children.count; index++) {
        str_free(&blueprint->children.data[index].blueprint_name);
        str_free(&blueprint->children.data[index].tag);
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

/* ---- blueprint_tree_total / blueprint_tree_is_add_child ------------- */

void test_blueprint_tree_total_no_children(void)
{
    Blueprint blueprint = {0};
    TEST_ASSERT_EQUAL_INT(1, blueprint_tree_total(&blueprint)); /* just ADD CHILD sentinel */
}

void test_blueprint_tree_total_with_children(void)
{
    Blueprint blueprint = {0};
    blueprint.children.count = 3;
    TEST_ASSERT_EQUAL_INT(4, blueprint_tree_total(&blueprint)); /* 3 children + sentinel */
}

void test_blueprint_tree_is_add_child_sentinel(void)
{
    Blueprint blueprint = {0};
    blueprint.children.count = 2;
    TEST_ASSERT_TRUE(blueprint_tree_is_add_child(&blueprint, 2));
    TEST_ASSERT_FALSE(blueprint_tree_is_add_child(&blueprint, 0));
    TEST_ASSERT_FALSE(blueprint_tree_is_add_child(&blueprint, 1));
}

/* ---- handle_blueprint_navigate ------------------------------------------ */

void test_navigate_from_top_down_enters_tree(void)
{
    Blueprint blueprint = make_named_blueprint("test");
    blueprint.children.count = 1;
    EditorState editor_state = {
        .blueprint_tree_index = -1,
        .blueprint_attr_index = -1,
    };

    handle_blueprint_navigate(&blueprint, &editor_state, 1);

    TEST_ASSERT_EQUAL_INT(0, editor_state.blueprint_tree_index);
    TEST_ASSERT_EQUAL_INT(-1, editor_state.blueprint_attr_index);
    test_attr_set_free_local(&blueprint.attrs);
}

void test_navigate_from_top_up_enters_attrs(void)
{
    Blueprint blueprint = make_named_blueprint("test");
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint.attrs, "speed", 10));
    EditorState editor_state = {
        .blueprint_tree_index = -1,
        .blueprint_attr_index = -1,
    };

    handle_blueprint_navigate(&blueprint, &editor_state, -1);

    /* Should go to attr sentinel (count of attrs = add sentinel) */
    TEST_ASSERT_EQUAL_INT(-1, editor_state.blueprint_tree_index);
    TEST_ASSERT_EQUAL_INT(blueprint.attrs.entries.count, editor_state.blueprint_attr_index);
    test_attr_set_free_local(&blueprint.attrs);
}

void test_navigate_tree_down_past_end_wraps_to_attrs(void)
{
    Blueprint blueprint = make_named_blueprint("test");
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint.attrs, "speed", 10));
    /* tree_total = children(0) + 1(sentinel) = 1 */
    EditorState editor_state = {
        .blueprint_tree_index = 0, /* on ADD CHILD sentinel */
        .blueprint_attr_index = -1,
    };

    handle_blueprint_navigate(&blueprint, &editor_state, 1);

    /* Should transition from tree to attr section */
    TEST_ASSERT_EQUAL_INT(-1, editor_state.blueprint_tree_index);
    TEST_ASSERT_EQUAL_INT(0, editor_state.blueprint_attr_index);
    test_attr_set_free_local(&blueprint.attrs);
}

void test_navigate_tree_up_past_start_goes_to_top(void)
{
    Blueprint blueprint = make_named_blueprint("test");
    EditorState editor_state = {
        .blueprint_tree_index = 0,
        .blueprint_attr_index = -1,
    };

    handle_blueprint_navigate(&blueprint, &editor_state, -1);

    TEST_ASSERT_EQUAL_INT(-1, editor_state.blueprint_tree_index);
    TEST_ASSERT_EQUAL_INT(-1, editor_state.blueprint_attr_index);
    test_attr_set_free_local(&blueprint.attrs);
}

void test_navigate_attrs_up_past_start_goes_to_tree(void)
{
    Blueprint blueprint = make_named_blueprint("test");
    blueprint.children.count = 2;
    EditorState editor_state = {
        .blueprint_tree_index = -1,
        .blueprint_attr_index = 0,
    };

    handle_blueprint_navigate(&blueprint, &editor_state, -1);

    /* Should go to last tree item */
    TEST_ASSERT_EQUAL_INT(blueprint_tree_total(&blueprint) - 1, editor_state.blueprint_tree_index);
    TEST_ASSERT_EQUAL_INT(-1, editor_state.blueprint_attr_index);
    test_attr_set_free_local(&blueprint.attrs);
}

/* ---- handle_blueprint_cancel -------------------------------------------- */

void test_cancel_from_attr_clears_attr(void)
{
    EditorState editor_state = {
        .selected_blueprint_index = 0,
        .blueprint_tree_index = -1,
        .blueprint_attr_index = 2,
    };

    handle_blueprint_cancel(&editor_state);

    TEST_ASSERT_EQUAL_INT(-1, editor_state.blueprint_attr_index);
    TEST_ASSERT_EQUAL_INT(0, editor_state.selected_blueprint_index);
}

void test_cancel_from_tree_clears_tree(void)
{
    EditorState editor_state = {
        .selected_blueprint_index = 0,
        .blueprint_tree_index = 1,
        .blueprint_attr_index = -1,
    };

    handle_blueprint_cancel(&editor_state);

    TEST_ASSERT_EQUAL_INT(-1, editor_state.blueprint_tree_index);
    TEST_ASSERT_EQUAL_INT(0, editor_state.selected_blueprint_index);
}

void test_cancel_from_top_deselects_blueprint(void)
{
    EditorState editor_state = {
        .selected_blueprint_index = 0,
        .blueprint_tree_index = -1,
        .blueprint_attr_index = -1,
    };

    handle_blueprint_cancel(&editor_state);

    TEST_ASSERT_EQUAL_INT(-1, editor_state.selected_blueprint_index);
}

/* ---- handle_blueprint_select -------------------------------------------- */

void test_select_on_add_child_sentinel_opens_fuzzy_finder(void)
{
    GameState state = {0};
    state.gamedata.blueprints.entries.alloc = test_heap_alloc;
    Blueprint blueprint = make_named_blueprint("npc");
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, blueprint));

    EditorState editor_state = {
        .selected_blueprint_index = 0,
        .blueprint_tree_index = 0, /* ADD CHILD sentinel (no children) */
        .blueprint_attr_index = -1,
    };
    UndoHistory undo_history = {0};

    handle_blueprint_select(&state, &editor_state, &undo_history);

    TEST_ASSERT_TRUE(editor_state.adding_child);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_FUZZY_FINDER, editor_state.sub_mode);

    test_blueprint_table_free_local(&state.gamedata.blueprints);
}

void test_select_on_add_attr_sentinel_opens_fuzzy_finder(void)
{
    GameState state = {0};
    state.gamedata.blueprints.entries.alloc = test_heap_alloc;
    Blueprint blueprint = make_named_blueprint("npc");
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint.attrs, "speed", 10));
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, blueprint));

    EditorState editor_state = {
        .selected_blueprint_index = 0,
        .blueprint_tree_index = -1,
        .blueprint_attr_index = state.gamedata.blueprints.entries.data[0].attrs.entries.count, /* +ADD sentinel */
    };
    UndoHistory undo_history = {0};

    handle_blueprint_select(&state, &editor_state, &undo_history);

    TEST_ASSERT_TRUE(editor_state.adding_blueprint_attr);
    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_FUZZY_FINDER, editor_state.sub_mode);

    test_blueprint_table_free_local(&state.gamedata.blueprints);
}

void test_select_on_bool_attr_toggles(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);
    state.gamedata.blueprints.entries = vec_blueprint_new(alloc);

    Blueprint blueprint = {0};
    blueprint.attrs.entries = vec_attribute_new(alloc);
    blueprint.children = vec_blueprint_child_new(alloc);
    blueprint.rules = vec_rule_new(alloc);
    (void)attr_set_string(&alloc, &blueprint.attrs, (AttrStringPair){"name", "npc"});
    (void)attr_set_bool(&alloc, &blueprint.attrs, "visible", false);
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, blueprint);

    int visible_index = -1;
    for (int index = 0; index < state.gamedata.blueprints.entries.data[0].attrs.entries.count; index++) {
        if (strcmp(state.gamedata.blueprints.entries.data[0].attrs.entries.data[index].name.ptr, "visible") == 0) {
            visible_index = index;
        }
    }
    TEST_ASSERT_TRUE(visible_index >= 0);

    EditorState editor_state = {
        .selected_blueprint_index = 0,
        .blueprint_tree_index = -1,
        .blueprint_attr_index = visible_index,
    };
    UndoHistory undo_history = {0};

    handle_blueprint_select(&state, &editor_state, &undo_history);

    const Attribute *attr = &state.gamedata.blueprints.entries.data[0].attrs.entries.data[visible_index];
    TEST_ASSERT_TRUE(attr->value.b);
    TEST_ASSERT_EQUAL_INT(1, undo_history_new_entry_fake.call_count);

    arena_free(&state.gamedata_arena);
}

void test_select_on_int_attr_enters_edit_mode(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);
    state.gamedata.blueprints.entries = vec_blueprint_new(alloc);

    Blueprint blueprint = {0};
    blueprint.attrs.entries = vec_attribute_new(alloc);
    blueprint.children = vec_blueprint_child_new(alloc);
    blueprint.rules = vec_rule_new(alloc);
    (void)attr_set_string(&alloc, &blueprint.attrs, (AttrStringPair){"name", "npc"});
    (void)attr_set_int(&alloc, &blueprint.attrs, "speed", 42);
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, blueprint);

    int speed_index = -1;
    for (int index = 0; index < state.gamedata.blueprints.entries.data[0].attrs.entries.count; index++) {
        if (strcmp(state.gamedata.blueprints.entries.data[0].attrs.entries.data[index].name.ptr, "speed") == 0) {
            speed_index = index;
        }
    }
    TEST_ASSERT_TRUE(speed_index >= 0);

    EditorState editor_state = {
        .selected_blueprint_index = 0,
        .blueprint_tree_index = -1,
        .blueprint_attr_index = speed_index,
    };
    UndoHistory undo_history = {0};

    handle_blueprint_select(&state, &editor_state, &undo_history);

    TEST_ASSERT_EQUAL_INT(EDITOR_SUB_ATTR_EDIT, editor_state.sub_mode);
    TEST_ASSERT_EQUAL_INT(42, editor_state.saved_attr_int);

    arena_free(&state.gamedata_arena);
}

/* ---- handle_blueprint_delete -------------------------------------------- */

void test_delete_attr_removes_from_blueprint(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);
    state.gamedata.blueprints.entries = vec_blueprint_new(alloc);

    Blueprint blueprint = {0};
    blueprint.attrs.entries = vec_attribute_new(alloc);
    blueprint.children = vec_blueprint_child_new(alloc);
    blueprint.rules = vec_rule_new(alloc);
    (void)attr_set_string(&alloc, &blueprint.attrs, (AttrStringPair){"name", "npc"});
    (void)attr_set_int(&alloc, &blueprint.attrs, "speed", 10);
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, blueprint);

    int speed_index = -1;
    for (int index = 0; index < state.gamedata.blueprints.entries.data[0].attrs.entries.count; index++) {
        if (strcmp(state.gamedata.blueprints.entries.data[0].attrs.entries.data[index].name.ptr, "speed") == 0) {
            speed_index = index;
        }
    }
    TEST_ASSERT_TRUE(speed_index >= 0);

    EditorState editor_state = {
        .selected_blueprint_index = 0,
        .blueprint_tree_index = -1,
        .blueprint_attr_index = speed_index,
    };
    UndoHistory undo_history = {0};

    handle_blueprint_delete(&state, &editor_state, &undo_history);

    TEST_ASSERT_NULL(attr_get(&state.gamedata.blueprints.entries.data[0].attrs, "speed"));
    TEST_ASSERT_EQUAL_INT(1, undo_history_new_entry_fake.call_count);

    arena_free(&state.gamedata_arena);
}

void test_delete_name_attr_is_blocked(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);
    state.gamedata.blueprints.entries = vec_blueprint_new(alloc);

    Blueprint blueprint = {0};
    blueprint.attrs.entries = vec_attribute_new(alloc);
    blueprint.children = vec_blueprint_child_new(alloc);
    blueprint.rules = vec_rule_new(alloc);
    (void)attr_set_string(&alloc, &blueprint.attrs, (AttrStringPair){"name", "npc"});
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, blueprint);

    EditorState editor_state = {
        .selected_blueprint_index = 0,
        .blueprint_tree_index = -1,
        .blueprint_attr_index = 0,
    };
    UndoHistory undo_history = {0};
    RESET_FAKE(undo_history_new_entry);

    handle_blueprint_delete(&state, &editor_state, &undo_history);

    TEST_ASSERT_NOT_NULL(attr_get(&state.gamedata.blueprints.entries.data[0].attrs, "name"));
    TEST_ASSERT_EQUAL_INT(0, undo_history_new_entry_fake.call_count);

    arena_free(&state.gamedata_arena);
}

void test_delete_child_calls_remove(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);
    state.gamedata.blueprints.entries = vec_blueprint_new(alloc);

    Blueprint blueprint = {0};
    blueprint.attrs.entries = vec_attribute_new(alloc);
    blueprint.children = vec_blueprint_child_new(alloc);
    blueprint.rules = vec_rule_new(alloc);
    (void)attr_set_string(&alloc, &blueprint.attrs, (AttrStringPair){"name", "npc"});
    /* Push two dummy children so count = 2 */
    BlueprintChild dummy_child = {0};
    dummy_child.blueprint_name = str_new(alloc);
    (void)str_from_cstr(&dummy_child.blueprint_name, "child_a");
    (void)vec_blueprint_child_push(&blueprint.children, dummy_child);
    BlueprintChild dummy_child_b = {0};
    dummy_child_b.blueprint_name = str_new(alloc);
    (void)str_from_cstr(&dummy_child_b.blueprint_name, "child_b");
    (void)vec_blueprint_child_push(&blueprint.children, dummy_child_b);
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, blueprint);

    EditorState editor_state = {
        .selected_blueprint_index = 0,
        .blueprint_tree_index = 0,
        .blueprint_attr_index = -1,
    };
    UndoHistory undo_history = {0};
    RESET_FAKE(remove_blueprint_child);

    handle_blueprint_delete(&state, &editor_state, &undo_history);

    TEST_ASSERT_EQUAL_INT(1, remove_blueprint_child_fake.call_count);

    arena_free(&state.gamedata_arena);
}

/* ---- create_blank_blueprint --------------------------------------------- */

void test_create_blank_blueprint(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);
    state.gamedata.blueprints.entries = vec_blueprint_new(alloc);

    EditorState editor_state = {.selected_blueprint_index = -1};
    UndoHistory undo_history = {0};

    create_blank_blueprint(&state, &editor_state, &undo_history, "new_thing");

    TEST_ASSERT_EQUAL_INT(1, state.gamedata.blueprints.entries.count);
    TEST_ASSERT_EQUAL_STRING("new_thing", attr_get_string(&state.gamedata.blueprints.entries.data[0].attrs, "name"));
    TEST_ASSERT_EQUAL_INT(0, editor_state.selected_blueprint_index);
    TEST_ASSERT_EQUAL_INT(1, undo_history_new_entry_fake.call_count);

    arena_free(&state.gamedata_arena);
}

/* ---- duplicate_blueprint ------------------------------------------------ */

void test_duplicate_blueprint_copies_attrs(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);
    state.gamedata.blueprints.entries = vec_blueprint_new(alloc);

    /* Create source blueprint */
    Blueprint source = {0};
    source.attrs.entries = vec_attribute_new(alloc);
    source.children = vec_blueprint_child_new(alloc);
    source.rules = vec_rule_new(alloc);
    (void)attr_set_string(&alloc, &source.attrs, (AttrStringPair){"name", "original"});
    (void)attr_set_int(&alloc, &source.attrs, "speed", 42);
    (void)attr_set_bool(&alloc, &source.attrs, "visible", true);
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, source);

    EditorState editor_state = {.selected_blueprint_index = 0};
    UndoHistory undo_history = {0};

    duplicate_blueprint(&state, &editor_state, &undo_history, "copy");

    TEST_ASSERT_EQUAL_INT(2, state.gamedata.blueprints.entries.count);
    Blueprint *copy = &state.gamedata.blueprints.entries.data[1];
    TEST_ASSERT_EQUAL_STRING("copy", attr_get_string(&copy->attrs, "name"));
    TEST_ASSERT_EQUAL_INT(42, attr_get_int(&copy->attrs, "speed", 0));
    TEST_ASSERT_TRUE(attr_get_bool(&copy->attrs, "visible", false));
    TEST_ASSERT_EQUAL_INT(1, editor_state.selected_blueprint_index);

    arena_free(&state.gamedata_arena);
}

void test_duplicate_blueprint_copies_children(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);
    state.gamedata.blueprints.entries = vec_blueprint_new(alloc);

    Blueprint source = {0};
    source.attrs.entries = vec_attribute_new(alloc);
    source.children = vec_blueprint_child_new(alloc);
    source.rules = vec_rule_new(alloc);
    (void)attr_set_string(&alloc, &source.attrs, (AttrStringPair){"name", "parent"});

    BlueprintChild child = {0};
    child.blueprint_name = str_new(alloc);
    (void)str_from_cstr(&child.blueprint_name, "weapon");
    child.tag = str_new(alloc);
    (void)str_from_cstr(&child.tag, "main");
    child.offset = (Vector2){10.0F, 20.0F};
    (void)vec_blueprint_child_push(&source.children, child);
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, source);

    EditorState editor_state = {.selected_blueprint_index = 0};
    UndoHistory undo_history = {0};

    duplicate_blueprint(&state, &editor_state, &undo_history, "parent_copy");

    Blueprint *copy = &state.gamedata.blueprints.entries.data[1];
    TEST_ASSERT_EQUAL_INT(1, copy->children.count);
    TEST_ASSERT_NOT_NULL(copy->children.data);
    TEST_ASSERT_EQUAL_STRING("weapon", copy->children.data[0].blueprint_name.ptr);
    TEST_ASSERT_EQUAL_STRING("main", copy->children.data[0].tag.ptr);
    TEST_ASSERT_EQUAL_FLOAT(10.0F, copy->children.data[0].offset.x);
    TEST_ASSERT_EQUAL_FLOAT(20.0F, copy->children.data[0].offset.y);

    arena_free(&state.gamedata_arena);
}

/* ---- delete_blueprint --------------------------------------------------- */

void test_delete_blueprint_removes_from_vec(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);
    state.gamedata.blueprints.entries = vec_blueprint_new(alloc);
    state.gamedata.current_level.entities = vec_entity_new(alloc);

    Blueprint bp_a = {0};
    bp_a.attrs.entries = vec_attribute_new(alloc);
    bp_a.children = vec_blueprint_child_new(alloc);
    bp_a.rules = vec_rule_new(alloc);
    (void)attr_set_string(&alloc, &bp_a.attrs, (AttrStringPair){"name", "alpha"});
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, bp_a);

    Blueprint bp_b = {0};
    bp_b.attrs.entries = vec_attribute_new(alloc);
    bp_b.children = vec_blueprint_child_new(alloc);
    bp_b.rules = vec_rule_new(alloc);
    (void)attr_set_string(&alloc, &bp_b.attrs, (AttrStringPair){"name", "beta"});
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, bp_b);

    EditorState editor_state = {.blueprint_list_scroll = 0};
    UndoHistory undo_history = {0};

    delete_blueprint(&state, &editor_state, &undo_history);

    TEST_ASSERT_EQUAL_INT(1, state.gamedata.blueprints.entries.count);
    /* "beta" swapped into position 0 */
    TEST_ASSERT_EQUAL_STRING("beta", attr_get_string(&state.gamedata.blueprints.entries.data[0].attrs, "name"));
    TEST_ASSERT_EQUAL_INT(1, undo_history_new_entry_fake.call_count);

    arena_free(&state.gamedata_arena);
}

/* ---- main --------------------------------------------------------------- */

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();

    /* Tree helpers */
    RUN_TEST(test_blueprint_tree_total_no_children);
    RUN_TEST(test_blueprint_tree_total_with_children);
    RUN_TEST(test_blueprint_tree_is_add_child_sentinel);

    /* Navigation */
    RUN_TEST(test_navigate_from_top_down_enters_tree);
    RUN_TEST(test_navigate_from_top_up_enters_attrs);
    RUN_TEST(test_navigate_tree_down_past_end_wraps_to_attrs);
    RUN_TEST(test_navigate_tree_up_past_start_goes_to_top);
    RUN_TEST(test_navigate_attrs_up_past_start_goes_to_tree);

    /* Cancel */
    RUN_TEST(test_cancel_from_attr_clears_attr);
    RUN_TEST(test_cancel_from_tree_clears_tree);
    RUN_TEST(test_cancel_from_top_deselects_blueprint);

    /* Select */
    RUN_TEST(test_select_on_add_child_sentinel_opens_fuzzy_finder);
    RUN_TEST(test_select_on_add_attr_sentinel_opens_fuzzy_finder);
    RUN_TEST(test_select_on_bool_attr_toggles);
    RUN_TEST(test_select_on_int_attr_enters_edit_mode);

    /* Delete */
    RUN_TEST(test_delete_attr_removes_from_blueprint);
    RUN_TEST(test_delete_name_attr_is_blocked);
    RUN_TEST(test_delete_child_calls_remove);

    /* Create */
    RUN_TEST(test_create_blank_blueprint);

    /* Duplicate */
    RUN_TEST(test_duplicate_blueprint_copies_attrs);
    RUN_TEST(test_duplicate_blueprint_copies_children);

    /* Delete blueprint */
    RUN_TEST(test_delete_blueprint_removes_from_vec);

    return UNITY_END();
}
