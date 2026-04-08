#include "fff.h"
#include "unity.h"

#include "../src/strv.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"          // NOLINT(bugprone-suspicious-include)
#include "../src/error.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/arena_posix.c"  // NOLINT(bugprone-suspicious-include)
#include "../src/attribute.c"    // NOLINT(bugprone-suspicious-include)
#include "../src/entity.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/map.c"          // NOLINT(bugprone-suspicious-include)
#include "../src/editor/child.c" // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

/* Cross-file editor fakes: core.c */
FAKE_VALUE_FUNC(Blueprint *, find_blueprint_by_name, GameState *, const char *);
FAKE_VOID_FUNC(mark_deleted_descendants, const Level *, bool *, int);

/* External module fakes */
FAKE_VOID_FUNC(undo_history_new_entry, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint, Strv);
FAKE_VALUE_FUNC(bool,
                level_spawn_single_child,
                Diag *,
                Level *,
                int,
                const BlueprintChild *,
                const BlueprintTable *,
                TextureLookupFn,
                void *,
                Allocator *);

/* Variadic stubs — cannot use FAKE macros */
void debug_log(DebugState *dbg, const char *format, ...)
{
    (void)dbg;
    (void)format;
}

const char *TextFormat(const char *text, ...)
{
    (void)text;
    return "";
}

/* VEC_IMPL / MAP_IMPL for test setup */
VEC_IMPL(blueprint_child, BlueprintChild)
VEC_IMPL(blueprint, Blueprint)
MAP_IMPL(entity_ruleset, int, vec_rule, map_hash_int, map_eq_int)

#include "test_heap_alloc.h"

void setUp(void)
{
    RESET_FAKE(find_blueprint_by_name);
    RESET_FAKE(mark_deleted_descendants);
    RESET_FAKE(undo_history_new_entry);
    RESET_FAKE(level_spawn_single_child);
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
        str_free(&test_heap_alloc, &blueprint->children.data[index].blueprint_name);
        str_free(&test_heap_alloc, &blueprint->children.data[index].tag);
    }
    vec_blueprint_child_free(&blueprint->children);
    test_attr_set_free_local(&blueprint->attrs);
}

static Blueprint make_named_blueprint(const char *name)
{
    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &blueprint.attrs, (AttrStringPair){"name", name}));
    return blueprint;
}

/* Custom fake for mark_deleted_descendants: cascade delete to children of deleted parents */
static void mark_descendants_custom(const Level *level, bool *is_deleted, int count)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (int index = 0; index < count; index++) {
            if (is_deleted[index]) {
                continue;
            }
            int parent = level->entities.data[index].parent_index;
            if (parent >= 0 && is_deleted[parent]) {
                is_deleted[index] = true;
                changed = true;
            }
        }
    }
}

static Entity make_entity(Allocator *alloc, int entity_id, const char *bp_name, int parent_index, const char *tag)
{
    Entity entity = {0};
    entity.id = entity_id;
    entity.parent_index = parent_index;
    (void)str_from_cstr(alloc, &entity.blueprint_name, bp_name);
    if (tag && tag[0] != '\0') {
        (void)str_from_cstr(alloc, &entity.tag, tag);
    }
    return entity;
}

/* ---- find_child_entity -------------------------------------------------- */

void test_find_child_entity_exact_tag(void)
{
    Level level = {0};
    level.entities.alloc = test_heap_alloc;
    (void)vec_entity_push(&level.entities, make_entity(&test_heap_alloc, 1, "parent_bp", -1, ""));
    (void)vec_entity_push(&level.entities, make_entity(&test_heap_alloc, 2, "child_bp", 0, "arm"));

    int result = find_child_entity(&level, 0, "child_bp", "arm");
    TEST_ASSERT_EQUAL_INT(1, result);

    for (int index = 0; index < level.entities.count; index++) {
        str_free(&test_heap_alloc, &level.entities.data[index].blueprint_name);
        str_free(&test_heap_alloc, &level.entities.data[index].tag);
    }
    vec_entity_free(&level.entities);
}

void test_find_child_entity_empty_tag(void)
{
    Level level = {0};
    level.entities.alloc = test_heap_alloc;
    (void)vec_entity_push(&level.entities, make_entity(&test_heap_alloc, 1, "parent_bp", -1, ""));
    (void)vec_entity_push(&level.entities, make_entity(&test_heap_alloc, 2, "child_bp", 0, ""));

    int result = find_child_entity(&level, 0, "child_bp", "");
    TEST_ASSERT_EQUAL_INT(1, result);

    for (int index = 0; index < level.entities.count; index++) {
        str_free(&test_heap_alloc, &level.entities.data[index].blueprint_name);
        str_free(&test_heap_alloc, &level.entities.data[index].tag);
    }
    vec_entity_free(&level.entities);
}

void test_find_child_entity_wrong_parent(void)
{
    Level level = {0};
    level.entities.alloc = test_heap_alloc;
    (void)vec_entity_push(&level.entities, make_entity(&test_heap_alloc, 1, "parent_bp", -1, ""));
    (void)vec_entity_push(&level.entities, make_entity(&test_heap_alloc, 2, "child_bp", 0, "arm"));

    int result = find_child_entity(&level, 99, "child_bp", "arm");
    TEST_ASSERT_EQUAL_INT(-1, result);

    for (int index = 0; index < level.entities.count; index++) {
        str_free(&test_heap_alloc, &level.entities.data[index].blueprint_name);
        str_free(&test_heap_alloc, &level.entities.data[index].tag);
    }
    vec_entity_free(&level.entities);
}

void test_find_child_entity_wrong_blueprint(void)
{
    Level level = {0};
    level.entities.alloc = test_heap_alloc;
    (void)vec_entity_push(&level.entities, make_entity(&test_heap_alloc, 1, "parent_bp", -1, ""));
    (void)vec_entity_push(&level.entities, make_entity(&test_heap_alloc, 2, "child_bp", 0, "arm"));

    int result = find_child_entity(&level, 0, "other_bp", "arm");
    TEST_ASSERT_EQUAL_INT(-1, result);

    for (int index = 0; index < level.entities.count; index++) {
        str_free(&test_heap_alloc, &level.entities.data[index].blueprint_name);
        str_free(&test_heap_alloc, &level.entities.data[index].tag);
    }
    vec_entity_free(&level.entities);
}

void test_find_child_entity_wrong_tag(void)
{
    Level level = {0};
    level.entities.alloc = test_heap_alloc;
    (void)vec_entity_push(&level.entities, make_entity(&test_heap_alloc, 1, "parent_bp", -1, ""));
    (void)vec_entity_push(&level.entities, make_entity(&test_heap_alloc, 2, "child_bp", 0, "arm"));

    int result = find_child_entity(&level, 0, "child_bp", "leg");
    TEST_ASSERT_EQUAL_INT(-1, result);

    for (int index = 0; index < level.entities.count; index++) {
        str_free(&test_heap_alloc, &level.entities.data[index].blueprint_name);
        str_free(&test_heap_alloc, &level.entities.data[index].tag);
    }
    vec_entity_free(&level.entities);
}

void test_find_child_entity_empty_level(void)
{
    Level level = {0};
    int result = find_child_entity(&level, 0, "child_bp", "arm");
    TEST_ASSERT_EQUAL_INT(-1, result);
}

/* ---- propagate_child_tag ------------------------------------------------ */

void test_propagate_child_tag_updates_matching(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 1, "npc", -1, ""));
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 2, "weapon", 0, "sword"));

    Blueprint blueprint = make_named_blueprint("npc");
    blueprint.children.alloc = test_heap_alloc;
    BlueprintChild child = {0};
    (void)str_from_cstr(&test_heap_alloc, &child.blueprint_name, "weapon");
    (void)str_from_cstr(&test_heap_alloc, &child.tag, "axe");
    (void)vec_blueprint_child_push(&blueprint.children, child);

    propagate_child_tag(&state, &blueprint, 0, "sword");

    TEST_ASSERT_EQUAL_STRING("axe", state.gamedata.current_level.entities.data[1].tag.ptr);

    test_blueprint_free_local(&blueprint);
    arena_free(&state.gamedata_arena);
}

void test_propagate_child_tag_multiple_parents(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 1, "npc", -1, ""));
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 2, "weapon", 0, "old"));
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 3, "npc", -1, ""));
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 4, "weapon", 2, "old"));

    Blueprint blueprint = make_named_blueprint("npc");
    blueprint.children.alloc = test_heap_alloc;
    BlueprintChild child = {0};
    (void)str_from_cstr(&test_heap_alloc, &child.blueprint_name, "weapon");
    (void)str_from_cstr(&test_heap_alloc, &child.tag, "new");
    (void)vec_blueprint_child_push(&blueprint.children, child);

    propagate_child_tag(&state, &blueprint, 0, "old");

    TEST_ASSERT_EQUAL_STRING("new", state.gamedata.current_level.entities.data[1].tag.ptr);
    TEST_ASSERT_EQUAL_STRING("new", state.gamedata.current_level.entities.data[3].tag.ptr);

    test_blueprint_free_local(&blueprint);
    arena_free(&state.gamedata_arena);
}

void test_propagate_child_tag_no_match(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 1, "npc", -1, ""));
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 2, "weapon", 0, "sword"));

    Blueprint blueprint = make_named_blueprint("npc");
    blueprint.children.alloc = test_heap_alloc;
    BlueprintChild child = {0};
    (void)str_from_cstr(&test_heap_alloc, &child.blueprint_name, "weapon");
    (void)str_from_cstr(&test_heap_alloc, &child.tag, "axe");
    (void)vec_blueprint_child_push(&blueprint.children, child);

    propagate_child_tag(&state, &blueprint, 0, "nonexistent");

    TEST_ASSERT_EQUAL_STRING("sword", state.gamedata.current_level.entities.data[1].tag.ptr);

    test_blueprint_free_local(&blueprint);
    arena_free(&state.gamedata_arena);
}

/* ---- propagate_child_offset --------------------------------------------- */

void test_propagate_child_offset_repositions(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    Entity parent = make_entity(&alloc, 1, "npc", -1, "");
    parent.position = (Vector2){100.0F, 200.0F};
    (void)vec_entity_push(&state.gamedata.current_level.entities, parent);
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 2, "weapon", 0, ""));

    Blueprint blueprint = make_named_blueprint("npc");
    blueprint.children.alloc = test_heap_alloc;
    BlueprintChild child = {0};
    (void)str_from_cstr(&test_heap_alloc, &child.blueprint_name, "weapon");
    child.offset = (Vector2){10.0F, 20.0F};
    (void)vec_blueprint_child_push(&blueprint.children, child);

    propagate_child_offset(&state, &blueprint, 0);

    Entity *child_ent = &state.gamedata.current_level.entities.data[1];
    TEST_ASSERT_EQUAL_FLOAT(110.0F, child_ent->position.x);
    TEST_ASSERT_EQUAL_FLOAT(220.0F, child_ent->position.y);

    test_blueprint_free_local(&blueprint);
    arena_free(&state.gamedata_arena);
}

void test_propagate_child_offset_updates_collision(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    Entity parent = make_entity(&alloc, 1, "npc", -1, "");
    parent.position = (Vector2){50.0F, 60.0F};
    (void)vec_entity_push(&state.gamedata.current_level.entities, parent);
    Entity child_entity = make_entity(&alloc, 2, "weapon", 0, "");
    child_entity.collision_offset = (Vector2){5.0F, 5.0F};
    (void)vec_entity_push(&state.gamedata.current_level.entities, child_entity);

    Blueprint blueprint = make_named_blueprint("npc");
    blueprint.children.alloc = test_heap_alloc;
    BlueprintChild child = {0};
    (void)str_from_cstr(&test_heap_alloc, &child.blueprint_name, "weapon");
    child.offset = (Vector2){10.0F, 10.0F};
    (void)vec_blueprint_child_push(&blueprint.children, child);

    propagate_child_offset(&state, &blueprint, 0);

    Entity *updated = &state.gamedata.current_level.entities.data[1];
    TEST_ASSERT_EQUAL_FLOAT(65.0F, updated->collision.x);
    TEST_ASSERT_EQUAL_FLOAT(75.0F, updated->collision.y);

    test_blueprint_free_local(&blueprint);
    arena_free(&state.gamedata_arena);
}

void test_propagate_child_offset_multiple_parents(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    Entity parent_a = make_entity(&alloc, 1, "npc", -1, "");
    parent_a.position = (Vector2){100.0F, 100.0F};
    (void)vec_entity_push(&state.gamedata.current_level.entities, parent_a);
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 2, "weapon", 0, ""));
    Entity parent_b = make_entity(&alloc, 3, "npc", -1, "");
    parent_b.position = (Vector2){200.0F, 300.0F};
    (void)vec_entity_push(&state.gamedata.current_level.entities, parent_b);
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 4, "weapon", 2, ""));

    Blueprint blueprint = make_named_blueprint("npc");
    blueprint.children.alloc = test_heap_alloc;
    BlueprintChild child = {0};
    (void)str_from_cstr(&test_heap_alloc, &child.blueprint_name, "weapon");
    child.offset = (Vector2){5.0F, 5.0F};
    (void)vec_blueprint_child_push(&blueprint.children, child);

    propagate_child_offset(&state, &blueprint, 0);

    TEST_ASSERT_EQUAL_FLOAT(105.0F, state.gamedata.current_level.entities.data[1].position.x);
    TEST_ASSERT_EQUAL_FLOAT(105.0F, state.gamedata.current_level.entities.data[1].position.y);
    TEST_ASSERT_EQUAL_FLOAT(205.0F, state.gamedata.current_level.entities.data[3].position.x);
    TEST_ASSERT_EQUAL_FLOAT(305.0F, state.gamedata.current_level.entities.data[3].position.y);

    test_blueprint_free_local(&blueprint);
    arena_free(&state.gamedata_arena);
}

/* ---- remove_blueprint_child --------------------------------------------- */

void test_remove_blueprint_child_removes_from_vec(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    TEST_ASSERT_TRUE(arena_init(&err, &state.scratch_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 1, "npc", -1, ""));

    Blueprint blueprint = make_named_blueprint("npc");
    blueprint.children = vec_blueprint_child_new(alloc);
    BlueprintChild child_a = {0};
    (void)str_from_cstr(&alloc, &child_a.blueprint_name, "weapon");
    (void)vec_blueprint_child_push(&blueprint.children, child_a);
    BlueprintChild child_b = {0};
    (void)str_from_cstr(&alloc, &child_b.blueprint_name, "shield");
    (void)vec_blueprint_child_push(&blueprint.children, child_b);

    find_blueprint_by_name_fake.return_val = &blueprint;
    EditorState editor_state = {.selected_entity_index = 0};
    UndoHistory undo_history = {0};
    state.gamedata.player_index = -1;

    remove_blueprint_child(&state, &editor_state, &undo_history, 0);

    TEST_ASSERT_EQUAL_INT(1, blueprint.children.count);
    TEST_ASSERT_EQUAL_STRING("shield", blueprint.children.data[0].blueprint_name.ptr);

    test_attr_set_free_local(&blueprint.attrs);
    arena_free(&state.gamedata_arena);
    arena_free(&state.scratch_arena);
}

void test_remove_blueprint_child_deletes_entities(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    TEST_ASSERT_TRUE(arena_init(&err, &state.scratch_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 1, "npc", -1, ""));
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 2, "weapon", 0, ""));

    Blueprint blueprint = make_named_blueprint("npc");
    blueprint.children = vec_blueprint_child_new(alloc);
    BlueprintChild child = {0};
    (void)str_from_cstr(&alloc, &child.blueprint_name, "weapon");
    (void)vec_blueprint_child_push(&blueprint.children, child);

    find_blueprint_by_name_fake.return_val = &blueprint;
    EditorState editor_state = {.selected_entity_index = 0};
    UndoHistory undo_history = {0};
    state.gamedata.player_index = -1;

    remove_blueprint_child(&state, &editor_state, &undo_history, 0);

    TEST_ASSERT_EQUAL_INT(1, state.gamedata.current_level.entities.count);
    TEST_ASSERT_EQUAL_INT(1, state.gamedata.current_level.entities.data[0].id);

    test_attr_set_free_local(&blueprint.attrs);
    arena_free(&state.gamedata_arena);
    arena_free(&state.scratch_arena);
}

void test_remove_blueprint_child_cascades_descendants(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    TEST_ASSERT_TRUE(arena_init(&err, &state.scratch_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 1, "npc", -1, ""));
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 2, "weapon", 0, ""));
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 3, "gem", 1, ""));

    Blueprint blueprint = make_named_blueprint("npc");
    blueprint.children = vec_blueprint_child_new(alloc);
    BlueprintChild child = {0};
    (void)str_from_cstr(&alloc, &child.blueprint_name, "weapon");
    (void)vec_blueprint_child_push(&blueprint.children, child);

    find_blueprint_by_name_fake.return_val = &blueprint;

    mark_deleted_descendants_fake.custom_fake = mark_descendants_custom;

    EditorState editor_state = {.selected_entity_index = 0};
    UndoHistory undo_history = {0};
    state.gamedata.player_index = -1;

    remove_blueprint_child(&state, &editor_state, &undo_history, 0);

    TEST_ASSERT_EQUAL_INT(1, state.gamedata.current_level.entities.count);
    TEST_ASSERT_EQUAL_INT(1, state.gamedata.current_level.entities.data[0].id);

    test_attr_set_free_local(&blueprint.attrs);
    arena_free(&state.gamedata_arena);
    arena_free(&state.scratch_arena);
}

void test_remove_blueprint_child_invalid_index(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 1, "npc", -1, ""));

    Blueprint blueprint = make_named_blueprint("npc");
    blueprint.children.alloc = test_heap_alloc;

    find_blueprint_by_name_fake.return_val = &blueprint;
    EditorState editor_state = {.selected_entity_index = 0};
    UndoHistory undo_history = {0};

    remove_blueprint_child(&state, &editor_state, &undo_history, -1);
    TEST_ASSERT_EQUAL_INT(0, undo_history_new_entry_fake.call_count);

    remove_blueprint_child(&state, &editor_state, &undo_history, 5);
    TEST_ASSERT_EQUAL_INT(0, undo_history_new_entry_fake.call_count);

    test_blueprint_free_local(&blueprint);
    arena_free(&state.gamedata_arena);
}

void test_remove_blueprint_child_records_undo(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    TEST_ASSERT_TRUE(arena_init(&err, &state.scratch_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 1, "npc", -1, ""));

    Blueprint blueprint = make_named_blueprint("npc");
    blueprint.children = vec_blueprint_child_new(alloc);
    BlueprintChild child = {0};
    (void)str_from_cstr(&alloc, &child.blueprint_name, "weapon");
    (void)vec_blueprint_child_push(&blueprint.children, child);

    find_blueprint_by_name_fake.return_val = &blueprint;
    EditorState editor_state = {.selected_entity_index = 0};
    UndoHistory undo_history = {0};
    state.gamedata.player_index = -1;

    remove_blueprint_child(&state, &editor_state, &undo_history, 0);

    TEST_ASSERT_EQUAL_INT(1, undo_history_new_entry_fake.call_count);

    test_attr_set_free_local(&blueprint.attrs);
    arena_free(&state.gamedata_arena);
    arena_free(&state.scratch_arena);
}

/* ---- add_blueprint_child ------------------------------------------------ */

void test_add_blueprint_child_adds_to_vec(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 1, "npc", -1, ""));

    Blueprint blueprint = make_named_blueprint("npc");
    blueprint.children = vec_blueprint_child_new(alloc);

    find_blueprint_by_name_fake.return_val = &blueprint;
    level_spawn_single_child_fake.return_val = true;
    EditorState editor_state = {.selected_entity_index = 0};
    UndoHistory undo_history = {0};
    Diag diag = {0};

    add_blueprint_child(&diag, &state, &editor_state, &undo_history, "weapon", nullptr, nullptr);

    TEST_ASSERT_EQUAL_INT(1, blueprint.children.count);
    TEST_ASSERT_EQUAL_STRING("weapon", blueprint.children.data[0].blueprint_name.ptr);

    test_attr_set_free_local(&blueprint.attrs);
    arena_free(&state.gamedata_arena);
}

void test_add_blueprint_child_spawns_for_instances(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 1, "npc", -1, ""));
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 2, "npc", -1, ""));
    (void)vec_entity_push(&state.gamedata.current_level.entities, make_entity(&alloc, 3, "tree", -1, ""));

    Blueprint blueprint = make_named_blueprint("npc");
    blueprint.children = vec_blueprint_child_new(alloc);

    find_blueprint_by_name_fake.return_val = &blueprint;
    level_spawn_single_child_fake.return_val = true;
    EditorState editor_state = {.selected_entity_index = 0};
    UndoHistory undo_history = {0};
    Diag diag = {0};

    add_blueprint_child(&diag, &state, &editor_state, &undo_history, "weapon", nullptr, nullptr);

    TEST_ASSERT_EQUAL_INT(2, level_spawn_single_child_fake.call_count);
    TEST_ASSERT_EQUAL_INT(1, undo_history_new_entry_fake.call_count);

    test_attr_set_free_local(&blueprint.attrs);
    arena_free(&state.gamedata_arena);
}

void test_add_blueprint_child_no_selection(void)
{
    GameState state = {0};
    EditorState editor_state = {.selected_entity_index = -1};
    UndoHistory undo_history = {0};
    Diag diag = {0};

    add_blueprint_child(&diag, &state, &editor_state, &undo_history, "weapon", nullptr, nullptr);

    TEST_ASSERT_EQUAL_INT(0, find_blueprint_by_name_fake.call_count);
    TEST_ASSERT_EQUAL_INT(0, undo_history_new_entry_fake.call_count);
}

/* ---- main --------------------------------------------------------------- */

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();

    RUN_TEST(test_find_child_entity_exact_tag);
    RUN_TEST(test_find_child_entity_empty_tag);
    RUN_TEST(test_find_child_entity_wrong_parent);
    RUN_TEST(test_find_child_entity_wrong_blueprint);
    RUN_TEST(test_find_child_entity_wrong_tag);
    RUN_TEST(test_find_child_entity_empty_level);

    RUN_TEST(test_propagate_child_tag_updates_matching);
    RUN_TEST(test_propagate_child_tag_multiple_parents);
    RUN_TEST(test_propagate_child_tag_no_match);

    RUN_TEST(test_propagate_child_offset_repositions);
    RUN_TEST(test_propagate_child_offset_updates_collision);
    RUN_TEST(test_propagate_child_offset_multiple_parents);

    RUN_TEST(test_remove_blueprint_child_removes_from_vec);
    RUN_TEST(test_remove_blueprint_child_deletes_entities);
    RUN_TEST(test_remove_blueprint_child_cascades_descendants);
    RUN_TEST(test_remove_blueprint_child_invalid_index);
    RUN_TEST(test_remove_blueprint_child_records_undo);

    RUN_TEST(test_add_blueprint_child_adds_to_vec);
    RUN_TEST(test_add_blueprint_child_spawns_for_instances);
    RUN_TEST(test_add_blueprint_child_no_selection);

    return UNITY_END();
}
