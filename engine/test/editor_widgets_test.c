#include "fff.h"
#include "unity.h"

#include "raylib.h"

DEFINE_FFF_GLOBALS;

/* raylib draw fakes (used by draw_radial_picker, draw_gamepad_kb, etc.) */
FAKE_VOID_FUNC(DrawCircle, int, int, float, Color);
FAKE_VOID_FUNC(DrawRing, Vector2, float, float, float, float, int, Color);
FAKE_VOID_FUNC(DrawRectangle, int, int, int, int, Color);
FAKE_VOID_FUNC(DrawTextEx, Font, const char *, Vector2, float, float, Color);
FAKE_VALUE_FUNC(Vector2, MeasureTextEx, Font, const char *, float, float);

/* Raylib input fakes — input_capture polls these but the unit tests
 * construct InputState directly; the fakes never actually fire. */
FAKE_VALUE_FUNC(int, SetGamepadMappings, const char *);
FAKE_VALUE_FUNC(bool, IsGamepadAvailable, int);
FAKE_VALUE_FUNC(float, GetGamepadAxisMovement, int, int);
FAKE_VALUE_FUNC(bool, IsKeyPressed, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonPressed, int, int);
FAKE_VALUE_FUNC(bool, IsKeyDown, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonDown, int, int);

#include "../src/strv.c"            // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"             // NOLINT(bugprone-suspicious-include)
#include "../src/error.c"           // NOLINT(bugprone-suspicious-include)
#include "../src/arena_posix.c"     // NOLINT(bugprone-suspicious-include)
#include "../src/attribute.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/entity.c"          // NOLINT(bugprone-suspicious-include)
#include "../src/input.c"           // NOLINT(bugprone-suspicious-include)
#include "../src/input_func.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/map.c"             // NOLINT(bugprone-suspicious-include)
#include "../src/vec.c"             // NOLINT(bugprone-suspicious-include)
#include "../src/keyboard_widget.c" // NOLINT(bugprone-suspicious-include)
#include "../src/editor/widgets.c"  // NOLINT(bugprone-suspicious-include)

/* Cross-file editor fakes: draw.c */
FAKE_VOID_FUNC(draw_ui_text, Font, const char *, int, int, int, Color);
FAKE_VALUE_FUNC(int, measure_ui_text, Font, const char *, int);

/* extern color constants from draw.c */
const Color debug_text_color = {0};
const Color debug_bg_color = {0};
const Color debug_log_color = {0};

/* Cross-file editor fakes: core.c */
FAKE_VALUE_FUNC(Blueprint *, find_blueprint_by_name, GameState *, const char *);
FAKE_VALUE_FUNC(bool, is_blueprint_attr, const GameState *, const Entity *, int);
FAKE_VALUE_FUNC(Attribute *, attr_at_display_index, GameState *, Entity *, int);
FAKE_VALUE_FUNC(AttrRow, attr_row_at, const GameState *, const Entity *, int);
FAKE_VALUE_FUNC(AttrSet *, attr_section_set, GameState *, Entity *, AttrSection);
FAKE_VALUE_FUNC(int, place_visible_count, int);

/* Cross-file editor fakes: attr.c */
FAKE_VOID_FUNC(confirm_child_tag_edit, Diag *, GameState *, EditorState *, UndoHistory *);

/* Cross-file editor fakes: blueprint.c */
FAKE_VOID_FUNC(create_blank_blueprint, GameState *, EditorState *, UndoHistory *, const char *);
FAKE_VOID_FUNC(duplicate_blueprint, GameState *, EditorState *, UndoHistory *, const char *);

/* Cross-file editor fakes: child.c */
FAKE_VOID_FUNC(
    add_blueprint_child, Diag *, GameState *, EditorState *, UndoHistory *, const char *, TextureLookupFn, void *);

/* External module fakes */
FAKE_VOID_FUNC(undo_history_new_entry, UndoHistory *, GamedataState *, Arena *, ArenaCheckpoint, Strv);

/* TextFormat stub — variadic, cannot use FAKE_VALUE_FUNC */
const char *TextFormat(const char *text, ...)
{
    (void)text;
    return "";
}

/* debug_log stub — variadic with __attribute__((format)). The real
 * debug.c is not pulled into the test, but input.c includes debug.h
 * and main.c calls it from input_load_mappings; provide a no-op stub. */
/* debug_log lives in debug.c which we do NOT include; provide a stub. */
/* Note: input.c uses debug_log in input_load_mappings. */
#ifndef WIDGETS_TEST_DEBUG_LOG_STUBBED
#define WIDGETS_TEST_DEBUG_LOG_STUBBED 1
void debug_log(DebugState *dbg, const char *format, ...)
{
    (void)dbg;
    (void)format;
}
#endif

/* VEC_IMPL / MAP_IMPL for types needed by test setup/cleanup */
VEC_IMPL(blueprint_child, BlueprintChild)
VEC_IMPL(blueprint, Blueprint)
VEC_IMPL(flag_name, FlagName)

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

static BindingStore test_widget_bindings;
static bool test_widget_bindings_loaded;
static const BindingStore *get_test_bindings(void)
{
    if (!test_widget_bindings_loaded) {
        input_func_load_defaults(&test_widget_bindings, test_heap_alloc);
        test_widget_bindings_loaded = true;
    }
    return &test_widget_bindings;
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

void test_editor_radial_label_blueprints(void)
{
    EditorState editor_state = {.radial_context = RADIAL_CTX_TOOLS};
    TEST_ASSERT_EQUAL_STRING("Blueprints", radial_label(&editor_state, 4));
}

void test_editor_radial_label_out_of_bounds(void)
{
    EditorState editor_state = {.radial_context = RADIAL_CTX_TOOLS};
    TEST_ASSERT_EQUAL_STRING("", radial_label(&editor_state, EDITOR_TOOLS_ITEM_COUNT));
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

/* ---- word_builder_navigate ---------------------------------------------- */

void test_editor_word_builder_nav_up(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_UP);
    EditorState editor_state = {.word_builder_scroll = 5};
    word_builder_navigate(&editor_state, &input, get_test_bindings(), 10);
    TEST_ASSERT_EQUAL_INT(4, editor_state.word_builder_scroll);
}

void test_editor_word_builder_nav_up_clamped(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_UP);
    EditorState editor_state = {.word_builder_scroll = 0};
    word_builder_navigate(&editor_state, &input, get_test_bindings(), 10);
    TEST_ASSERT_EQUAL_INT(0, editor_state.word_builder_scroll);
}

void test_editor_word_builder_nav_down(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_DOWN);
    EditorState editor_state = {.word_builder_scroll = 0};
    word_builder_navigate(&editor_state, &input, get_test_bindings(), 10);
    TEST_ASSERT_EQUAL_INT(1, editor_state.word_builder_scroll);
}

void test_editor_word_builder_nav_down_clamped(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_DOWN);
    EditorState editor_state = {.word_builder_scroll = 9};
    word_builder_navigate(&editor_state, &input, get_test_bindings(), 10);
    TEST_ASSERT_EQUAL_INT(9, editor_state.word_builder_scroll);
}

void test_editor_word_builder_nav_page_up(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_Q);
    EditorState editor_state = {.word_builder_scroll = 8};
    word_builder_navigate(&editor_state, &input, get_test_bindings(), 20);
    TEST_ASSERT_EQUAL_INT(8 - WORD_BUILDER_PAGE_SIZE, editor_state.word_builder_scroll);
}

void test_editor_word_builder_nav_page_down(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_E);
    EditorState editor_state = {.word_builder_scroll = 0};
    word_builder_navigate(&editor_state, &input, get_test_bindings(), 20);
    TEST_ASSERT_EQUAL_INT(WORD_BUILDER_PAGE_SIZE, editor_state.word_builder_scroll);
}

/* ---- add_attr_by_name --------------------------------------------------- */

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

void test_attr_add_by_name_persisted_targets_persisted_set(void)
{
    ErrorState err_state = {0};
    DebugState dbg_state = {0};
    Diag diag = {.error = &err_state, .debug = &dbg_state};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err_state, &state.gamedata_arena));
    state.gamedata.current_level.entities.alloc = allocator_arena(&state.gamedata_arena);
    Entity entity = {.id = 1, .parent_index = -1};
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));
    EditorState editor_state = {
        .selected_entity_index = 0,
        .top_mode = EDITOR_TOP_SCENE,
        .adding_persisted_attr = true,
    };

    TEST_ASSERT_TRUE(add_attr_by_name(&diag, &state, &editor_state, "hp"));

    Entity *result = &state.gamedata.current_level.entities.data[0];
    TEST_ASSERT_EQUAL_INT(1, result->persisted_attrs.entries.count);
    TEST_ASSERT_EQUAL_STRING("hp", result->persisted_attrs.entries.data[0].name.ptr);
    TEST_ASSERT_EQUAL_INT(ATTR_INT, result->persisted_attrs.entries.data[0].type);
    /* Runtime set untouched */
    TEST_ASSERT_EQUAL_INT(0, result->attrs.entries.count);

    arena_reset(&state.gamedata_arena);
}

void test_attr_add_by_name_runtime_targets_runtime_set(void)
{
    ErrorState err_state = {0};
    DebugState dbg_state = {0};
    Diag diag = {.error = &err_state, .debug = &dbg_state};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err_state, &state.gamedata_arena));
    state.gamedata.current_level.entities.alloc = allocator_arena(&state.gamedata_arena);
    Entity entity = {.id = 1, .parent_index = -1};
    TEST_ASSERT_TRUE(vec_entity_push(&state.gamedata.current_level.entities, entity));
    EditorState editor_state = {
        .selected_entity_index = 0,
        .top_mode = EDITOR_TOP_SCENE,
        .adding_persisted_attr = false,
    };

    TEST_ASSERT_TRUE(add_attr_by_name(&diag, &state, &editor_state, "speed"));

    Entity *result = &state.gamedata.current_level.entities.data[0];
    TEST_ASSERT_EQUAL_INT(1, result->attrs.entries.count);
    TEST_ASSERT_EQUAL_STRING("speed", result->attrs.entries.data[0].name.ptr);
    /* Persisted set untouched */
    TEST_ASSERT_EQUAL_INT(0, result->persisted_attrs.entries.count);

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
    InputState input = {0};
    input_state_press_key(&input, KEY_UP);
    EditorState editor_state = {.fuzzy_finder_scroll = 0};
    fuzzy_finder_navigate(&editor_state, &input, get_test_bindings(), 10);
    TEST_ASSERT_EQUAL_INT(0, editor_state.fuzzy_finder_scroll);
}

void test_fuzzy_finder_navigate_down_clamped(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_DOWN);
    EditorState editor_state = {.fuzzy_finder_scroll = 9};
    fuzzy_finder_navigate(&editor_state, &input, get_test_bindings(), 10);
    TEST_ASSERT_EQUAL_INT(9, editor_state.fuzzy_finder_scroll);
}

void test_fuzzy_finder_navigate_page_up(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_Q);
    EditorState editor_state = {.fuzzy_finder_scroll = 8};
    fuzzy_finder_navigate(&editor_state, &input, get_test_bindings(), 20);
    TEST_ASSERT_EQUAL_INT(8 - FUZZY_FINDER_PAGE_SIZE, editor_state.fuzzy_finder_scroll);
}

void test_fuzzy_finder_navigate_page_down(void)
{
    InputState input = {0};
    input_state_press_key(&input, KEY_E);
    EditorState editor_state = {.fuzzy_finder_scroll = 0};
    fuzzy_finder_navigate(&editor_state, &input, get_test_bindings(), 20);
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
    entity.tag = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&entity.tag, "my_tag"));
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

    str_free(&state.gamedata.current_level.entities.data[0].tag);
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
    flag.name = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&flag.name, "has_key"));
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

    str_free(&state.gamedata.flags.names.data[0].name);
    vec_flag_name_free(&state.gamedata.flags.names);
    arena_reset(&state.gamedata_arena);
}

/* keyboard_type_char / keyboard_backspace are now private helpers of
 * keyboard_widget.c. End-to-end coverage of typing, backspacing, and
 * keyboard char-group integrity lives in keyboard_widget_test.c, which
 * exercises the public widget API. */

/* ---- compare_cstr_ptrs -------------------------------------------------- */

void test_compare_cstr_ptrs_ordering(void)
{
    const char *items[] = {"cherry", "apple", "banana"};
    qsort((void *)items, 3, sizeof(const char *), compare_cstr_ptrs);
    TEST_ASSERT_EQUAL_STRING("apple", items[0]);
    TEST_ASSERT_EQUAL_STRING("banana", items[1]);
    TEST_ASSERT_EQUAL_STRING("cherry", items[2]);
}

/* ---- fuzzy_finder_try_add ----------------------------------------------- */

void test_fuzzy_finder_try_add_new_item(void)
{
    const char *items[8] = {0};
    int count = 0;
    fuzzy_finder_try_add(items, &count, "hello");
    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_EQUAL_STRING("hello", items[0]);
}

void test_fuzzy_finder_try_add_duplicate(void)
{
    const char *items[8] = {0};
    int count = 0;
    fuzzy_finder_try_add(items, &count, "hello");
    fuzzy_finder_try_add(items, &count, "hello");
    TEST_ASSERT_EQUAL_INT(1, count);
}

/* ---- fuzzy_finder_max_item_count ---------------------------------------- */

void test_fuzzy_finder_max_item_count_sums_all(void)
{
    GamedataState gamedata = {0};
    gamedata.blueprints.entries.alloc = test_heap_alloc;
    Blueprint blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint.attrs, "speed", 10));
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &blueprint.attrs, "health", 5));
    (void)vec_blueprint_push(&gamedata.blueprints.entries, blueprint);

    gamedata.current_level.entities.alloc = test_heap_alloc;
    Entity entity = {0};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &entity.attrs, "x", 1));
    (void)vec_entity_push(&gamedata.current_level.entities, entity);

    int result = fuzzy_finder_max_item_count(&gamedata);
    /* 1 blueprint + 0 flags + 1 (current level) + 0 other levels + 1 entity + 2 bp attrs + 1 entity attr = 6 */
    TEST_ASSERT_EQUAL_INT(6, result);

    attr_set_free(&test_heap_alloc, &blueprint.attrs);
    vec_blueprint_free(&gamedata.blueprints.entries);
    attr_set_free(&test_heap_alloc, &entity.attrs);
    vec_entity_free(&gamedata.current_level.entities);
}

/* ---- word_builder_confirm ----------------------------------------------- */

void test_word_builder_confirm_sets_string_value(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    Entity entity = {.parent_index = -1};
    entity.blueprint_name = str_new(alloc);
    (void)str_from_cstr(&entity.blueprint_name, "npc");
    Attribute attr = {.type = ATTR_STRING, .value = {.str = str_new(alloc)}};
    attr.name = str_new(alloc);
    (void)str_from_cstr(&attr.name, "greeting");
    (void)str_from_cstr(&attr.value.str, "old");
    entity.attrs.entries.alloc = alloc;
    (void)vec_attribute_push(&entity.attrs.entries, attr);
    (void)vec_entity_push(&state.gamedata.current_level.entities, entity);

    attr_at_display_index_fake.return_val = &state.gamedata.current_level.entities.data[0].attrs.entries.data[0];
    is_blueprint_attr_fake.return_val = false;
    attr_row_at_fake.return_val =
        (AttrRow){.kind = ATTR_ROW_KIND_ATTR, .section = ATTR_SECTION_RUNTIME, .index_in_section = 0};
    attr_section_set_fake.return_val = &state.gamedata.current_level.entities.data[0].attrs;

    EditorState editor_state = {.selected_entity_index = 0, .selected_attr_index = 0};
    strcpy(editor_state.word_builder_buf, "hello");
    editor_state.word_builder_len = 5;
    Diag diag = {.error = &err};

    word_builder_confirm(&diag, &state, &editor_state);

    Attribute *updated = &state.gamedata.current_level.entities.data[0].attrs.entries.data[0];
    TEST_ASSERT_EQUAL_STRING("hello", updated->value.str.ptr);

    str_free(&updated->name);
    arena_free(&state.gamedata_arena);
}

/* ---- fuzzy_finder_confirm ----------------------------------------------- */

void test_fuzzy_finder_confirm_sets_string_value(void)
{
    ErrorState err = {0};
    GameState state = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state.gamedata_arena));
    Allocator alloc = allocator_arena(&state.gamedata_arena);

    state.gamedata.current_level.entities = vec_entity_new(alloc);
    Entity entity = {.parent_index = -1};
    entity.blueprint_name = str_new(alloc);
    (void)str_from_cstr(&entity.blueprint_name, "npc");
    Attribute attr = {.type = ATTR_STRING, .value = {.str = str_new(alloc)}};
    attr.name = str_new(alloc);
    (void)str_from_cstr(&attr.name, "target");
    (void)str_from_cstr(&attr.value.str, "old");
    entity.attrs.entries.alloc = alloc;
    (void)vec_attribute_push(&entity.attrs.entries, attr);
    (void)vec_entity_push(&state.gamedata.current_level.entities, entity);

    attr_at_display_index_fake.return_val = &state.gamedata.current_level.entities.data[0].attrs.entries.data[0];
    is_blueprint_attr_fake.return_val = false;
    attr_row_at_fake.return_val =
        (AttrRow){.kind = ATTR_ROW_KIND_ATTR, .section = ATTR_SECTION_RUNTIME, .index_in_section = 0};
    attr_section_set_fake.return_val = &state.gamedata.current_level.entities.data[0].attrs;

    const char *items[] = {"chosen_name"};
    EditorState editor_state = {
        .selected_entity_index = 0,
        .selected_attr_index = 0,
        .fuzzy_finder_scroll = 1,
        .fuzzy_finder_items = items,
        .fuzzy_finder_item_count = 1,
    };
    Diag diag = {.error = &err};

    fuzzy_finder_confirm(&diag, &state, &editor_state);

    Attribute *updated = &state.gamedata.current_level.entities.data[0].attrs.entries.data[0];
    TEST_ASSERT_EQUAL_STRING("chosen_name", updated->value.str.ptr);

    str_free(&updated->name);
    arena_free(&state.gamedata_arena);
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
    RUN_TEST(test_editor_radial_label_blueprints);
    RUN_TEST(test_editor_radial_label_out_of_bounds);
    RUN_TEST(test_attr_radial_label_float);
    RUN_TEST(test_attr_radial_label_string);
    RUN_TEST(test_child_radial_label_tag);
    RUN_TEST(test_child_radial_label_offset);
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
    RUN_TEST(test_editor_word_builder_nav_up);
    RUN_TEST(test_editor_word_builder_nav_up_clamped);
    RUN_TEST(test_editor_word_builder_nav_down);
    RUN_TEST(test_editor_word_builder_nav_down_clamped);
    RUN_TEST(test_editor_word_builder_nav_page_up);
    RUN_TEST(test_editor_word_builder_nav_page_down);
    RUN_TEST(test_attr_add_by_name_creates_int_attr);
    RUN_TEST(test_attr_add_by_name_persisted_targets_persisted_set);
    RUN_TEST(test_attr_add_by_name_runtime_targets_runtime_set);
    RUN_TEST(test_attr_add_by_name_duplicate_returns_false);
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
    /* keyboard_* tests moved to keyboard_widget_test.c */

    RUN_TEST(test_compare_cstr_ptrs_ordering);
    RUN_TEST(test_fuzzy_finder_try_add_new_item);
    RUN_TEST(test_fuzzy_finder_try_add_duplicate);
    RUN_TEST(test_fuzzy_finder_max_item_count_sums_all);
    RUN_TEST(test_word_builder_confirm_sets_string_value);
    RUN_TEST(test_fuzzy_finder_confirm_sets_string_value);

    return UNITY_END();
}
