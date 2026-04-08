#include "fff.h"
#include "unity.h"

#include "../src/strv.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/error.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/arena_posix.c" // NOLINT(bugprone-suspicious-include)
#include "../src/attribute.c"   // NOLINT(bugprone-suspicious-include)
#include "../src/entity.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/map.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/editor.c"      // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

/* raylib input fakes */
FAKE_VALUE_FUNC(bool, IsKeyPressed, int);
FAKE_VALUE_FUNC(bool, IsKeyDown, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonPressed, int, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonDown, int, int);

/* raylib draw fakes */
FAKE_VOID_FUNC(DrawLine, int, int, int, int, Color);
FAKE_VOID_FUNC(DrawRectangle, int, int, int, int, Color);
FAKE_VOID_FUNC(DrawTextEx, Font, const char *, Vector2, float, float, Color);
FAKE_VOID_FUNC(DrawRectangleLinesEx, Rectangle, float, Color);
FAKE_VOID_FUNC(DrawTextureRec, Texture2D, Rectangle, Vector2, Color);
FAKE_VOID_FUNC(DrawCircle, int, int, float, Color);
FAKE_VOID_FUNC(DrawRing, Vector2, float, float, float, float, int, Color);
FAKE_VALUE_FUNC(Vector2, MeasureTextEx, Font, const char *, float, float);
FAKE_VALUE_FUNC(bool, IsFontValid, Font);

/* TextFormat stub — variadic, cannot use FAKE_VALUE_FUNC */
const char *TextFormat(const char *text, ...)
{
    (void)text;
    return "";
}

/* blueprint function fakes — editor.c calls these but we don't include blueprint.c */
FAKE_VALUE_FUNC(Vector2, blueprint_get_collision_offset, const Blueprint *);
FAKE_VALUE_FUNC(Vector2, blueprint_get_collision_size, const Blueprint *);

/* game function fakes — entity_resolve_defaults is defined in game.c */
FAKE_VALUE_FUNC(const AttrSet *, entity_resolve_defaults, const GameState *, int);

/* blueprint function fakes — blueprint_find is defined in blueprint.c */
FAKE_VALUE_FUNC(const Blueprint *, blueprint_find, const BlueprintTable *, const char *);

/* level function fakes — level_spawn_single_child is defined in level.c */
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

/* undo function fakes — editor.c calls these but we don't include undo.c */
FAKE_VOID_FUNC(undo_history_new_entry, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint, Strv);
FAKE_VOID_FUNC(undo_history_step_back, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint);
FAKE_VOID_FUNC(undo_history_step_forward, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint);
FAKE_VOID_FUNC(undo_history_discard, UndoHistory *);
FAKE_VALUE_FUNC(Strv, undo_history_description, const UndoHistory *);

/* debug_log stub — cannot use FAKE_VOID_FUNC_VARARG due to __attribute__((format)) conflict */
void debug_log(DebugState *dbg, const char *format, ...)
{
    (void)dbg;
    (void)format;
}

/* VEC_IMPL for blueprint types — test code needs push/free for setup/cleanup,
 * but we don't include blueprint.c (heavy toml deps). editor.c only reads
 * these vecs, never pushes. */
VEC_IMPL(blueprint_child, BlueprintChild)
VEC_IMPL(blueprint, Blueprint)
VEC_IMPL(flag_name, FlagName)
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

/* ---- radial_sector_from_stick ------------------------------------------- */

void test_editor_radial_dead_zone_returns_negative_one(void)
{
    Vector2 tiny_stick = {0.1F, 0.1F};
    TEST_ASSERT_EQUAL_INT(-1, radial_sector_from_stick(tiny_stick, 4));
}

void test_editor_radial_stick_up_four_items(void)
{
    Vector2 stick_up = {0.0F, -1.0F};
    TEST_ASSERT_EQUAL_INT(0, radial_sector_from_stick(stick_up, 4));
}

void test_editor_radial_stick_right_four_items(void)
{
    Vector2 right = {1.0F, 0.0F};
    TEST_ASSERT_EQUAL_INT(1, radial_sector_from_stick(right, 4));
}

void test_editor_radial_stick_down_four_items(void)
{
    /* Slightly west of south to avoid exact sector boundary at 180 degrees */
    Vector2 down = {-0.1F, 1.0F};
    TEST_ASSERT_EQUAL_INT(2, radial_sector_from_stick(down, 4));
}

void test_editor_radial_stick_left_four_items(void)
{
    Vector2 left = {-1.0F, 0.0F};
    TEST_ASSERT_EQUAL_INT(3, radial_sector_from_stick(left, 4));
}

/* ---- radial_label ------------------------------------------------------- */

void test_editor_radial_label_grab(void)
{
    EditorState editor_state = {.radial_context = RADIAL_CTX_TOOLS};
    TEST_ASSERT_EQUAL_STRING("Grab", radial_label(&editor_state, 0));
}

void test_editor_radial_label_delete(void)
{
    EditorState editor_state = {.radial_context = RADIAL_CTX_TOOLS};
    TEST_ASSERT_EQUAL_STRING("Delete", radial_label(&editor_state, 3));
}

void test_editor_radial_label_out_of_bounds(void)
{
    EditorState editor_state = {.radial_context = RADIAL_CTX_TOOLS};
    TEST_ASSERT_EQUAL_STRING("", radial_label(&editor_state, 4));
}

/* ---- word_builder_append ------------------------------------------------ */

void test_editor_word_builder_append_to_empty(void)
{
    EditorState editor_state = {0};
    word_builder_append(&editor_state, "chest");
    TEST_ASSERT_EQUAL_STRING("chest", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(5, editor_state.word_builder_len);
}

void test_editor_word_builder_append_with_underscore(void)
{
    EditorState editor_state = {0};
    word_builder_append(&editor_state, "chest");
    word_builder_append(&editor_state, "locked");
    TEST_ASSERT_EQUAL_STRING("chest_locked", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(12, editor_state.word_builder_len);
}

void test_editor_word_builder_append_overflow_noop(void)
{
    EditorState editor_state = {0};
    memset(editor_state.word_builder_buf, 'a', WORD_BUILDER_BUF_SIZE - 2);
    editor_state.word_builder_buf[WORD_BUILDER_BUF_SIZE - 2] = '\0';
    editor_state.word_builder_len = WORD_BUILDER_BUF_SIZE - 2;

    word_builder_append(&editor_state, "x");

    TEST_ASSERT_EQUAL_INT(WORD_BUILDER_BUF_SIZE - 2, editor_state.word_builder_len);
}

void test_editor_word_builder_append_multiple(void)
{
    EditorState editor_state = {0};
    word_builder_append(&editor_state, "fire");
    word_builder_append(&editor_state, "ice");
    word_builder_append(&editor_state, "water");
    TEST_ASSERT_EQUAL_STRING("fire_ice_water", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(14, editor_state.word_builder_len);
}

/* ---- word_builder_pop --------------------------------------------------- */

void test_editor_word_builder_pop_last_word(void)
{
    EditorState editor_state = {0};
    word_builder_append(&editor_state, "chest");
    word_builder_append(&editor_state, "locked");
    word_builder_pop(&editor_state);
    TEST_ASSERT_EQUAL_STRING("chest", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(5, editor_state.word_builder_len);
}

void test_editor_word_builder_pop_single_word(void)
{
    EditorState editor_state = {0};
    word_builder_append(&editor_state, "chest");
    word_builder_pop(&editor_state);
    TEST_ASSERT_EQUAL_STRING("", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(0, editor_state.word_builder_len);
}

void test_editor_word_builder_pop_empty_noop(void)
{
    EditorState editor_state = {0};
    word_builder_pop(&editor_state);
    TEST_ASSERT_EQUAL_STRING("", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(0, editor_state.word_builder_len);
}

/* ---- word_builder_total_count ------------------------------------------- */

void test_editor_word_builder_total_count_no_blueprints(void)
{
    GameState state = {0};
    int total = word_builder_total_count(&state);
    TEST_ASSERT_EQUAL_INT(1 + WORD_BUILDER_BUILTIN_COUNT, total);
}

void test_editor_word_builder_total_count_with_blueprints(void)
{
    GameState state = {0};
    state.gamedata.blueprints.entries.alloc = test_heap_alloc;
    int base = word_builder_total_count(&state);

    Blueprint blueprint = make_named_blueprint("player");
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, blueprint));

    TEST_ASSERT_EQUAL_INT(base + 1, word_builder_total_count(&state));

    test_blueprint_table_free_local(&state.gamedata.blueprints);
}

/* ---- word_builder_item -------------------------------------------------- */

void test_editor_word_builder_item_zero_is_done(void)
{
    GameState state = {0};
    TEST_ASSERT_EQUAL_STRING("[ DONE ]", word_builder_item(&state, 0));
}

void test_editor_word_builder_item_first_builtin(void)
{
    GameState state = {0};
    TEST_ASSERT_EQUAL_STRING("chest", word_builder_item(&state, 1));
}

void test_editor_word_builder_item_blueprint_name(void)
{
    GameState state = {0};
    state.gamedata.blueprints.entries.alloc = test_heap_alloc;
    Blueprint blueprint = make_named_blueprint("player");
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, blueprint));

    int blueprint_index = 1 + WORD_BUILDER_BUILTIN_COUNT;
    TEST_ASSERT_EQUAL_STRING("player", word_builder_item(&state, blueprint_index));

    test_blueprint_table_free_local(&state.gamedata.blueprints);
}

void test_editor_word_builder_item_negative_index(void)
{
    GameState state = {0};
    TEST_ASSERT_EQUAL_STRING("[ DONE ]", word_builder_item(&state, -1));
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

/* ---- find_nearest_entity ------------------------------------------------ */

void test_editor_find_nearest_single_entity(void)
{
    Level level = {.entities.alloc = test_heap_alloc};
    Entity entity = {.parent_index = -1, .position = {10.0F, 10.0F}};
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, entity));

    TEST_ASSERT_EQUAL_INT(0, find_nearest_entity(&level, (Vector2){15.0F, 15.0F}));

    vec_entity_free(&level.entities);
}

void test_editor_find_nearest_closer_wins(void)
{
    Level level = {.entities.alloc = test_heap_alloc};
    Entity far_entity = {.parent_index = -1, .position = {100.0F, 100.0F}};
    Entity near_entity = {.parent_index = -1, .position = {10.0F, 10.0F}};
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, far_entity));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, near_entity));

    TEST_ASSERT_EQUAL_INT(1, find_nearest_entity(&level, (Vector2){12.0F, 12.0F}));

    vec_entity_free(&level.entities);
}

void test_editor_find_nearest_skips_children(void)
{
    Level level = {.entities.alloc = test_heap_alloc};
    Entity child = {.parent_index = 0, .position = {5.0F, 5.0F}};
    Entity root = {.parent_index = -1, .position = {100.0F, 100.0F}};
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, child));
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, root));

    TEST_ASSERT_EQUAL_INT(1, find_nearest_entity(&level, (Vector2){0.0F, 0.0F}));

    vec_entity_free(&level.entities);
}

void test_editor_find_nearest_empty_level(void)
{
    Level level = {.entities.alloc = test_heap_alloc};
    TEST_ASSERT_EQUAL_INT(-1, find_nearest_entity(&level, (Vector2){0.0F, 0.0F}));
}

/* ---- entity_outline_rect ------------------------------------------------ */

void test_editor_entity_outline_rect_with_collision(void)
{
    GameState state = {0};
    Entity entity = {0};
    entity.collision = (Rectangle){10.0F, 20.0F, 32.0F, 16.0F};

    Rectangle rect = entity_outline_rect(&state, &entity);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 10.0F, rect.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 20.0F, rect.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 32.0F, rect.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 16.0F, rect.height);
}

void test_editor_entity_outline_rect_without_collision(void)
{
    GameState state = {0};
    Entity entity = {0};
    entity.position = (Vector2){50.0F, 60.0F};
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "src_w", 16.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "src_h", 24.0F));

    Rectangle rect = entity_outline_rect(&state, &entity);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 50.0F, rect.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 60.0F, rect.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 16.0F, rect.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 24.0F, rect.height);

    test_attr_set_free_local(&entity.attrs);
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

/* ---- apply_attr_delta --------------------------------------------------- */

void test_editor_apply_attr_delta_int(void)
{
    GameState state = {0};
    state.gamedata.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "speed", 10));
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));

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

    EditorState editor_state = {.selected_entity_index = 0, .selected_attr_index = 99};
    apply_attr_delta(&state, &editor_state, 1);

    vec_entity_free(&state.gamedata.current_level.entities);
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

/* ==== Mocked raylib input tests ========================================== */

static void reset_input_fakes(void)
{
    RESET_FAKE(IsKeyPressed);
    RESET_FAKE(IsKeyDown);
    RESET_FAKE(IsGamepadButtonPressed);
    RESET_FAKE(IsGamepadButtonDown);
    FFF_RESET_HISTORY(); // NOLINT(bugprone-multi-level-implicit-pointer-conversion)
}

static int target_key_for_press;
static bool press_specific_key(int key)
{
    return key == target_key_for_press;
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

/* ---- toggle_pressed ----------------------------------------------------- */

void test_editor_toggle_pressed_key(void)
{
    reset_input_fakes();
    IsKeyPressed_fake.return_val = true;
    TEST_ASSERT_TRUE(toggle_pressed((ToggleBinding){KEY_SPACE, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}));
    TEST_ASSERT_EQUAL_INT(1, IsKeyPressed_fake.call_count);
    TEST_ASSERT_EQUAL_INT(0, IsGamepadButtonPressed_fake.call_count);
}

void test_editor_toggle_pressed_gamepad(void)
{
    reset_input_fakes();
    IsKeyPressed_fake.return_val = false;
    IsGamepadButtonPressed_fake.return_val = true;
    TEST_ASSERT_TRUE(toggle_pressed((ToggleBinding){KEY_SPACE, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}));
    TEST_ASSERT_EQUAL_INT(1, IsKeyPressed_fake.call_count);
    TEST_ASSERT_EQUAL_INT(1, IsGamepadButtonPressed_fake.call_count);
}

void test_editor_toggle_pressed_neither(void)
{
    reset_input_fakes();
    TEST_ASSERT_FALSE(toggle_pressed((ToggleBinding){KEY_SPACE, GAMEPAD_BUTTON_RIGHT_FACE_DOWN}));
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
    IsKeyPressed_fake.custom_fake = press_specific_key;
    TEST_ASSERT_EQUAL_INT(-EDITOR_ATTR_LARGE_STEP, read_value_delta());
}

void test_editor_read_value_delta_large_plus(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_RIGHT_BRACKET;
    IsKeyPressed_fake.custom_fake = press_specific_key;
    TEST_ASSERT_EQUAL_INT(EDITOR_ATTR_LARGE_STEP, read_value_delta());
}

void test_editor_read_value_delta_huge_minus(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_PAGE_DOWN;
    IsKeyPressed_fake.custom_fake = press_specific_key;
    TEST_ASSERT_EQUAL_INT(-EDITOR_ATTR_HUGE_STEP, read_value_delta());
}

void test_editor_read_value_delta_combined(void)
{
    reset_input_fakes();
    IsKeyPressed_fake.return_val = true;
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

/* ---- word_builder_navigate ---------------------------------------------- */

void test_editor_word_builder_nav_up(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_UP;
    IsKeyPressed_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.word_builder_scroll = 5};
    word_builder_navigate(&editor_state, 10);
    TEST_ASSERT_EQUAL_INT(4, editor_state.word_builder_scroll);
}

void test_editor_word_builder_nav_up_clamped(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_UP;
    IsKeyPressed_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.word_builder_scroll = 0};
    word_builder_navigate(&editor_state, 10);
    TEST_ASSERT_EQUAL_INT(0, editor_state.word_builder_scroll);
}

void test_editor_word_builder_nav_down(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_DOWN;
    IsKeyPressed_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.word_builder_scroll = 0};
    word_builder_navigate(&editor_state, 10);
    TEST_ASSERT_EQUAL_INT(1, editor_state.word_builder_scroll);
}

void test_editor_word_builder_nav_down_clamped(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_DOWN;
    IsKeyPressed_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.word_builder_scroll = 9};
    word_builder_navigate(&editor_state, 10);
    TEST_ASSERT_EQUAL_INT(9, editor_state.word_builder_scroll);
}

void test_editor_word_builder_nav_page_up(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_Q;
    IsKeyPressed_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.word_builder_scroll = 8};
    word_builder_navigate(&editor_state, 20);
    TEST_ASSERT_EQUAL_INT(8 - WORD_BUILDER_PAGE_SIZE, editor_state.word_builder_scroll);
}

void test_editor_word_builder_nav_page_down(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_E;
    IsKeyPressed_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.word_builder_scroll = 0};
    word_builder_navigate(&editor_state, 20);
    TEST_ASSERT_EQUAL_INT(WORD_BUILDER_PAGE_SIZE, editor_state.word_builder_scroll);
}

/* ---- fuzzy_finder_contains ---------------------------------------------- */

void test_fuzzy_finder_contains_found(void)
{
    const char *items[] = {"alpha", "beta", "gamma"};
    TEST_ASSERT_TRUE(fuzzy_finder_contains(items, 3, "beta"));
}

void test_fuzzy_finder_contains_not_found(void)
{
    const char *items[] = {"alpha", "beta", "gamma"};
    TEST_ASSERT_FALSE(fuzzy_finder_contains(items, 3, "delta"));
}

void test_fuzzy_finder_contains_empty(void)
{
    TEST_ASSERT_FALSE(fuzzy_finder_contains(nullptr, 0, "anything"));
}

/* ---- fuzzy_finder_item / fuzzy_finder_total_count ----------------------- */

void test_fuzzy_finder_item_zero_is_new(void)
{
    EditorState editor_state = {0};
    TEST_ASSERT_EQUAL_STRING("[ NEW... ]", fuzzy_finder_item(&editor_state, 0));
}

void test_fuzzy_finder_item_negative_is_new(void)
{
    EditorState editor_state = {0};
    TEST_ASSERT_EQUAL_STRING("[ NEW... ]", fuzzy_finder_item(&editor_state, -1));
}

void test_fuzzy_finder_item_returns_name(void)
{
    const char *items[] = {"alpha", "beta"};
    EditorState editor_state = {.fuzzy_finder_items = items, .fuzzy_finder_item_count = 2};
    TEST_ASSERT_EQUAL_STRING("alpha", fuzzy_finder_item(&editor_state, 1));
    TEST_ASSERT_EQUAL_STRING("beta", fuzzy_finder_item(&editor_state, 2));
}

void test_fuzzy_finder_item_out_of_range(void)
{
    const char *items[] = {"alpha"};
    EditorState editor_state = {.fuzzy_finder_items = items, .fuzzy_finder_item_count = 1};
    TEST_ASSERT_EQUAL_STRING("", fuzzy_finder_item(&editor_state, 5));
}

void test_fuzzy_finder_total_count_value(void)
{
    EditorState editor_state = {.fuzzy_finder_item_count = 7};
    TEST_ASSERT_EQUAL_INT(8, fuzzy_finder_total_count(&editor_state));
}

/* ---- fuzzy_finder_navigate ---------------------------------------------- */

void test_fuzzy_finder_navigate_up_clamped(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_UP;
    IsKeyPressed_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.fuzzy_finder_scroll = 0};
    fuzzy_finder_navigate(&editor_state, 10);
    TEST_ASSERT_EQUAL_INT(0, editor_state.fuzzy_finder_scroll);
}

void test_fuzzy_finder_navigate_down_clamped(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_DOWN;
    IsKeyPressed_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.fuzzy_finder_scroll = 9};
    fuzzy_finder_navigate(&editor_state, 10);
    TEST_ASSERT_EQUAL_INT(9, editor_state.fuzzy_finder_scroll);
}

void test_fuzzy_finder_navigate_page_up(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_Q;
    IsKeyPressed_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.fuzzy_finder_scroll = 8};
    fuzzy_finder_navigate(&editor_state, 20);
    TEST_ASSERT_EQUAL_INT(8 - FUZZY_FINDER_PAGE_SIZE, editor_state.fuzzy_finder_scroll);
}

void test_fuzzy_finder_navigate_page_down(void)
{
    reset_input_fakes();
    target_key_for_press = KEY_E;
    IsKeyPressed_fake.custom_fake = press_specific_key;

    EditorState editor_state = {.fuzzy_finder_scroll = 0};
    fuzzy_finder_navigate(&editor_state, 20);
    TEST_ASSERT_EQUAL_INT(FUZZY_FINDER_PAGE_SIZE, editor_state.fuzzy_finder_scroll);
}

/* ---- fuzzy_finder_build_items ------------------------------------------- */

void test_fuzzy_finder_build_items_collects_blueprint_names(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    state.gamedata.blueprints.entries.alloc = test_heap_alloc;

    Blueprint bp_chest = make_named_blueprint("chest");
    Blueprint bp_door = make_named_blueprint("door");
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, bp_chest));
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, bp_door));

    EditorState editor_state = {0};
    fuzzy_finder_build_items(&state, &editor_state);

    TEST_ASSERT_TRUE(editor_state.fuzzy_finder_item_count >= 2);
    bool found_chest = false;
    bool found_door = false;
    for (int index = 0; index < editor_state.fuzzy_finder_item_count; index++) {
        if (strcmp(editor_state.fuzzy_finder_items[index], "chest") == 0) {
            found_chest = true;
        }
        if (strcmp(editor_state.fuzzy_finder_items[index], "door") == 0) {
            found_door = true;
        }
    }
    TEST_ASSERT_TRUE(found_chest);
    TEST_ASSERT_TRUE(found_door);

    test_blueprint_table_free_local(&state.gamedata.blueprints);
    arena_reset(&state.gamedata_arena);
}

void test_fuzzy_finder_build_items_deduplicates(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    state.gamedata.blueprints.entries.alloc = test_heap_alloc;

    Blueprint bp_one = make_named_blueprint("chest");
    Blueprint bp_two = make_named_blueprint("chest");
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, bp_one));
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, bp_two));

    EditorState editor_state = {0};
    fuzzy_finder_build_items(&state, &editor_state);

    int chest_count = 0;
    for (int index = 0; index < editor_state.fuzzy_finder_item_count; index++) {
        if (strcmp(editor_state.fuzzy_finder_items[index], "chest") == 0) {
            chest_count++;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, chest_count);

    test_blueprint_table_free_local(&state.gamedata.blueprints);
    arena_reset(&state.gamedata_arena);
}

void test_fuzzy_finder_build_items_sorts_alphabetically(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    state.gamedata.blueprints.entries.alloc = test_heap_alloc;

    Blueprint bp_z = make_named_blueprint("zebra");
    Blueprint bp_a = make_named_blueprint("apple");
    Blueprint bp_m = make_named_blueprint("mango");
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, bp_z));
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, bp_a));
    TEST_ASSERT_TRUE(vec_blueprint_push(&state.gamedata.blueprints.entries, bp_m));

    EditorState editor_state = {0};
    fuzzy_finder_build_items(&state, &editor_state);

    TEST_ASSERT_TRUE(editor_state.fuzzy_finder_item_count >= 3);
    int apple_index = -1;
    int mango_index = -1;
    int zebra_index = -1;
    for (int index = 0; index < editor_state.fuzzy_finder_item_count; index++) {
        if (strcmp(editor_state.fuzzy_finder_items[index], "apple") == 0) {
            apple_index = index;
        }
        if (strcmp(editor_state.fuzzy_finder_items[index], "mango") == 0) {
            mango_index = index;
        }
        if (strcmp(editor_state.fuzzy_finder_items[index], "zebra") == 0) {
            zebra_index = index;
        }
    }
    TEST_ASSERT_TRUE(apple_index >= 0);
    TEST_ASSERT_TRUE(mango_index >= 0);
    TEST_ASSERT_TRUE(zebra_index >= 0);
    TEST_ASSERT_TRUE(apple_index < mango_index);
    TEST_ASSERT_TRUE(mango_index < zebra_index);

    test_blueprint_table_free_local(&state.gamedata.blueprints);
    arena_reset(&state.gamedata_arena);
}

void test_fuzzy_finder_build_items_collects_entity_tags(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    state.gamedata.current_level.entities.alloc = test_heap_alloc;

    Entity entity = {.parent_index = -1};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &entity.tag, "my_tag"));
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));

    EditorState editor_state = {0};
    fuzzy_finder_build_items(&state, &editor_state);

    bool found_tag = false;
    for (int index = 0; index < editor_state.fuzzy_finder_item_count; index++) {
        if (strcmp(editor_state.fuzzy_finder_items[index], "my_tag") == 0) {
            found_tag = true;
        }
    }
    TEST_ASSERT_TRUE(found_tag);

    str_free(&test_heap_alloc, &state.gamedata.current_level.entities.data[0].tag);
    vec_entity_free(&state.gamedata.current_level.entities);
    arena_reset(&state.gamedata_arena);
}

void test_fuzzy_finder_build_items_collects_flag_names(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    state.gamedata.flags.names.alloc = test_heap_alloc;

    FlagName flag = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &flag.name, "has_key"));
    TEST_ASSERT_TRUE(vec_flag_name_push(&state.gamedata.flags.names, flag));

    EditorState editor_state = {0};
    fuzzy_finder_build_items(&state, &editor_state);

    bool found_flag = false;
    for (int index = 0; index < editor_state.fuzzy_finder_item_count; index++) {
        if (strcmp(editor_state.fuzzy_finder_items[index], "has_key") == 0) {
            found_flag = true;
        }
    }
    TEST_ASSERT_TRUE(found_flag);

    str_free(&test_heap_alloc, &state.gamedata.flags.names.data[0].name);
    vec_flag_name_free(&state.gamedata.flags.names);
    arena_reset(&state.gamedata_arena);
}

/* ---- keyboard_type_char ------------------------------------------------- */

void test_keyboard_type_char_appends(void)
{
    EditorState editor_state = {0};
    keyboard_type_char(&editor_state, 'a');
    TEST_ASSERT_EQUAL_STRING("a", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(1, editor_state.word_builder_len);
}

void test_keyboard_type_char_multiple(void)
{
    EditorState editor_state = {0};
    keyboard_type_char(&editor_state, 'a');
    keyboard_type_char(&editor_state, 'b');
    keyboard_type_char(&editor_state, 'c');
    TEST_ASSERT_EQUAL_STRING("abc", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(3, editor_state.word_builder_len);
}

void test_keyboard_type_char_overflow_noop(void)
{
    EditorState editor_state = {0};
    memset(editor_state.word_builder_buf, 'x', WORD_BUILDER_BUF_SIZE - 1);
    editor_state.word_builder_buf[WORD_BUILDER_BUF_SIZE - 1] = '\0';
    editor_state.word_builder_len = WORD_BUILDER_BUF_SIZE - 1;

    keyboard_type_char(&editor_state, 'z');

    TEST_ASSERT_EQUAL_INT(WORD_BUILDER_BUF_SIZE - 1, editor_state.word_builder_len);
}

/* ---- keyboard_backspace ------------------------------------------------- */

void test_keyboard_backspace_removes_last(void)
{
    EditorState editor_state = {0};
    keyboard_type_char(&editor_state, 'a');
    keyboard_type_char(&editor_state, 'b');
    keyboard_type_char(&editor_state, 'c');
    keyboard_backspace(&editor_state);
    TEST_ASSERT_EQUAL_STRING("ab", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(2, editor_state.word_builder_len);
}

void test_keyboard_backspace_empty_noop(void)
{
    EditorState editor_state = {0};
    keyboard_backspace(&editor_state);
    TEST_ASSERT_EQUAL_STRING("", editor_state.word_builder_buf);
    TEST_ASSERT_EQUAL_INT(0, editor_state.word_builder_len);
}

/* ---- keyboard data integrity -------------------------------------------- */

void test_keyboard_group_sizes_consistent(void)
{
    for (int group = 0; group < KEYBOARD_GROUP_COUNT; group++) {
        int actual_size = 0;
        for (int character = 0; character < KEYBOARD_MAX_CHARS_PER_GROUP; character++) {
            if (keyboard_groups[group][character] != '\0') {
                actual_size++;
            }
        }
        TEST_ASSERT_EQUAL_INT(keyboard_group_sizes[group], actual_size);
    }
}

void test_keyboard_all_lowercase_alpha_present(void)
{
    for (int code = 'a'; code <= 'z'; code++) {
        char letter = (char)code;
        bool found = false;
        for (int group = 0; group < KEYBOARD_GROUP_COUNT && !found; group++) {
            for (int character = 0; character < keyboard_group_sizes[group]; character++) {
                if (keyboard_groups[group][character] == letter) {
                    found = true;
                    break;
                }
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(found, TextFormat("letter '%c' not found in any keyboard group", letter));
    }
}

void test_keyboard_all_digits_present(void)
{
    for (int code = '0'; code <= '9'; code++) {
        char digit = (char)code;
        bool found = false;
        for (int group = 0; group < KEYBOARD_GROUP_COUNT && !found; group++) {
            for (int character = 0; character < keyboard_group_sizes[group]; character++) {
                if (keyboard_groups[group][character] == digit) {
                    found = true;
                    break;
                }
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(found, TextFormat("digit '%c' not found in any keyboard group", digit));
    }
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

/* ---- Attribute editor: add / remove / type change ----------------------- */

void test_attr_add_by_name_creates_int_attr(void)
{
    ErrorState err_state = {0};
    DebugState dbg_state = {0};
    Diag diag = {.error = &err_state, .debug = &dbg_state};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err_state, &state.gamedata_arena));
    state.gamedata.current_level.entities.alloc = allocator_arena(&state.gamedata_arena);
    Entity entity = {.id = 1};
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));
    EditorState editor_state = {.selected_entity_index = 0};

    TEST_ASSERT_TRUE(add_attr_by_name(&diag, &state, &editor_state, "new_attr"));

    Entity *result = &state.gamedata.current_level.entities.data[0];
    TEST_ASSERT_EQUAL_INT(1, result->attrs.entries.count);
    TEST_ASSERT_EQUAL_STRING("new_attr", result->attrs.entries.data[0].name.ptr);
    TEST_ASSERT_EQUAL_INT(ATTR_INT, result->attrs.entries.data[0].type);
    TEST_ASSERT_EQUAL_INT(0, result->attrs.entries.data[0].value.i);

    arena_reset(&state.gamedata_arena);
}

void test_attr_add_by_name_duplicate_returns_false(void)
{
    ErrorState err_state = {0};
    DebugState dbg_state = {0};
    Diag diag = {.error = &err_state, .debug = &dbg_state};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err_state, &state.gamedata_arena));
    state.gamedata.current_level.entities.alloc = allocator_arena(&state.gamedata_arena);
    Allocator alloc = allocator_arena(&state.gamedata_arena);
    Entity entity = {.id = 1};
    TEST_ASSERT_TRUE(attr_set_int(&alloc, &entity.attrs, "existing", 42));
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));
    EditorState editor_state = {.selected_entity_index = 0};

    TEST_ASSERT_FALSE(add_attr_by_name(&diag, &state, &editor_state, "existing"));

    arena_reset(&state.gamedata_arena);
}

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
    Str test_str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &test_str, "42"));
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

void test_attr_radial_label_float(void)
{
    EditorState editor_state = {.radial_context = RADIAL_CTX_ATTR_TYPE};
    TEST_ASSERT_EQUAL_STRING("Float", radial_label(&editor_state, 0));
}

void test_attr_radial_label_string(void)
{
    EditorState editor_state = {.radial_context = RADIAL_CTX_ATTR_TYPE};
    TEST_ASSERT_EQUAL_STRING("String", radial_label(&editor_state, 3));
}

void test_diff_view_override_detection(void)
{
    AttrSet instance = {0};
    AttrSet blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &instance, "speed", 20));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint, "speed", 10));

    const Attribute *found = attr_get(&blueprint, "speed");
    TEST_ASSERT_NOT_NULL(found);

    test_attr_set_free_local(&instance);
    test_attr_set_free_local(&blueprint);
}

void test_diff_view_custom_detection(void)
{
    AttrSet instance = {0};
    AttrSet blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &instance, "custom_flag", 1));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint, "speed", 10));

    TEST_ASSERT_NULL(attr_get(&blueprint, "custom_flag"));

    test_attr_set_free_local(&instance);
    test_attr_set_free_local(&blueprint);
}

/* ---- Tree section helpers ------------------------------------------------ */

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

void test_tree_child_index_mapping(void)
{
    Entity root = {.parent_index = -1};
    TEST_ASSERT_EQUAL_INT(0, tree_child_index(&root, 0));
    TEST_ASSERT_EQUAL_INT(1, tree_child_index(&root, 1));

    Entity child = {.parent_index = 0};
    TEST_ASSERT_EQUAL_INT(-1, tree_child_index(&child, 0));
    TEST_ASSERT_EQUAL_INT(0, tree_child_index(&child, 1));
}

void test_tree_is_add_child_sentinel(void)
{
    Entity entity = {.parent_index = -1};
    BlueprintChild children[2] = {0};
    Blueprint blueprint = {.children = {.data = children, .count = 2}};
    TEST_ASSERT_FALSE(tree_is_add_child_row(&entity, &blueprint, 0));
    TEST_ASSERT_FALSE(tree_is_add_child_row(&entity, &blueprint, 1));
    TEST_ASSERT_TRUE(tree_is_add_child_row(&entity, &blueprint, 2));
}

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

void test_child_radial_label_tag(void)
{
    EditorState editor_state = {0};
    editor_state.radial_context = RADIAL_CTX_CHILD_PROPS;
    TEST_ASSERT_EQUAL_STRING("Tag", radial_label(&editor_state, 0));
}

void test_child_radial_label_offset(void)
{
    EditorState editor_state = {0};
    editor_state.radial_context = RADIAL_CTX_CHILD_PROPS;
    TEST_ASSERT_EQUAL_STRING("Offset X", radial_label(&editor_state, 1));
    TEST_ASSERT_EQUAL_STRING("Offset Y", radial_label(&editor_state, 2));
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();

    RUN_TEST(test_editor_radial_dead_zone_returns_negative_one);
    RUN_TEST(test_editor_radial_stick_up_four_items);
    RUN_TEST(test_editor_radial_stick_right_four_items);
    RUN_TEST(test_editor_radial_stick_down_four_items);
    RUN_TEST(test_editor_radial_stick_left_four_items);
    RUN_TEST(test_editor_radial_label_grab);
    RUN_TEST(test_editor_radial_label_delete);
    RUN_TEST(test_editor_radial_label_out_of_bounds);
    RUN_TEST(test_editor_word_builder_append_to_empty);
    RUN_TEST(test_editor_word_builder_append_with_underscore);
    RUN_TEST(test_editor_word_builder_append_overflow_noop);
    RUN_TEST(test_editor_word_builder_append_multiple);
    RUN_TEST(test_editor_word_builder_pop_last_word);
    RUN_TEST(test_editor_word_builder_pop_single_word);
    RUN_TEST(test_editor_word_builder_pop_empty_noop);
    RUN_TEST(test_editor_word_builder_total_count_no_blueprints);
    RUN_TEST(test_editor_word_builder_total_count_with_blueprints);
    RUN_TEST(test_editor_word_builder_item_zero_is_done);
    RUN_TEST(test_editor_word_builder_item_first_builtin);
    RUN_TEST(test_editor_word_builder_item_blueprint_name);
    RUN_TEST(test_editor_word_builder_item_negative_index);
    RUN_TEST(test_editor_place_visible_count_known_height);
    RUN_TEST(test_editor_place_visible_count_small_height);
    RUN_TEST(test_editor_total_attr_count_instance_only);
    RUN_TEST(test_editor_total_attr_count_with_blueprint);
    RUN_TEST(test_editor_is_blueprint_attr_false_for_instance);
    RUN_TEST(test_editor_is_blueprint_attr_true_for_blueprint);
    RUN_TEST(test_editor_find_nearest_single_entity);
    RUN_TEST(test_editor_find_nearest_closer_wins);
    RUN_TEST(test_editor_find_nearest_skips_children);
    RUN_TEST(test_editor_find_nearest_empty_level);
    RUN_TEST(test_editor_entity_outline_rect_with_collision);
    RUN_TEST(test_editor_entity_outline_rect_without_collision);
    RUN_TEST(test_editor_find_blueprint_by_name_found);
    RUN_TEST(test_editor_find_blueprint_by_name_not_found);
    RUN_TEST(test_editor_find_blueprint_by_name_empty_table);
    RUN_TEST(test_editor_mark_deleted_root_marks_child);
    RUN_TEST(test_editor_mark_deleted_chain);
    RUN_TEST(test_editor_mark_deleted_sibling_untouched);
    RUN_TEST(test_editor_apply_attr_delta_int);
    RUN_TEST(test_editor_apply_attr_delta_float);
    RUN_TEST(test_editor_apply_attr_delta_null_attr_no_crash);
    RUN_TEST(test_editor_attr_at_display_index_instance);
    RUN_TEST(test_editor_attr_at_display_index_blueprint);
    RUN_TEST(test_editor_attr_at_display_index_out_of_range);
    RUN_TEST(test_editor_toggle_pressed_key);
    RUN_TEST(test_editor_toggle_pressed_gamepad);
    RUN_TEST(test_editor_toggle_pressed_neither);
    RUN_TEST(test_editor_read_value_delta_no_input);
    RUN_TEST(test_editor_read_value_delta_large_minus);
    RUN_TEST(test_editor_read_value_delta_large_plus);
    RUN_TEST(test_editor_read_value_delta_huge_minus);
    RUN_TEST(test_editor_read_value_delta_combined);
    RUN_TEST(test_editor_read_held_dir_left_key);
    RUN_TEST(test_editor_read_held_dir_right_gamepad);
    RUN_TEST(test_editor_read_held_dir_none);
    RUN_TEST(test_editor_word_builder_nav_up);
    RUN_TEST(test_editor_word_builder_nav_up_clamped);
    RUN_TEST(test_editor_word_builder_nav_down);
    RUN_TEST(test_editor_word_builder_nav_down_clamped);
    RUN_TEST(test_editor_word_builder_nav_page_up);
    RUN_TEST(test_editor_word_builder_nav_page_down);
    RUN_TEST(test_editor_delete_entity_removes_map_entries);
    RUN_TEST(test_fuzzy_finder_contains_found);
    RUN_TEST(test_fuzzy_finder_contains_not_found);
    RUN_TEST(test_fuzzy_finder_contains_empty);
    RUN_TEST(test_fuzzy_finder_item_zero_is_new);
    RUN_TEST(test_fuzzy_finder_item_negative_is_new);
    RUN_TEST(test_fuzzy_finder_item_returns_name);
    RUN_TEST(test_fuzzy_finder_item_out_of_range);
    RUN_TEST(test_fuzzy_finder_total_count_value);
    RUN_TEST(test_fuzzy_finder_navigate_up_clamped);
    RUN_TEST(test_fuzzy_finder_navigate_down_clamped);
    RUN_TEST(test_fuzzy_finder_navigate_page_up);
    RUN_TEST(test_fuzzy_finder_navigate_page_down);
    RUN_TEST(test_fuzzy_finder_build_items_collects_blueprint_names);
    RUN_TEST(test_fuzzy_finder_build_items_deduplicates);
    RUN_TEST(test_fuzzy_finder_build_items_sorts_alphabetically);
    RUN_TEST(test_fuzzy_finder_build_items_collects_entity_tags);
    RUN_TEST(test_fuzzy_finder_build_items_collects_flag_names);
    RUN_TEST(test_keyboard_type_char_appends);
    RUN_TEST(test_keyboard_type_char_multiple);
    RUN_TEST(test_keyboard_type_char_overflow_noop);
    RUN_TEST(test_keyboard_backspace_removes_last);
    RUN_TEST(test_keyboard_backspace_empty_noop);
    RUN_TEST(test_keyboard_group_sizes_consistent);
    RUN_TEST(test_keyboard_all_lowercase_alpha_present);
    RUN_TEST(test_keyboard_all_digits_present);
    RUN_TEST(test_attr_add_by_name_creates_int_attr);
    RUN_TEST(test_attr_add_by_name_duplicate_returns_false);
    RUN_TEST(test_attr_remove_instance_attr);
    RUN_TEST(test_attr_type_change_int_to_float);
    RUN_TEST(test_attr_type_change_float_to_int);
    RUN_TEST(test_attr_type_change_int_to_bool_nonzero);
    RUN_TEST(test_attr_type_change_int_to_bool_zero);
    RUN_TEST(test_attr_type_change_string_to_int);
    RUN_TEST(test_attr_type_radial_order_matches_enum);
    RUN_TEST(test_attr_radial_label_float);
    RUN_TEST(test_attr_radial_label_string);
    RUN_TEST(test_diff_view_override_detection);
    RUN_TEST(test_diff_view_custom_detection);
    RUN_TEST(test_tree_section_total_root_no_children);
    RUN_TEST(test_tree_section_total_root_with_children);
    RUN_TEST(test_tree_section_total_child_entity);
    RUN_TEST(test_tree_is_parent_row_true);
    RUN_TEST(test_tree_is_parent_row_false);
    RUN_TEST(test_tree_child_index_mapping);
    RUN_TEST(test_tree_is_add_child_sentinel);
    RUN_TEST(test_navigate_tree_to_attr_boundary);
    RUN_TEST(test_navigate_attr_to_tree_boundary);
    RUN_TEST(test_child_radial_label_tag);
    RUN_TEST(test_child_radial_label_offset);

    return UNITY_END();
}
