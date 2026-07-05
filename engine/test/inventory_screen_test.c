#include "fff.h"
#include "unity.h"

#include "blur.h"
#include "raylib.h"

DEFINE_FFF_GLOBALS;

/* inventory_screen_handle_input reads through input_pressed, same as
 * menu_handle_input/settings_handle_input -- construct InputState
 * directly via the input_state_* helpers rather than driving these
 * fakes; they exist only to satisfy the linker (input.c's
 * input_capture polls them, unused by these tests). */
FAKE_VALUE_FUNC(int, SetGamepadMappings, const char *);
FAKE_VALUE_FUNC(bool, IsGamepadAvailable, int);
FAKE_VALUE_FUNC(float, GetGamepadAxisMovement, int, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonPressed, int, int);
FAKE_VALUE_FUNC(bool, IsGamepadButtonDown, int, int);
FAKE_VALUE_FUNC(bool, IsKeyDown, int);
FAKE_VALUE_FUNC(bool, IsKeyPressed, int);

/* raylib draw fakes -- used only by inventory_screen_render, which these
 * pure-core-focused tests never call. Present purely to satisfy the
 * linker, same rationale as menu_test.c/settings_capture_test.c. */
FAKE_VOID_FUNC(DrawRectangle, int, int, int, int, Color);
FAKE_VOID_FUNC(DrawTextEx, Font, const char *, Vector2, float, float, Color);
FAKE_VALUE_FUNC(Vector2, MeasureTextEx, Font, const char *, float, float);
FAKE_VOID_FUNC(blur_draw, const BlurPipeline *, Rectangle);

/* TextFormat stub -- variadic, cannot use FAKE_VALUE_FUNC. */
const char *TextFormat(const char *text, ...)
{
    (void)text;
    return "";
}

#include "debug.h"
void debug_log(DebugState *dbg, const char *format, ...)
{
    (void)dbg;
    (void)format;
}

#include "../src/strv.c"             // NOLINT(bugprone-suspicious-include)
#include "../src/input.c"            // NOLINT(bugprone-suspicious-include)
#include "../src/input_func.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/inventory_screen.c" // NOLINT(bugprone-suspicious-include)

#include "test_heap_alloc.h"

void setUp(void)
{
    test_helpers_init();
}

void tearDown(void) {}

/* ---- inventory_screen_collect_items ------------------------------------- */

static const InventoryItemEntry *find_entry(const vec_inventory_item_entry *entries, const char *name)
{
    for (int index = 0; index < entries->count; index++) {
        if (strv_eq_cstr(entries->data[index].name, name)) {
            return &entries->data[index];
        }
    }
    return nullptr;
}

void test_collect_items_returns_every_occupied_entry(void)
{
    map_strv_int_entry raw_entries[8] = {0};
    raw_entries[0] = (map_strv_int_entry){.key = strv_from_cstr("key"), .value = 3, .state = MAP_ENTRY_OCCUPIED};
    raw_entries[2] = (map_strv_int_entry){.key = strv_from_cstr("potion"), .value = 1, .state = MAP_ENTRY_OCCUPIED};
    raw_entries[3].state = MAP_ENTRY_DELETED;
    ItemSet items = {.counts = {.entries = raw_entries, .count = 2, .capacity = 8, .alloc = test_heap_alloc}};

    vec_inventory_item_entry result = inventory_screen_collect_items(&items, test_heap_alloc);

    TEST_ASSERT_EQUAL_INT(2, result.count);
    const InventoryItemEntry *key_entry = find_entry(&result, "key");
    TEST_ASSERT_NOT_NULL(key_entry);
    TEST_ASSERT_EQUAL_INT(3, key_entry->count);
    const InventoryItemEntry *potion_entry = find_entry(&result, "potion");
    TEST_ASSERT_NOT_NULL(potion_entry);
    TEST_ASSERT_EQUAL_INT(1, potion_entry->count);

    vec_inventory_item_entry_free(&result);
}

void test_collect_items_empty_item_set_returns_zero_entries(void)
{
    ItemSet items = {0};

    vec_inventory_item_entry result = inventory_screen_collect_items(&items, test_heap_alloc);

    TEST_ASSERT_EQUAL_INT(0, result.count);
    vec_inventory_item_entry_free(&result);
}

/* ---- inventory_screen_grid_nav ------------------------------------------- */

void test_grid_nav_right_advances_within_row(void)
{
    InventoryGridShape shape = {.columns = 4, .item_count = 6};
    TEST_ASSERT_EQUAL_INT(1, inventory_screen_grid_nav(0, shape, INVENTORY_NAV_RIGHT));
}

void test_grid_nav_right_crosses_into_next_row(void)
{
    InventoryGridShape shape = {.columns = 4, .item_count = 6};
    TEST_ASSERT_EQUAL_INT(4, inventory_screen_grid_nav(3, shape, INVENTORY_NAV_RIGHT));
}

void test_grid_nav_right_clamps_at_last_item(void)
{
    InventoryGridShape shape = {.columns = 4, .item_count = 6};
    TEST_ASSERT_EQUAL_INT(5, inventory_screen_grid_nav(5, shape, INVENTORY_NAV_RIGHT));
}

void test_grid_nav_left_retreats_within_row(void)
{
    InventoryGridShape shape = {.columns = 4, .item_count = 6};
    TEST_ASSERT_EQUAL_INT(1, inventory_screen_grid_nav(2, shape, INVENTORY_NAV_LEFT));
}

void test_grid_nav_left_clamps_at_first_item(void)
{
    InventoryGridShape shape = {.columns = 4, .item_count = 6};
    TEST_ASSERT_EQUAL_INT(0, inventory_screen_grid_nav(0, shape, INVENTORY_NAV_LEFT));
}

void test_grid_nav_down_advances_one_row(void)
{
    InventoryGridShape shape = {.columns = 4, .item_count = 6};
    TEST_ASSERT_EQUAL_INT(5, inventory_screen_grid_nav(1, shape, INVENTORY_NAV_DOWN));
}

void test_grid_nav_down_clamps_past_last_row(void)
{
    InventoryGridShape shape = {.columns = 4, .item_count = 6};
    TEST_ASSERT_EQUAL_INT(5, inventory_screen_grid_nav(5, shape, INVENTORY_NAV_DOWN));
}

void test_grid_nav_up_retreats_one_row(void)
{
    InventoryGridShape shape = {.columns = 4, .item_count = 6};
    TEST_ASSERT_EQUAL_INT(1, inventory_screen_grid_nav(5, shape, INVENTORY_NAV_UP));
}

void test_grid_nav_up_clamps_at_top_row(void)
{
    InventoryGridShape shape = {.columns = 4, .item_count = 6};
    TEST_ASSERT_EQUAL_INT(0, inventory_screen_grid_nav(1, shape, INVENTORY_NAV_UP));
}

void test_grid_nav_on_empty_inventory_is_noop(void)
{
    InventoryGridShape shape = {.columns = 4, .item_count = 0};
    TEST_ASSERT_EQUAL_INT(0, inventory_screen_grid_nav(2, shape, INVENTORY_NAV_RIGHT));
}

/* ---- inventory_screen_cell_position --------------------------------------- */

void test_cell_position_first_cell_is_grid_origin(void)
{
    InventoryGridLayout layout = {.columns = 4, .cell_width = 100, .cell_height = 50, .margin = 10};
    InventoryCellPosition position = inventory_screen_cell_position(0, layout);
    TEST_ASSERT_EQUAL_INT(0, position.x);
    TEST_ASSERT_EQUAL_INT(0, position.y);
}

void test_cell_position_second_column_offsets_by_cell_and_margin(void)
{
    InventoryGridLayout layout = {.columns = 4, .cell_width = 100, .cell_height = 50, .margin = 10};
    InventoryCellPosition position = inventory_screen_cell_position(1, layout);
    TEST_ASSERT_EQUAL_INT(110, position.x);
    TEST_ASSERT_EQUAL_INT(0, position.y);
}

void test_cell_position_next_row_offsets_y_only(void)
{
    InventoryGridLayout layout = {.columns = 4, .cell_width = 100, .cell_height = 50, .margin = 10};
    InventoryCellPosition position = inventory_screen_cell_position(4, layout);
    TEST_ASSERT_EQUAL_INT(0, position.x);
    TEST_ASSERT_EQUAL_INT(60, position.y);
}

void test_cell_position_arbitrary_index(void)
{
    InventoryGridLayout layout = {.columns = 3, .cell_width = 80, .cell_height = 40, .margin = 5};
    InventoryCellPosition position = inventory_screen_cell_position(7, layout); /* row 2, column 1 */
    TEST_ASSERT_EQUAL_INT(85, position.x);
    TEST_ASSERT_EQUAL_INT(90, position.y);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_collect_items_returns_every_occupied_entry);
    RUN_TEST(test_collect_items_empty_item_set_returns_zero_entries);
    RUN_TEST(test_grid_nav_right_advances_within_row);
    RUN_TEST(test_grid_nav_right_crosses_into_next_row);
    RUN_TEST(test_grid_nav_right_clamps_at_last_item);
    RUN_TEST(test_grid_nav_left_retreats_within_row);
    RUN_TEST(test_grid_nav_left_clamps_at_first_item);
    RUN_TEST(test_grid_nav_down_advances_one_row);
    RUN_TEST(test_grid_nav_down_clamps_past_last_row);
    RUN_TEST(test_grid_nav_up_retreats_one_row);
    RUN_TEST(test_grid_nav_up_clamps_at_top_row);
    RUN_TEST(test_grid_nav_on_empty_inventory_is_noop);
    RUN_TEST(test_cell_position_first_cell_is_grid_origin);
    RUN_TEST(test_cell_position_second_column_offsets_by_cell_and_margin);
    RUN_TEST(test_cell_position_next_row_offsets_y_only);
    RUN_TEST(test_cell_position_arbitrary_index);
    return UNITY_END();
}
