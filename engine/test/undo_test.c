#include "unity.h"

#include "arena.h"
#include "error.h"
#include "game.h"
#include "strv.h"
#include "undo.h"

/* Helper: set up a minimal GameState with initialized arenas and a gamedata_base checkpoint.
 * The caller must call teardown_state() when done. */
static void setup_state(GameState *state)
{
    *state = (GameState){0};
    ErrorState err = {0};
    TEST_ASSERT_TRUE(arena_init(&err, &state->gamedata_arena));
    TEST_ASSERT_TRUE(arena_init(&err, &state->scratch_arena));
    state->gamedata.player_index = -1;

    /* Simulate asset loading having completed — set the gamedata_base checkpoint. */
    state->gamedata_base = arena_save(&state->gamedata_arena);
}

static void teardown_state(GameState *state)
{
    arena_free(&state->gamedata_arena);
    arena_free(&state->scratch_arena);
}

/* Allocate a dummy value in the gamedata arena to simulate parsed data.
 * Returns a pointer to the allocated int. */
static int *alloc_dummy_value(GameState *state, int value)
{
    int *slot = arena_alloc(&state->gamedata_arena, sizeof(int));
    *slot = value;
    return slot;
}

void test_undo_new_entry_and_step_back(void)
{
    GameState state = {0};
    setup_state(&state);
    UndoHistory history = {0};
    ErrorState err = {0};
    TEST_ASSERT_TRUE(undo_history_init(&err, &history));

    /* Allocate a value and take a snapshot. */
    int *value = alloc_dummy_value(&state, 42);
    undo_history_new_entry(&history, &state, strv_from_cstr("Set value"));

    /* Modify the value after the snapshot. */
    *value = 99;
    TEST_ASSERT_EQUAL_INT(99, *value);

    /* Step back — should restore the value. */
    undo_history_step_back(&history, &state);

    /* The arena contents were restored — the pointer still points at the same arena address. */
    TEST_ASSERT_EQUAL_INT(42, *value);

    undo_history_free(&history);
    teardown_state(&state);
}

void test_undo_step_forward(void)
{
    GameState state = {0};
    setup_state(&state);
    UndoHistory history = {0};
    ErrorState err = {0};
    TEST_ASSERT_TRUE(undo_history_init(&err, &history));

    /* Snapshot A: value = 10 */
    int *value = alloc_dummy_value(&state, 10);
    undo_history_new_entry(&history, &state, strv_from_cstr("A"));

    /* Modify to 20 and snapshot B. */
    *value = 20;
    undo_history_new_entry(&history, &state, strv_from_cstr("B"));

    /* Step back to A. */
    undo_history_step_back(&history, &state);
    TEST_ASSERT_EQUAL_INT(10, *value);

    /* Step forward to B. */
    undo_history_step_forward(&history, &state);
    TEST_ASSERT_EQUAL_INT(20, *value);

    undo_history_free(&history);
    teardown_state(&state);
}

void test_undo_truncate_on_new_edit(void)
{
    GameState state = {0};
    setup_state(&state);
    UndoHistory history = {0};
    ErrorState err = {0};
    TEST_ASSERT_TRUE(undo_history_init(&err, &history));

    /* Snapshot A: value = 1 */
    int *value = alloc_dummy_value(&state, 1);
    undo_history_new_entry(&history, &state, strv_from_cstr("A"));

    /* Snapshot B: value = 2 */
    *value = 2;
    undo_history_new_entry(&history, &state, strv_from_cstr("B"));

    /* Step back to A. */
    undo_history_step_back(&history, &state);

    /* New edit C: value = 3 — should truncate B. */
    *value = 3;
    undo_history_new_entry(&history, &state, strv_from_cstr("C"));

    /* Redo should be impossible — B was truncated. */
    undo_history_step_forward(&history, &state);
    TEST_ASSERT_EQUAL_INT(3, *value); /* Still C, not B */

    undo_history_free(&history);
    teardown_state(&state);
}

void test_undo_clear(void)
{
    GameState state = {0};
    setup_state(&state);
    UndoHistory history = {0};
    ErrorState err = {0};
    TEST_ASSERT_TRUE(undo_history_init(&err, &history));

    alloc_dummy_value(&state, 42);
    undo_history_new_entry(&history, &state, strv_from_cstr("A"));

    TEST_ASSERT_EQUAL_INT(0, history.current_position);

    undo_history_clear(&history);

    TEST_ASSERT_NULL(history.current);
    TEST_ASSERT_EQUAL_INT(-1, history.current_position);

    /* Step back should be a no-op. */
    undo_history_step_back(&history, &state);
    TEST_ASSERT_NULL(history.current);

    undo_history_free(&history);
    teardown_state(&state);
}

void test_undo_dirty_tracking(void)
{
    GameState state = {0};
    setup_state(&state);
    UndoHistory history = {0};
    ErrorState err = {0};
    TEST_ASSERT_TRUE(undo_history_init(&err, &history));

    /* Initially not dirty (position -1 == saved -1). */
    TEST_ASSERT_FALSE(undo_history_is_dirty(&history));

    /* Edit makes it dirty. */
    int *value = alloc_dummy_value(&state, 42);
    undo_history_new_entry(&history, &state, strv_from_cstr("A"));
    TEST_ASSERT_TRUE(undo_history_is_dirty(&history));

    /* Save clears dirty. */
    undo_history_mark_saved(&history);
    TEST_ASSERT_FALSE(undo_history_is_dirty(&history));

    /* Another edit makes it dirty again (mutate the existing allocation in-place). */
    *value = 99;
    undo_history_new_entry(&history, &state, strv_from_cstr("B"));
    TEST_ASSERT_TRUE(undo_history_is_dirty(&history));

    /* Undo back to saved position clears dirty. */
    undo_history_step_back(&history, &state);
    TEST_ASSERT_FALSE(undo_history_is_dirty(&history));

    undo_history_free(&history);
    teardown_state(&state);
}

void test_undo_description(void)
{
    GameState state = {0};
    setup_state(&state);
    UndoHistory history = {0};
    ErrorState err = {0};
    TEST_ASSERT_TRUE(undo_history_init(&err, &history));

    /* No entry — empty description. */
    Strv empty = undo_history_description(&history);
    TEST_ASSERT_EQUAL_INT(0, (int)empty.len);

    /* After push — description matches. */
    alloc_dummy_value(&state, 42);
    undo_history_new_entry(&history, &state, strv_from_cstr("Move entity"));
    Strv desc = undo_history_description(&history);
    TEST_ASSERT_TRUE(strv_eq_cstr(desc, "Move entity"));

    undo_history_free(&history);
    teardown_state(&state);
}

void test_undo_discard(void)
{
    GameState state = {0};
    setup_state(&state);
    UndoHistory history = {0};
    ErrorState err = {0};
    TEST_ASSERT_TRUE(undo_history_init(&err, &history));

    alloc_dummy_value(&state, 42);
    undo_history_new_entry(&history, &state, strv_from_cstr("A"));
    TEST_ASSERT_EQUAL_INT(0, history.current_position);

    /* Discard removes the entry. */
    undo_history_discard(&history);
    TEST_ASSERT_NULL(history.current);
    TEST_ASSERT_EQUAL_INT(-1, history.current_position);

    /* Discard on empty is a no-op. */
    undo_history_discard(&history);
    TEST_ASSERT_NULL(history.current);

    undo_history_free(&history);
    teardown_state(&state);
}

void test_undo_discard_preserves_previous(void)
{
    GameState state = {0};
    setup_state(&state);
    UndoHistory history = {0};
    ErrorState err = {0};
    TEST_ASSERT_TRUE(undo_history_init(&err, &history));

    /* Snapshot A. */
    int *value = alloc_dummy_value(&state, 10);
    undo_history_new_entry(&history, &state, strv_from_cstr("A"));

    /* Snapshot B. */
    *value = 20;
    undo_history_new_entry(&history, &state, strv_from_cstr("B"));

    /* Discard B — cursor should be at A. */
    undo_history_discard(&history);
    TEST_ASSERT_EQUAL_INT(0, history.current_position);
    TEST_ASSERT_TRUE(strv_eq_cstr(undo_history_description(&history), "A"));

    /* Step back from A should be a no-op (A is the first entry). */
    undo_history_step_back(&history, &state);
    TEST_ASSERT_EQUAL_INT(0, history.current_position);

    undo_history_free(&history);
    teardown_state(&state);
}

void test_undo_dirty_invalidated_by_truncation(void)
{
    GameState state = {0};
    setup_state(&state);
    UndoHistory history = {0};
    ErrorState err = {0};
    TEST_ASSERT_TRUE(undo_history_init(&err, &history));

    /* A → B → save at B → undo to A → new edit C.
     * B is truncated, saved_position should be invalidated. */
    int *value = alloc_dummy_value(&state, 1);
    undo_history_new_entry(&history, &state, strv_from_cstr("A"));

    *value = 2;
    undo_history_new_entry(&history, &state, strv_from_cstr("B"));
    undo_history_mark_saved(&history);
    TEST_ASSERT_FALSE(undo_history_is_dirty(&history));

    /* Undo to A. */
    undo_history_step_back(&history, &state);

    /* New edit C — truncates B (where we saved). */
    *value = 3;
    undo_history_new_entry(&history, &state, strv_from_cstr("C"));

    /* Should be permanently dirty — saved state is gone. */
    TEST_ASSERT_TRUE(undo_history_is_dirty(&history));

    /* Even undoing back to A shouldn't clear dirty. */
    undo_history_step_back(&history, &state);
    TEST_ASSERT_TRUE(undo_history_is_dirty(&history));

    undo_history_free(&history);
    teardown_state(&state);
}
