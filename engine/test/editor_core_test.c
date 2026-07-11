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

/* Cross-file editor fakes: input_func.c */
FAKE_VALUE_FUNC(bool, input_pressed, const InputState *, const BindingStore *, InputAction);
FAKE_VALUE_FUNC(bool, input_held, const InputState *, const BindingStore *, InputAction);
FAKE_VALUE_FUNC(float, input_axis, const InputState *, const BindingStore *, InputAxis);
FAKE_VALUE_FUNC(Vector2, input_axis_pair, const InputState *, const BindingStore *, InputAxis, InputAxis);

/* Cross-file editor fakes: draw.c */
FAKE_VOID_FUNC(update_editor_camera, Camera2D *, const InputState *, const BindingStore *, float);
FAKE_VALUE_FUNC(int, find_nearest_entity, const Level *, Vector2);

/* Cross-file editor fakes: attr.c */
FAKE_VOID_FUNC(dispatch_attr_type_change, GameState *, EditorState *, int, UndoHistory *);
FAKE_VOID_FUNC(dispatch_child_props, GameState *, EditorState *, int);

/* Cross-file editor fakes: child.c */
FAKE_VOID_FUNC(remove_blueprint_child, GameState *, EditorState *, UndoHistory *, int);
FAKE_VALUE_FUNC(int, find_child_entity, const Level *, int, const char *, const char *);

/* Cross-file editor fakes: widgets.c */
FAKE_VOID_FUNC(fuzzy_finder_build_items, GameState *, EditorState *);

/* Cross-file editor fakes: anim.c */
FAKE_VOID_FUNC(enter_anim_mode, GameState *, EditorState *);

/* Cross-file editor fakes: rule.c */
FAKE_VOID_FUNC(enter_rule_mode, GameState *, EditorState *);

/* External module fakes */
FAKE_VALUE_FUNC(const AttrSet *, entity_resolve_defaults, const GameState *, int);
FAKE_VALUE_FUNC(const Blueprint *, blueprint_find, const BlueprintTable *, const char *);
FAKE_VALUE_FUNC(int, level_find_entity_by_id, const Level *, int);
/* Cross-file fakes: level.c/game.c (S5.7 paste path) */
FAKE_VALUE_FUNC(bool,
                level_spawn_entity,
                Diag *,
                Level *,
                const Blueprint *,
                Vector2,
                const BlueprintTable *,
                TextureLookupFn,
                void *,
                Allocator *);
FAKE_VALUE_FUNC(Texture2D *, texture_registry_lookup, const char *, void *);
FAKE_VALUE_FUNC(bool, setup_current_level_runtime, Diag *, GameState *);
FAKE_VOID_FUNC(undo_history_new_entry, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint, Strv);
FAKE_VOID_FUNC(undo_history_step_back, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint);
FAKE_VOID_FUNC(undo_history_step_forward, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint);
FAKE_VALUE_FUNC(Strv, undo_history_description, const UndoHistory *);
/* S8.7c: handle_drag_input's CONFIRM branch now calls this net_session.h
 * seam once per moved entity; faked here so core.c links without the whole
 * network stack. The end-to-end drag-commit -> op -> converge path is
 * covered black-box in integration_test.c. */
FAKE_VOID_FUNC(network_editor_commit_move, GameState *, int, Vector2);
/* S8.7d2: core.c's grab sites gate on this seam and the drag commit/cancel +
 * deny-drain paths call the release seam; faked here so core.c links without
 * the network stack. try_grab defaults to returning false (fff zero-init), so
 * tests that exercise a grab set network_editor_try_grab_fake.return_val = true
 * first. The end-to-end lock UX is covered black-box in integration_test.c. */
FAKE_VALUE_FUNC(bool, network_editor_try_grab, GameState *, const int *, int);
FAKE_VOID_FUNC(network_editor_release_locks, GameState *, const int *, int);
/* S8.7f3a: core.c's delete sites route the level/gamedata mutation through
 * game.c's extracted cascade and commit the result over the
 * network_editor_commit_delete seam, gated by a replica lock check
 * (network_lock_find, network.c); all three are faked here so core.c links
 * without game.c or the network stack. The end-to-end delete/place
 * convergence is covered black-box in integration_test.c. */
FAKE_VOID_FUNC(game_delete_entity_cascade, GameState *, int);
FAKE_VOID_FUNC(network_editor_commit_delete, GameState *, int);
/* S8.7f3: core.c's networked paste sites route each pasted PLACE through this
 * seam (empty attrs for a bare place, the copy-buffer entry's attrs for a
 * paste); faked here so core.c links without the network stack. The end-to-end
 * paste convergence is covered black-box in integration_test.c. */
FAKE_VOID_FUNC(network_editor_commit_place, GameState *, int, Strv, Vector2, const AttrRecord *, size_t);
FAKE_VALUE_FUNC(EntityLock *, network_lock_find, NetworkState *, int);
/* S8.7f3b: core.c's scene ATTR panel edit sites propagate over these three
 * net_session/network seams (SET/REMOVE ops and the host structural-resync
 * arm), building each SET record from the fresh entity attr; all faked here so
 * core.c links without the network stack. The end-to-end convergence is
 * covered black-box in integration_test.c. */
FAKE_VOID_FUNC(network_editor_commit_set_attr, GameState *, int, AttrRecord);
FAKE_VOID_FUNC(network_editor_commit_remove_attr, GameState *, int, Strv);
FAKE_VOID_FUNC(network_structural_mark_dirty, NetworkState *);
FAKE_VALUE_FUNC(AttrRecord, network_attr_record_from_attribute, int, const Attribute *);
/* S8.7h1: the shared session-undo reroute helpers (defined in core.c, real
 * here) send this reliable event under NET_CLIENT; faked so core.c links
 * without the network stack. The end-to-end reroute is covered black-box in
 * integration_test.c. */
FAKE_VOID_FUNC(network_client_send_reliable_event, NetworkState *, EventRecord);

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

/* Top-level entity: sections = persisted(N)+ADD + runtime(N)+ADD + blueprint(N)+ADD. */
void test_editor_total_attr_count_top_level_no_blueprint(void)
{
    GameState state = {0};
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "health", 5));
    entity_resolve_defaults_fake.return_val = nullptr;

    /* persisted(0)+1 + runtime(2)+1 + blueprint(0)+1 = 5 */
    TEST_ASSERT_EQUAL_INT(5, total_attr_count(&state, &entity));

    test_attr_set_free_local(&entity.attrs);
}

void test_editor_total_attr_count_top_level_with_blueprint(void)
{
    GameState state = {0};
    state.gamedata.blueprints.entries.alloc = test_heap_alloc;
    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &blueprint.attrs, (AttrStringPair){"name", "test_bp"}));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint.attrs, "bp_attr", 42));
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, blueprint);

    Entity entity = {.parent_index = -1};
    entity.id = 1;
    entity.blueprint_name = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&entity.blueprint_name, "test_bp"));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.persisted_attrs, "hp", 42));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "inst_attr", 10));
    Str bp_name = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&bp_name, "test_bp"));
    state.gamedata.entity_blueprints = map_int_str_new(test_heap_alloc);
    (void)map_int_str_set(&state.gamedata.entity_blueprints, entity.id, bp_name);

    entity_resolve_defaults_fake.return_val = &state.gamedata.blueprints.entries.data[0].attrs;
    /* persisted(1)+1 + runtime(1)+1 + blueprint(2)+1 = 7 */
    TEST_ASSERT_EQUAL_INT(7, total_attr_count(&state, &entity));
    entity_resolve_defaults_fake.return_val = nullptr;

    test_attr_set_free_local(&entity.persisted_attrs);
    test_attr_set_free_local(&entity.attrs);
    test_attr_set_free_local(&blueprint.attrs);
    str_free(&entity.blueprint_name);
    str_free(&bp_name);
    map_int_str_free(&state.gamedata.entity_blueprints);
    vec_blueprint_free(&state.gamedata.blueprints.entries);
}

/* Child entity: has a persisted section same as root (S3.3a round-trips
 * child persisted attrs through TOML), even with zero persisted attrs set:
 * persisted(0)+ADD + runtime(N)+ADD + blueprint(N)+ADD. */
void test_editor_total_attr_count_child_includes_persisted(void)
{
    GameState state = {0};
    Entity entity = {.parent_index = 0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));
    entity_resolve_defaults_fake.return_val = nullptr;

    /* persisted(0)+1 + runtime(1)+1 + blueprint(0)+1 = 4 */
    TEST_ASSERT_EQUAL_INT(4, total_attr_count(&state, &entity));

    test_attr_set_free_local(&entity.attrs);
}

/* Child entity with an actual persisted attr set: total_attr_count reflects
 * it, and attr_row_at exposes it (+ its ADD row) under ATTR_SECTION_PERSISTED
 * the same way it does for a root entity. */
void test_editor_total_attr_count_child_with_persisted_attr(void)
{
    GameState state = {0};
    Entity entity = {.parent_index = 3};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.persisted_attrs, "hp", 42));
    entity_resolve_defaults_fake.return_val = nullptr;

    /* persisted(1)+1 + runtime(0)+1 + blueprint(0)+1 = 4 */
    TEST_ASSERT_EQUAL_INT(4, total_attr_count(&state, &entity));

    AttrRow row = attr_row_at(&state, &entity, 0);
    TEST_ASSERT_EQUAL_INT(ATTR_ROW_KIND_ATTR, row.kind);
    TEST_ASSERT_EQUAL_INT(ATTR_SECTION_PERSISTED, row.section);
    TEST_ASSERT_EQUAL_INT(0, row.index_in_section);

    row = attr_row_at(&state, &entity, 1);
    TEST_ASSERT_EQUAL_INT(ATTR_ROW_KIND_ADD, row.kind);
    TEST_ASSERT_EQUAL_INT(ATTR_SECTION_PERSISTED, row.section);

    test_attr_set_free_local(&entity.persisted_attrs);
}

/* ---- is_blueprint_attr -------------------------------------------------- */

void test_editor_is_blueprint_attr_false_for_runtime(void)
{
    GameState state = {0};
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));
    entity_resolve_defaults_fake.return_val = nullptr;

    /* index 0 = persisted ADD sentinel; index 1 = runtime attr 0 */
    TEST_ASSERT_FALSE(is_blueprint_attr(&state, &entity, 1));

    test_attr_set_free_local(&entity.attrs);
}

void test_editor_is_blueprint_attr_true_for_blueprint_section(void)
{
    GameState state = {0};
    state.gamedata.blueprints.entries.alloc = test_heap_alloc;
    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint.attrs, "bp_val", 99));
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, blueprint);

    Entity entity = {.parent_index = -1};
    entity_resolve_defaults_fake.return_val = &state.gamedata.blueprints.entries.data[0].attrs;

    /* Row layout: [0]=persisted ADD, [1]=runtime ADD, [2]=blueprint attr 0 */
    TEST_ASSERT_TRUE(is_blueprint_attr(&state, &entity, 2));
    entity_resolve_defaults_fake.return_val = nullptr;

    test_attr_set_free_local(&state.gamedata.blueprints.entries.data[0].attrs);
    vec_blueprint_free(&state.gamedata.blueprints.entries);
}

/* ---- attr_at_display_index ---------------------------------------------- */

/* Top-level layout with 1 runtime attr, no blueprint:
 *   [0]=persisted ADD, [1]=runtime attr, [2]=runtime ADD, [3]=blueprint ADD. */
void test_editor_attr_at_display_index_runtime(void)
{
    GameState state = {0};
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 42));
    entity_resolve_defaults_fake.return_val = nullptr;

    Attribute *attr = attr_at_display_index(&state, &entity, 1);
    TEST_ASSERT_NOT_NULL(attr);
    TEST_ASSERT_EQUAL_INT(42, attr->value.i);
    /* ADD sentinel returns nullptr */
    TEST_ASSERT_NULL(attr_at_display_index(&state, &entity, 0));

    test_attr_set_free_local(&entity.attrs);
}

/* Top-level layout with persisted(1) + runtime(1) + blueprint(2):
 *   [0]=persisted attr 0, [1]=persisted ADD, [2]=runtime attr 0, [3]=runtime ADD,
 *   [4]=blueprint attr 0, [5]=blueprint attr 1, [6]=blueprint ADD. */
void test_editor_attr_at_display_index_blueprint(void)
{
    GameState state = {0};
    state.gamedata.blueprints.entries.alloc = test_heap_alloc;

    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, (Blueprint){0}));
    Blueprint *blueprint = &state.gamedata.blueprints.entries.data[0];
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &blueprint->attrs, (AttrStringPair){"name", "test_bp"}));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint->attrs, "bp_val", 99));

    Entity entity = {.parent_index = -1};
    entity.blueprint_name = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&entity.blueprint_name, "test_bp"));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.persisted_attrs, "hp", 50));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "inst_val", 10));
    entity_resolve_defaults_fake.return_val = &blueprint->attrs;

    /* persisted attr at index 0 */
    Attribute *persisted = attr_at_display_index(&state, &entity, 0);
    TEST_ASSERT_NOT_NULL(persisted);
    TEST_ASSERT_EQUAL_INT(50, persisted->value.i);

    /* runtime attr at index 2 */
    Attribute *runtime = attr_at_display_index(&state, &entity, 2);
    TEST_ASSERT_NOT_NULL(runtime);
    TEST_ASSERT_EQUAL_INT(10, runtime->value.i);

    /* blueprint attr 1 (bp_val=99) at index 5 */
    Attribute *bp_attr = attr_at_display_index(&state, &entity, 5);
    TEST_ASSERT_NOT_NULL(bp_attr);
    TEST_ASSERT_EQUAL_INT(99, bp_attr->value.i);
    entity_resolve_defaults_fake.return_val = nullptr;

    str_free(&entity.blueprint_name);
    test_attr_set_free_local(&entity.persisted_attrs);
    test_attr_set_free_local(&entity.attrs);
    test_blueprint_table_free_local(&state.gamedata.blueprints);
}

/* ---- attr_row_at -------------------------------------------------------- */

/* Walk every display index for a top-level entity with persisted(1) + runtime(2)
 * + blueprint(1) and assert the returned kind/section/index_in_section for each
 * row. Layout:
 *   [0]=persisted attr 0
 *   [1]=persisted ADD
 *   [2]=runtime attr 0
 *   [3]=runtime attr 1
 *   [4]=runtime ADD
 *   [5]=blueprint attr 0
 *   [6]=blueprint ADD
 */
void test_editor_attr_row_at_walks_sections(void)
{
    GameState state = {0};
    state.gamedata.blueprints.entries.alloc = test_heap_alloc;
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, (Blueprint){0}));
    Blueprint *blueprint = &state.gamedata.blueprints.entries.data[0];
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint->attrs, "bp_val", 7));

    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.persisted_attrs, "hp", 50));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "damage", 20));
    entity_resolve_defaults_fake.return_val = &blueprint->attrs;

    AttrRow row;

    row = attr_row_at(&state, &entity, 0);
    TEST_ASSERT_EQUAL_INT(ATTR_ROW_KIND_ATTR, row.kind);
    TEST_ASSERT_EQUAL_INT(ATTR_SECTION_PERSISTED, row.section);
    TEST_ASSERT_EQUAL_INT(0, row.index_in_section);

    row = attr_row_at(&state, &entity, 1);
    TEST_ASSERT_EQUAL_INT(ATTR_ROW_KIND_ADD, row.kind);
    TEST_ASSERT_EQUAL_INT(ATTR_SECTION_PERSISTED, row.section);

    row = attr_row_at(&state, &entity, 2);
    TEST_ASSERT_EQUAL_INT(ATTR_ROW_KIND_ATTR, row.kind);
    TEST_ASSERT_EQUAL_INT(ATTR_SECTION_RUNTIME, row.section);
    TEST_ASSERT_EQUAL_INT(0, row.index_in_section);

    row = attr_row_at(&state, &entity, 3);
    TEST_ASSERT_EQUAL_INT(ATTR_ROW_KIND_ATTR, row.kind);
    TEST_ASSERT_EQUAL_INT(ATTR_SECTION_RUNTIME, row.section);
    TEST_ASSERT_EQUAL_INT(1, row.index_in_section);

    row = attr_row_at(&state, &entity, 4);
    TEST_ASSERT_EQUAL_INT(ATTR_ROW_KIND_ADD, row.kind);
    TEST_ASSERT_EQUAL_INT(ATTR_SECTION_RUNTIME, row.section);

    row = attr_row_at(&state, &entity, 5);
    TEST_ASSERT_EQUAL_INT(ATTR_ROW_KIND_ATTR, row.kind);
    TEST_ASSERT_EQUAL_INT(ATTR_SECTION_BLUEPRINT, row.section);
    TEST_ASSERT_EQUAL_INT(0, row.index_in_section);

    row = attr_row_at(&state, &entity, 6);
    TEST_ASSERT_EQUAL_INT(ATTR_ROW_KIND_ADD, row.kind);
    TEST_ASSERT_EQUAL_INT(ATTR_SECTION_BLUEPRINT, row.section);

    /* Out of range → INVALID. */
    row = attr_row_at(&state, &entity, 7);
    TEST_ASSERT_EQUAL_INT(ATTR_ROW_KIND_INVALID, row.kind);

    row = attr_row_at(&state, &entity, -1);
    TEST_ASSERT_EQUAL_INT(ATTR_ROW_KIND_INVALID, row.kind);

    entity_resolve_defaults_fake.return_val = nullptr;
    test_attr_set_free_local(&entity.persisted_attrs);
    test_attr_set_free_local(&entity.attrs);
    test_blueprint_table_free_local(&state.gamedata.blueprints);
}

/* Child entity with no persisted attrs set: the persisted section still
 * appears as its own ADD sentinel row before runtime, same as root.
 * Layout: [0]=persisted ADD, [1]=runtime attr 0, [2]=runtime ADD,
 * [3]=blueprint ADD. */
void test_editor_attr_row_at_child_includes_persisted_add(void)
{
    GameState state = {0};
    Entity entity = {.parent_index = 0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "hp", 5));
    entity_resolve_defaults_fake.return_val = nullptr;

    AttrRow row = attr_row_at(&state, &entity, 0);
    TEST_ASSERT_EQUAL_INT(ATTR_ROW_KIND_ADD, row.kind);
    TEST_ASSERT_EQUAL_INT(ATTR_SECTION_PERSISTED, row.section);

    row = attr_row_at(&state, &entity, 1);
    TEST_ASSERT_EQUAL_INT(ATTR_ROW_KIND_ATTR, row.kind);
    TEST_ASSERT_EQUAL_INT(ATTR_SECTION_RUNTIME, row.section);
    TEST_ASSERT_EQUAL_INT(0, row.index_in_section);

    row = attr_row_at(&state, &entity, 2);
    TEST_ASSERT_EQUAL_INT(ATTR_ROW_KIND_ADD, row.kind);
    TEST_ASSERT_EQUAL_INT(ATTR_SECTION_RUNTIME, row.section);

    row = attr_row_at(&state, &entity, 3);
    TEST_ASSERT_EQUAL_INT(ATTR_ROW_KIND_ADD, row.kind);
    TEST_ASSERT_EQUAL_INT(ATTR_SECTION_BLUEPRINT, row.section);

    test_attr_set_free_local(&entity.attrs);
}

void test_editor_attr_at_display_index_out_of_range(void)
{
    GameState state = {0};
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));
    entity_resolve_defaults_fake.return_val = nullptr;

    Attribute *attr = attr_at_display_index(&state, &entity, 99);
    TEST_ASSERT_NULL(attr);

    test_attr_set_free_local(&entity.attrs);
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

/* ---- delete_selected_entity / delete_selection_networked ----------------- */

/* S8.7f3a: the level/gamedata mutation core moved to game.c
 * (game_delete_entity_cascade, faked in this TU -- its cascade/compaction
 * behavior is covered by the black-box integration tests). What remains
 * here is delete_selected_entity's editor-specific contract: delegate to
 * the cascade with the selected id, prune watches whose ids no longer
 * resolve, and reset the stale tree cursor. */
void test_editor_delete_selected_delegates_and_prunes_watches(void)
{
    RESET_FAKE(level_find_entity_by_id);
    RESET_FAKE(game_delete_entity_cascade);
    GameState state = {0};
    EditorState editor_state = {.selected_entity_id = 10, .selected_tree_index = 3};
    WatchList watches = {0};
    watches.watch_ids[0] = 10;
    watches.watch_ids[1] = 20;
    watches.count = 2;

    /* Call 1: the selection resolve (index 0). Calls 2-3: the watch prune --
     * the deleted id 10 no longer resolves, id 20 still does. */
    int resolve_sequence[3] = {0, -1, 1};
    SET_RETURN_SEQ(level_find_entity_by_id, resolve_sequence, 3);

    delete_selected_entity(&state, &editor_state, &watches);

    TEST_ASSERT_EQUAL_INT(1, game_delete_entity_cascade_fake.call_count);
    TEST_ASSERT_EQUAL_INT(10, game_delete_entity_cascade_fake.arg1_val);
    TEST_ASSERT_EQUAL_INT(1, watches.count);
    TEST_ASSERT_EQUAL_INT(20, watches.watch_ids[0]);
    TEST_ASSERT_EQUAL_INT(-1, editor_state.selected_tree_index);
}

/* S8.7f3a: under NET_CLIENT a selection whose replica lock is held by
 * ANOTHER player is refused up front -- toast, no cascade, no commit,
 * returns false so the caller skips its undo push. */
void test_editor_delete_refused_when_locked_by_other(void)
{
    RESET_FAKE(level_find_entity_by_id);
    RESET_FAKE(game_delete_entity_cascade);
    RESET_FAKE(network_editor_commit_delete);
    RESET_FAKE(network_lock_find);
    GameState state = {0};
    state.network.mode = NET_CLIENT;
    state.network.local_player_id = 2;
    EditorState editor_state = {.selected_entity_id = 10};
    WatchList watches = {0};
    EntityLock foreign_lock = {.entity_id = 10, .holder_player_id = 5, .active = true};
    network_lock_find_fake.return_val = &foreign_lock;

    TEST_ASSERT_FALSE(delete_selection_networked(&state, &editor_state, &watches));

    TEST_ASSERT_EQUAL_INT(0, game_delete_entity_cascade_fake.call_count);
    TEST_ASSERT_EQUAL_INT(0, network_editor_commit_delete_fake.call_count);
    TEST_ASSERT_TRUE(editor_state.toast_text.len > 0);
    /* foreign_lock is stack-local: clear the fake so its stored return_val
     * never outlives this frame. */
    RESET_FAKE(network_lock_find);
}

/* S8.7f3a: an unlocked (or self-held) selection deletes locally and commits
 * the captured id over the network seam exactly once. */
void test_editor_delete_commits_over_network_seam(void)
{
    RESET_FAKE(level_find_entity_by_id);
    RESET_FAKE(game_delete_entity_cascade);
    RESET_FAKE(network_editor_commit_delete);
    RESET_FAKE(network_lock_find);
    GameState state = {0};
    state.network.mode = NET_CLIENT;
    state.network.local_player_id = 2;
    EditorState editor_state = {.selected_entity_id = 10};
    WatchList watches = {0};
    network_lock_find_fake.return_val = nullptr;
    level_find_entity_by_id_fake.return_val = 0;

    TEST_ASSERT_TRUE(delete_selection_networked(&state, &editor_state, &watches));

    TEST_ASSERT_EQUAL_INT(1, game_delete_entity_cascade_fake.call_count);
    TEST_ASSERT_EQUAL_INT(10, game_delete_entity_cascade_fake.arg1_val);
    TEST_ASSERT_EQUAL_INT(1, network_editor_commit_delete_fake.call_count);
    TEST_ASSERT_EQUAL_INT(10, network_editor_commit_delete_fake.arg1_val);
}

/* ---- S8.7f3b: scene ATTR panel network propagation ---------------------- */

/* A connected client is refused a BLUEPRINT-section edit up front (blueprint
 * defaults are structural, host-only) -- toast, and the lock table is never
 * even consulted (the blueprint block short-circuits before it). */
void test_editor_attr_edit_permitted_blocks_blueprint_on_client(void)
{
    RESET_FAKE(network_lock_find);
    GameState state = {0};
    state.network.mode = NET_CLIENT;
    EditorState editor_state = {0};

    TEST_ASSERT_FALSE(editor_attr_edit_permitted(&state, 7, &editor_state, ATTR_SECTION_BLUEPRINT));

    TEST_ASSERT_TRUE(editor_state.toast_text.len > 0);
    TEST_ASSERT_EQUAL_INT(0, network_lock_find_fake.call_count);
}

/* A connected client is refused an entity-section edit whose entity another
 * player holds -- the same fast local lock gate moves/deletes use. */
void test_editor_attr_edit_permitted_blocks_locked_entity_on_client(void)
{
    RESET_FAKE(network_lock_find);
    GameState state = {0};
    state.network.mode = NET_CLIENT;
    state.network.local_player_id = 2;
    EditorState editor_state = {0};
    EntityLock foreign_lock = {.entity_id = 7, .holder_player_id = 5, .active = true};
    network_lock_find_fake.return_val = &foreign_lock;

    TEST_ASSERT_FALSE(editor_attr_edit_permitted(&state, 7, &editor_state, ATTR_SECTION_RUNTIME));

    TEST_ASSERT_TRUE(editor_state.toast_text.len > 0);
    /* foreign_lock is stack-local: clear the fake so its return_val never
     * outlives this frame. */
    RESET_FAKE(network_lock_find);
}

/* The host (and offline) always proceed -- no gate on either section. */
void test_editor_attr_edit_permitted_allows_on_host(void)
{
    GameState state = {0};
    state.network.mode = NET_HOSTING;
    EditorState editor_state = {0};

    TEST_ASSERT_TRUE(editor_attr_edit_permitted(&state, 7, &editor_state, ATTR_SECTION_BLUEPRINT));
    TEST_ASSERT_TRUE(editor_attr_edit_permitted(&state, 7, &editor_state, ATTR_SECTION_RUNTIME));
}

/* A blueprint-section SET propagates by arming the host structural resync (the
 * f2 gap closer), NOT the per-attr op stream. */
void test_editor_attr_propagate_set_blueprint_marks_dirty(void)
{
    RESET_FAKE(network_structural_mark_dirty);
    RESET_FAKE(network_editor_commit_set_attr);
    GameState state = {0};
    state.network.mode = NET_HOSTING;

    /* attr is unused on the blueprint path (it only arms the resync). */
    editor_attr_propagate_set(&state, 7, nullptr, ATTR_SECTION_BLUEPRINT);

    TEST_ASSERT_EQUAL_INT(1, network_structural_mark_dirty_fake.call_count);
    TEST_ASSERT_EQUAL_INT(0, network_editor_commit_set_attr_fake.call_count);
}

/* An entity-section SET reads the fresh attr back off the entity and forwards
 * it over the SET_ATTR seam, carrying the entity id. */
void test_editor_attr_propagate_set_entity_calls_seam(void)
{
    RESET_FAKE(network_editor_commit_set_attr);
    RESET_FAKE(network_attr_record_from_attribute);
    RESET_FAKE(network_structural_mark_dirty);
    GameState state = {0};
    state.network.mode = NET_HOSTING;

    AttrSet attrs = {0};
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &attrs, "speed", 2.5F));

    editor_attr_propagate_set(&state, 7, &attrs.entries.data[0], ATTR_SECTION_RUNTIME);

    TEST_ASSERT_EQUAL_INT(1, network_editor_commit_set_attr_fake.call_count);
    TEST_ASSERT_EQUAL_INT(7, network_editor_commit_set_attr_fake.arg1_val);
    TEST_ASSERT_EQUAL_INT(1, network_attr_record_from_attribute_fake.call_count);
    TEST_ASSERT_EQUAL_INT(0, network_structural_mark_dirty_fake.call_count);

    test_attr_set_free_local(&attrs);
}

/* An entity-section REMOVE forwards over the REMOVE_ATTR seam, carrying the
 * entity id, and never arms the resync. */
void test_editor_attr_propagate_remove_entity_calls_seam(void)
{
    RESET_FAKE(network_editor_commit_remove_attr);
    RESET_FAKE(network_structural_mark_dirty);
    GameState state = {0};
    state.network.mode = NET_HOSTING;

    editor_attr_propagate_remove(&state, 7, "collision_w", ATTR_SECTION_RUNTIME);

    TEST_ASSERT_EQUAL_INT(1, network_editor_commit_remove_attr_fake.call_count);
    TEST_ASSERT_EQUAL_INT(7, network_editor_commit_remove_attr_fake.arg1_val);
    TEST_ASSERT_EQUAL_INT(0, network_structural_mark_dirty_fake.call_count);
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
    entity.blueprint_name = str_new(alloc);
    (void)str_from_cstr(&entity.blueprint_name, "wagon");
    state.gamedata.current_level.entities = vec_entity_new(alloc);
    (void)vec_entity_push(&state.gamedata.current_level.entities, entity);

    Blueprint wagon = make_named_blueprint("wagon");
    state.gamedata.blueprints.entries = vec_blueprint_new(test_heap_alloc);
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, wagon);

    blueprint_find_fake.return_val = &state.gamedata.blueprints.entries.data[0];
    level_find_entity_by_id_fake.return_val = 0;

    EditorState editor_state = {0};
    editor_state.selected_entity_id = 0;
    editor_state.selected_tree_index = 0;
    editor_state.selected_attr_kind = EDITOR_ATTR_SEL_NONE;

    handle_browse_navigate(&state, &editor_state, 1);

    TEST_ASSERT_EQUAL_INT(-1, editor_state.selected_tree_index);
    TEST_ASSERT_EQUAL_INT(EDITOR_ATTR_SEL_ADD, editor_state.selected_attr_kind);
    TEST_ASSERT_EQUAL_INT(ATTR_SECTION_PERSISTED, editor_state.selected_attr_section);

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
    entity.blueprint_name = str_new(alloc);
    (void)str_from_cstr(&entity.blueprint_name, "wagon");
    state.gamedata.current_level.entities = vec_entity_new(alloc);
    (void)vec_entity_push(&state.gamedata.current_level.entities, entity);

    Blueprint wagon = make_named_blueprint("wagon");
    state.gamedata.blueprints.entries = vec_blueprint_new(test_heap_alloc);
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, wagon);

    blueprint_find_fake.return_val = &state.gamedata.blueprints.entries.data[0];
    level_find_entity_by_id_fake.return_val = 0;

    EditorState editor_state = {0};
    editor_state.selected_entity_id = 0;
    editor_state.selected_tree_index = -1;
    editor_state.selected_attr_kind = EDITOR_ATTR_SEL_ADD;
    editor_state.selected_attr_section = ATTR_SECTION_PERSISTED;

    handle_browse_navigate(&state, &editor_state, -1);

    TEST_ASSERT_EQUAL_INT(0, editor_state.selected_tree_index);
    TEST_ASSERT_EQUAL_INT(EDITOR_ATTR_SEL_NONE, editor_state.selected_attr_kind);

    test_blueprint_table_free_local(&state.gamedata.blueprints);
    arena_free(&state.gamedata_arena);
}

/* ---- find_place_blueprint_index ----------------------------------------- */

void test_find_place_blueprint_index_found(void)
{
    GameState state = {0};
    state.gamedata.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1};
    entity.blueprint_name = str_new(test_heap_alloc);
    (void)str_from_cstr(&entity.blueprint_name, "chest");
    (void)vec_entity_push(&state.gamedata.current_level.entities, entity);

    state.gamedata.blueprints.entries.alloc = test_heap_alloc;
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, make_named_blueprint("tree"));
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, make_named_blueprint("chest"));

    EditorState editor_state = {.selected_entity_id = 0};
    level_find_entity_by_id_fake.return_val = 0;
    TEST_ASSERT_EQUAL_INT(1, find_place_blueprint_index(&state, &editor_state));

    str_free(&entity.blueprint_name);
    vec_entity_free(&state.gamedata.current_level.entities);
    test_blueprint_table_free_local(&state.gamedata.blueprints);
}

void test_find_place_blueprint_index_not_found(void)
{
    GameState state = {0};
    state.gamedata.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1};
    entity.blueprint_name = str_new(test_heap_alloc);
    (void)str_from_cstr(&entity.blueprint_name, "unknown");
    (void)vec_entity_push(&state.gamedata.current_level.entities, entity);

    state.gamedata.blueprints.entries.alloc = test_heap_alloc;
    (void)vec_blueprint_push(&state.gamedata.blueprints.entries, make_named_blueprint("tree"));

    EditorState editor_state = {.selected_entity_id = 0};
    level_find_entity_by_id_fake.return_val = 0;
    TEST_ASSERT_EQUAL_INT(0, find_place_blueprint_index(&state, &editor_state));

    str_free(&entity.blueprint_name);
    vec_entity_free(&state.gamedata.current_level.entities);
    test_blueprint_table_free_local(&state.gamedata.blueprints);
}

void test_find_place_blueprint_index_no_selection(void)
{
    GameState state = {0};
    EditorState editor_state = {.selected_entity_id = -1};
    level_find_entity_by_id_fake.return_val = -1;
    TEST_ASSERT_EQUAL_INT(0, find_place_blueprint_index(&state, &editor_state));
}

/* ---- editor_clear_stale_restore_cursor ---------------------------------- */

/* Undo/redo clears only the index-based tree cursor; the stable-identity
 * entity/attr selection and the watch list survive, since they resolve by
 * id/name against the restored gamedata. */
void test_clear_stale_tree_cursor_preserves_identity_selection(void)
{
    EditorState editor_state = {
        .selected_entity_id = 5,
        .selected_attr_kind = EDITOR_ATTR_SEL_NAMED,
        .selected_attr_section = ATTR_SECTION_RUNTIME,
        .selected_tree_index = 2,
    };
    WatchList watches = {.count = 2, .watch_ids = {10, 20}};

    editor_clear_stale_restore_cursor(&editor_state);

    TEST_ASSERT_EQUAL_INT(-1, editor_state.selected_tree_index);
    /* Identity selection and watches are untouched. */
    TEST_ASSERT_EQUAL_INT(5, editor_state.selected_entity_id);
    TEST_ASSERT_EQUAL_INT(EDITOR_ATTR_SEL_NAMED, editor_state.selected_attr_kind);
    TEST_ASSERT_EQUAL_INT(ATTR_SECTION_RUNTIME, editor_state.selected_attr_section);
    TEST_ASSERT_EQUAL_INT(2, watches.count);
    TEST_ASSERT_EQUAL_INT(10, watches.watch_ids[0]);
    TEST_ASSERT_EQUAL_INT(20, watches.watch_ids[1]);
}

/* ---- toggle_watch ------------------------------------------------------- */

void test_toggle_watch_adds_and_removes(void)
{
    EditorState editor_state = {.selected_entity_id = 7};
    WatchList watches = {0};

    toggle_watch(&editor_state, &watches);
    TEST_ASSERT_EQUAL_INT(1, watches.count);
    TEST_ASSERT_EQUAL_INT(7, watches.watch_ids[0]);

    toggle_watch(&editor_state, &watches);
    TEST_ASSERT_EQUAL_INT(0, watches.count);
}

/* ---- editor_snap_to_grid (S5.7, D38) ------------------------------------ */

void test_grid_snap_round(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0F, editor_snap_to_grid(0.0F));
    TEST_ASSERT_EQUAL_FLOAT(0.0F, editor_snap_to_grid(7.9F));   /* below half-tile: rounds down to 0 */
    TEST_ASSERT_EQUAL_FLOAT(16.0F, editor_snap_to_grid(8.0F));  /* exact half-tile: rounds up (away from zero) */
    TEST_ASSERT_EQUAL_FLOAT(16.0F, editor_snap_to_grid(15.0F)); /* above half-tile: rounds up to next tile */
    TEST_ASSERT_EQUAL_FLOAT(96.0F, editor_snap_to_grid(100.0F));
    TEST_ASSERT_EQUAL_FLOAT(112.0F, editor_snap_to_grid(104.0F)); /* exact half-tile past 96: rounds up */
    TEST_ASSERT_EQUAL_FLOAT(-16.0F, editor_snap_to_grid(-20.0F)); /* negative positions snap too */
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
    RUN_TEST(test_editor_total_attr_count_top_level_no_blueprint);
    RUN_TEST(test_editor_total_attr_count_top_level_with_blueprint);
    RUN_TEST(test_editor_total_attr_count_child_includes_persisted);
    RUN_TEST(test_editor_total_attr_count_child_with_persisted_attr);
    RUN_TEST(test_editor_is_blueprint_attr_false_for_runtime);
    RUN_TEST(test_editor_is_blueprint_attr_true_for_blueprint_section);
    RUN_TEST(test_editor_attr_at_display_index_runtime);
    RUN_TEST(test_editor_attr_at_display_index_blueprint);
    RUN_TEST(test_editor_attr_at_display_index_out_of_range);
    RUN_TEST(test_editor_attr_row_at_walks_sections);
    RUN_TEST(test_editor_attr_row_at_child_includes_persisted_add);
    RUN_TEST(test_tree_section_total_root_no_children);
    RUN_TEST(test_tree_section_total_root_with_children);
    RUN_TEST(test_tree_section_total_child_entity);
    RUN_TEST(test_tree_is_parent_row_true);
    RUN_TEST(test_tree_is_parent_row_false);
    RUN_TEST(test_tree_child_index_mapping);
    RUN_TEST(test_tree_is_add_child_sentinel);
    RUN_TEST(test_editor_delete_selected_delegates_and_prunes_watches);
    RUN_TEST(test_editor_delete_refused_when_locked_by_other);
    RUN_TEST(test_editor_delete_commits_over_network_seam);
    RUN_TEST(test_editor_attr_edit_permitted_blocks_blueprint_on_client);
    RUN_TEST(test_editor_attr_edit_permitted_blocks_locked_entity_on_client);
    RUN_TEST(test_editor_attr_edit_permitted_allows_on_host);
    RUN_TEST(test_editor_attr_propagate_set_blueprint_marks_dirty);
    RUN_TEST(test_editor_attr_propagate_set_entity_calls_seam);
    RUN_TEST(test_editor_attr_propagate_remove_entity_calls_seam);
    RUN_TEST(test_navigate_tree_to_attr_boundary);
    RUN_TEST(test_navigate_attr_to_tree_boundary);
    RUN_TEST(test_find_place_blueprint_index_found);
    RUN_TEST(test_find_place_blueprint_index_not_found);
    RUN_TEST(test_find_place_blueprint_index_no_selection);
    RUN_TEST(test_clear_stale_tree_cursor_preserves_identity_selection);
    RUN_TEST(test_toggle_watch_adds_and_removes);
    RUN_TEST(test_grid_snap_round);

    return UNITY_END();
}
