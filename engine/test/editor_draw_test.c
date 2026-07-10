#include "fff.h"
#include "unity.h"

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
#include "../src/editor/draw.c" // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

/* raylib draw fakes */
FAKE_VOID_FUNC(DrawLine, int, int, int, int, Color);
FAKE_VOID_FUNC(DrawRectangle, int, int, int, int, Color);
FAKE_VOID_FUNC(DrawTextEx, Font, const char *, Vector2, float, float, Color);
FAKE_VOID_FUNC(DrawRectangleLinesEx, Rectangle, float, Color);
FAKE_VOID_FUNC(DrawTextureRec, Texture2D, Rectangle, Vector2, Color);
FAKE_VALUE_FUNC(Vector2, MeasureTextEx, Font, const char *, float, float);

/* raylib input fakes — input.c's input_capture polls these but the unit
 * tests construct InputState directly; the fakes never fire. */
FAKE_VALUE_FUNC(int, SetGamepadMappings, const char *);
FAKE_VALUE_FUNC(bool, IsGamepadAvailable, int);
FAKE_VALUE_FUNC(float, GetGamepadAxisMovement, int, int);
FAKE_VALUE_FUNC(bool, IsKeyPressed, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonPressed, int, int);
FAKE_VALUE_FUNC(bool, IsKeyDown, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonDown, int, int);

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

/* External module fakes */
FAKE_VALUE_FUNC(Vector2, blueprint_get_collision_offset, const Blueprint *);
FAKE_VALUE_FUNC(Vector2, blueprint_get_collision_size, const Blueprint *);
FAKE_VALUE_FUNC(const AttrSet *, entity_resolve_defaults, const GameState *, int);
FAKE_VALUE_FUNC(const Blueprint *, blueprint_find, const BlueprintTable *, const char *);
/* render.c: draw_collision_handles now visualizes every collision_region/
 * trigger_region primitive (D28), but the render-only tests below never
 * exercise it — this fake exists purely to satisfy the linker. */
FAKE_VOID_FUNC(render_collision_shape_outline, CollisionShape, Vector2, Color);

/* Cross-file editor fakes (functions from other editor split files) */
FAKE_VALUE_FUNC(int, total_attr_count, const GameState *, const Entity *);
FAKE_VALUE_FUNC(bool, entity_has_persisted_section, const Entity *);
FAKE_VALUE_FUNC(int, place_visible_count, int);
FAKE_VALUE_FUNC(int, editor_resolve_selected_attr_index, const GameState *, const Entity *, const EditorState *);

/* Cross-file fake: level.c. draw_editor_panel/draw_editor_highlights/
 * draw_watch_overlay/draw_collision_handles resolve selection by stable id
 * via level_find_entity_by_id, but none of those are exercised by the
 * render-only tests below — this fake exists purely to satisfy the linker,
 * same rationale as the hint table fakes above. */
FAKE_VALUE_FUNC(int, level_find_entity_by_id, const Level *, int);

/* Cross-file editor fakes: per-submode hint table accessors are owned by
 * the other editor TUs and pulled in via the linker only when draw.c
 * dispatches through hints_table_for. The render-only tests never reach
 * draw_hints_bar, so nullptr fakes satisfy the linker without affecting
 * coverage. */
FAKE_VALUE_FUNC(const EditorHintTable *, browse_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, drag_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, handles_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, attr_edit_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, radial_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, word_builder_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, fuzzy_finder_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, gamepad_kb_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, blueprint_list_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, blueprint_detail_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, watch_list_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, level_list_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, level_detail_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, tile_paint_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, tile_palette_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, atlas_browse_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, atlas_region_edit_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, anim_blueprint_list_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, anim_edit_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, anim_frames_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, rule_blueprint_list_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, rule_list_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, rule_subroutine_list_hints_table);
FAKE_VALUE_FUNC(const EditorHintTable *, rule_tree_hints_table);

/* VEC_IMPL needed by input_func.c's ActionBinding/AxisBinding */
VEC_IMPL(blueprint_child, BlueprintChild)
VEC_IMPL(blueprint, Blueprint)

#include "test_heap_alloc.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---- Helpers ------------------------------------------------------------ */

static void test_attr_set_free_local(AttrSet *set)
{
    attr_set_free(&test_heap_alloc, set);
}

static BindingStore test_draw_bindings;
static bool test_draw_bindings_loaded;
static const BindingStore *get_test_bindings(void)
{
    if (!test_draw_bindings_loaded) {
        input_func_load_defaults(&test_draw_bindings, test_heap_alloc);
        test_draw_bindings_loaded = true;
    }
    return &test_draw_bindings;
}

/* ---- editor_hint_table_render ------------------------------------------- */

void test_editor_hint_table_renders_mode_label_and_entries(void)
{
    const EditorActionHint hints[] = {
        {ACTION_CONFIRM, "Pick"},
        {ACTION_EDITOR_UNDO, "Undo"},
    };
    const EditorHintTable table = {
        .hints = hints,
        .count = (int)(sizeof(hints) / sizeof(hints[0])),
        .mode_label = "Test",
    };
    char out[256];
    int len = editor_hint_table_render(get_test_bindings(), &table, out, (int)sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_NOT_NULL(strstr(out, "[Test]"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Pick"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Undo"));
    TEST_ASSERT_NOT_NULL(strstr(out, "Ctrl+Z"));
    TEST_ASSERT_NOT_NULL(strstr(out, "L1+Left"));
}

void test_editor_hint_table_skips_unbound_action(void)
{
    /* No action ships with an empty default binding anymore, so synthesize an
     * unbound action on a local copy of the store: struct-copying the store
     * copies each ActionBinding's alternatives vec by value, so zeroing the
     * copy's alternatives.count makes that action label empty without touching
     * the shared store (we only read count == 0 -> skip, never touch .data). */
    BindingStore store = *get_test_bindings();
    store.actions[ACTION_CANCEL].alternatives.count = 0;
    const EditorActionHint hints[] = {
        {ACTION_CONFIRM, "Confirm"},
        {ACTION_CANCEL, "Should not appear"},
    };
    const EditorHintTable table = {
        .hints = hints,
        .count = 2,
        .mode_label = "M",
    };
    char out[128];
    (void)editor_hint_table_render(&store, &table, out, (int)sizeof(out));
    TEST_ASSERT_NOT_NULL(strstr(out, "Confirm"));
    TEST_ASSERT_NULL(strstr(out, "Should not appear"));
}

void test_editor_hint_table_truncates_with_overflow_tail(void)
{
    const EditorActionHint hints[] = {
        {ACTION_CONFIRM, "One"},
        {ACTION_CANCEL, "Two"},
        {ACTION_NAV_UP, "Three"},
        {ACTION_NAV_DOWN, "Four"},
    };
    const EditorHintTable table = {
        .hints = hints,
        .count = 4,
        .mode_label = "X",
    };
    char out[48]; /* tight: forces truncation but leaves room for the overflow tail */
    int len = editor_hint_table_render(get_test_bindings(), &table, out, (int)sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_TRUE(len < (int)sizeof(out));
    TEST_ASSERT_EQUAL_CHAR('\0', out[len]);
    TEST_ASSERT_NOT_NULL(strstr(out, "+"));
    TEST_ASSERT_NOT_NULL(strstr(out, "more"));
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
    entity.position = (Vector2){10.0F, 20.0F};
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "collision_offset_x", 0.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "collision_offset_y", 0.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "collision_w", 32.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &entity.attrs, "collision_h", 16.0F));

    Rectangle rect = entity_outline_rect(&state, &entity);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 10.0F, rect.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 20.0F, rect.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 32.0F, rect.width);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 16.0F, rect.height);

    test_attr_set_free_local(&entity.attrs);
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

/* ---- diff_view color logic ---------------------------------------------- */

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

/* ---- get_source_rect ---------------------------------------------------- */

void test_get_source_rect_from_instance(void)
{
    AttrSet instance = {0};
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &instance, "src_x", 10.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &instance, "src_y", 20.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &instance, "src_w", 32.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &instance, "src_h", 48.0F));

    Rectangle rect = get_source_rect(&instance, nullptr);
    TEST_ASSERT_EQUAL_FLOAT(10.0F, rect.x);
    TEST_ASSERT_EQUAL_FLOAT(20.0F, rect.y);
    TEST_ASSERT_EQUAL_FLOAT(32.0F, rect.width);
    TEST_ASSERT_EQUAL_FLOAT(48.0F, rect.height);

    test_attr_set_free_local(&instance);
}

void test_get_source_rect_from_defaults(void)
{
    AttrSet instance = {0};
    AttrSet defaults = {0};
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &defaults, "src_x", 5.0F));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &defaults, "src_w", 16.0F));

    Rectangle rect = get_source_rect(&instance, &defaults);
    TEST_ASSERT_EQUAL_FLOAT(5.0F, rect.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, rect.y);
    TEST_ASSERT_EQUAL_FLOAT(16.0F, rect.width);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, rect.height);

    test_attr_set_free_local(&instance);
    test_attr_set_free_local(&defaults);
}

void test_get_source_rect_zero_when_missing(void)
{
    AttrSet instance = {0};
    Rectangle rect = get_source_rect(&instance, nullptr);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, rect.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, rect.y);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, rect.width);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, rect.height);
}

/* ---- draw_editor_highlights lock outlines (S8.7d2) ---------------------- */

#define LOCK_TEST_ENTITY_ID 7

/* Custom fake resolving only the locked entity's id to a real index, so the
 * selection lookup (selected_entity_id = -1) draws nothing and the lock outline
 * is the sole DrawRectangleLinesEx call under test. */
static int lock_test_find_by_id(const Level *level, int entity_id)
{
    (void)level;
    return entity_id == LOCK_TEST_ENTITY_ID ? 0 : -1;
}

void test_editor_draw_lock_highlight_outlines_foreign_lock(void)
{
    RESET_FAKE(DrawRectangleLinesEx);
    RESET_FAKE(level_find_entity_by_id);
    level_find_entity_by_id_fake.custom_fake = lock_test_find_by_id;

    Level level = {.entities.alloc = test_heap_alloc};
    Entity entity = {.id = LOCK_TEST_ENTITY_ID, .parent_index = -1, .position = {10.0F, 20.0F}};
    TEST_ASSERT_TRUE(vec_entity_push(&level.entities, entity));

    GameState state = {0};
    state.gamedata.current_level = level;
    state.network.mode = NET_HOSTING; /* this peer is holder 0; the lock below is another player's */
    state.network.locks[0] = (EntityLock){.entity_id = LOCK_TEST_ENTITY_ID, .holder_player_id = 2, .active = true};

    EditorState editor_state = {0};
    editor_state.selected_entity_id = -1;
    editor_state.multiselect_count = 0;

    draw_editor_highlights(&state, &editor_state, -1);

    /* Exactly one outline drawn -- the foreign lock -- in that holder's palette
     * color (holder 2 -> index 2). */
    TEST_ASSERT_EQUAL_INT(1, DrawRectangleLinesEx_fake.call_count);
    Color expected = lock_holder_colors[2 % LOCK_HOLDER_COLOR_COUNT];
    Color drawn = DrawRectangleLinesEx_fake.arg2_val;
    TEST_ASSERT_EQUAL_UINT8(expected.r, drawn.r);
    TEST_ASSERT_EQUAL_UINT8(expected.g, drawn.g);
    TEST_ASSERT_EQUAL_UINT8(expected.b, drawn.b);
    TEST_ASSERT_EQUAL_UINT8(expected.a, drawn.a);

    vec_entity_free(&level.entities);
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();

    RUN_TEST(test_editor_hint_table_renders_mode_label_and_entries);
    RUN_TEST(test_editor_hint_table_skips_unbound_action);
    RUN_TEST(test_editor_hint_table_truncates_with_overflow_tail);
    RUN_TEST(test_editor_find_nearest_single_entity);
    RUN_TEST(test_editor_find_nearest_closer_wins);
    RUN_TEST(test_editor_find_nearest_skips_children);
    RUN_TEST(test_editor_find_nearest_empty_level);
    RUN_TEST(test_editor_entity_outline_rect_with_collision);
    RUN_TEST(test_editor_entity_outline_rect_without_collision);
    RUN_TEST(test_diff_view_override_detection);
    RUN_TEST(test_diff_view_custom_detection);
    RUN_TEST(test_get_source_rect_from_instance);
    RUN_TEST(test_get_source_rect_from_defaults);
    RUN_TEST(test_get_source_rect_zero_when_missing);
    RUN_TEST(test_editor_draw_lock_highlight_outlines_foreign_lock);

    return UNITY_END();
}
