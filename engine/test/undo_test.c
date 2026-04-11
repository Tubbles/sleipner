#include "unity.h"

#include "arena.h"
#include "attribute.h"
#include "entity.h"
#include "error.h"
#include "game.h"
#include "level.h"
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

    /* Baseline snapshot: value = 42. */
    int *value = alloc_dummy_value(&state, 42);
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base,
                           strv_from_cstr("Baseline"));

    /* Edit 1: value = 99, push. */
    *value = 99;
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base,
                           strv_from_cstr("Edit 1"));

    /* Edit 2: value = 123, push. */
    *value = 123;
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base,
                           strv_from_cstr("Edit 2"));

    /* Undo Edit 2 — moves current to Edit 1, restores 99. */
    undo_history_step_back(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);
    TEST_ASSERT_EQUAL_INT(99, *value);

    /* Undo Edit 1 — moves current to Baseline, restores 42. */
    undo_history_step_back(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);
    TEST_ASSERT_EQUAL_INT(42, *value);

    /* Step back at the left edge — must be a no-op. Mutate live state
     * first and verify step_back does NOT overwrite it with the baseline
     * snapshot (that's the bug: editor undo at the left edge clobbering
     * play-mode runtime state). */
    *value = 77;
    undo_history_step_back(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);
    TEST_ASSERT_EQUAL_INT(77, *value);

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
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base, strv_from_cstr("A"));

    /* Modify to 20 and snapshot B. */
    *value = 20;
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base, strv_from_cstr("B"));

    /* Step back to A. */
    undo_history_step_back(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);
    TEST_ASSERT_EQUAL_INT(10, *value);

    /* Step forward to B. */
    undo_history_step_forward(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);
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
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base, strv_from_cstr("A"));

    /* Snapshot B: value = 2 */
    *value = 2;
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base, strv_from_cstr("B"));

    /* Step back to A. */
    undo_history_step_back(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);

    /* New edit C: value = 3 — should truncate B. */
    *value = 3;
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base, strv_from_cstr("C"));

    /* Redo should be impossible — B was truncated. */
    undo_history_step_forward(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);
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
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base, strv_from_cstr("A"));

    TEST_ASSERT_EQUAL_INT(0, history.current_position);

    undo_history_clear(&history);

    TEST_ASSERT_NULL(history.current);
    TEST_ASSERT_EQUAL_INT(-1, history.current_position);

    /* Step back should be a no-op. */
    undo_history_step_back(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);
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
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base, strv_from_cstr("A"));
    TEST_ASSERT_TRUE(undo_history_is_dirty(&history));

    /* Save clears dirty. */
    undo_history_mark_saved(&history);
    TEST_ASSERT_FALSE(undo_history_is_dirty(&history));

    /* Another edit makes it dirty again (mutate the existing allocation in-place). */
    *value = 99;
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base, strv_from_cstr("B"));
    TEST_ASSERT_TRUE(undo_history_is_dirty(&history));

    /* Undo back to saved position clears dirty. */
    undo_history_step_back(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);
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
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base,
                           strv_from_cstr("Move entity"));
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
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base, strv_from_cstr("A"));
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
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base, strv_from_cstr("A"));

    /* Snapshot B. */
    *value = 20;
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base, strv_from_cstr("B"));

    /* Discard B — cursor should be at A. */
    undo_history_discard(&history);
    TEST_ASSERT_EQUAL_INT(0, history.current_position);
    TEST_ASSERT_TRUE(strv_eq_cstr(undo_history_description(&history), "A"));

    /* Step back from A should be a no-op (A is the first entry). */
    undo_history_step_back(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);
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
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base, strv_from_cstr("A"));

    *value = 2;
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base, strv_from_cstr("B"));
    undo_history_mark_saved(&history);
    TEST_ASSERT_FALSE(undo_history_is_dirty(&history));

    /* Undo to A. */
    undo_history_step_back(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);

    /* New edit C — truncates B (where we saved). */
    *value = 3;
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base, strv_from_cstr("C"));

    /* Should be permanently dirty — saved state is gone. */
    TEST_ASSERT_TRUE(undo_history_is_dirty(&history));

    /* Even undoing back to A shouldn't clear dirty. */
    undo_history_step_back(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);
    TEST_ASSERT_TRUE(undo_history_is_dirty(&history));

    undo_history_free(&history);
    teardown_state(&state);
}

/* --- Integration tests: undo with real gamedata --- */

static const char *fixture_undo = "[[blueprint]]\n"
                                  "name = \"player\"\n"
                                  "texture = \"player.png\"\n"
                                  "src = [0, 0, 32, 32]\n"
                                  "collision_offset = [0, 0]\n"
                                  "collision_size = [16, 16]\n"
                                  "behavior = \"player\"\n"
                                  "speed = 80\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"rock\"\n"
                                  "texture = \"rock.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "collision_offset = [0, 0]\n"
                                  "collision_size = [16, 16]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"field\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"player\"\n"
                                  "pos = [160, 120]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"rock\"\n"
                                  "pos = [200, 120]\n";

static Texture2D dummy_texture;

static Texture2D *dummy_lookup(const char *texture_name, void *user_data)
{
    (void)texture_name;
    (void)user_data;
    return &dummy_texture;
}

void test_undo_entity_spawn_and_undo(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(&diag, &state,
                                        (GamedataParams){.toml_string = fixture_undo, .texture_lookup = dummy_lookup}));
    TEST_ASSERT_EQUAL_INT(2, state.gamedata.current_level.entities.count);

    UndoHistory history = {0};
    TEST_ASSERT_TRUE(undo_history_init(&state.error, &history));

    /* Baseline snapshot (push-after model: entries capture completed states). */
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base,
                           strv_from_cstr("Initial"));

    /* Spawn a rock at (50, 50), then snapshot the post-spawn state. */
    const Blueprint *rock = blueprint_find(&state.gamedata.blueprints, "rock");
    TEST_ASSERT_NOT_NULL(rock);
    Allocator alloc = allocator_arena(&state.gamedata_arena);
    TEST_ASSERT_TRUE(level_spawn_entity(&diag, &state.gamedata.current_level, rock, (Vector2){50, 50},
                                        &state.gamedata.blueprints, dummy_lookup, nullptr, &alloc));
    TEST_ASSERT_EQUAL_INT(3, state.gamedata.current_level.entities.count);
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base,
                           strv_from_cstr("Place entity"));

    /* Undo spawn — should restore to 2 entities (baseline). */
    undo_history_step_back(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);
    TEST_ASSERT_EQUAL_INT(2, state.gamedata.current_level.entities.count);

    /* Redo — should restore to 3 entities. */
    undo_history_step_forward(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);
    TEST_ASSERT_EQUAL_INT(3, state.gamedata.current_level.entities.count);

    undo_history_free(&history);
    game_free(&diag, &state);
}

void test_undo_entity_move_and_undo(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(&diag, &state,
                                        (GamedataParams){.toml_string = fixture_undo, .texture_lookup = dummy_lookup}));

    UndoHistory history = {0};
    TEST_ASSERT_TRUE(undo_history_init(&state.error, &history));

    /* Baseline. */
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 200.0F, state.gamedata.current_level.entities.data[1].position.x);
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base,
                           strv_from_cstr("Initial"));

    /* Move rock to (50, 50), then snapshot post-move state. */
    state.gamedata.current_level.entities.data[1].position = (Vector2){50, 50};
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 50.0F, state.gamedata.current_level.entities.data[1].position.x);
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base,
                           strv_from_cstr("Move entity"));

    /* Undo — should restore original position (baseline). */
    undo_history_step_back(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 200.0F, state.gamedata.current_level.entities.data[1].position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 120.0F, state.gamedata.current_level.entities.data[1].position.y);

    /* Redo — should restore moved position. */
    undo_history_step_forward(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 50.0F, state.gamedata.current_level.entities.data[1].position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 50.0F, state.gamedata.current_level.entities.data[1].position.y);

    undo_history_free(&history);
    game_free(&diag, &state);
}

void test_undo_attribute_change_and_undo(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(&diag, &state,
                                        (GamedataParams){.toml_string = fixture_undo, .texture_lookup = dummy_lookup}));

    UndoHistory history = {0};
    TEST_ASSERT_TRUE(undo_history_init(&state.error, &history));

    /* Baseline. */
    Entity *player = &state.gamedata.current_level.entities.data[0];
    const AttrSet *defaults = entity_resolve_defaults(&state, player->id);
    float original_speed = attr_get_scoped_float(&player->attrs, defaults, "speed", 0.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 80.0F, original_speed);
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base,
                           strv_from_cstr("Initial"));

    /* Override speed, then snapshot post-edit state. */
    Allocator alloc = allocator_arena(&state.gamedata_arena);
    TEST_ASSERT_TRUE(attr_set_float(&alloc, &player->attrs, "speed", 200.0F));
    float modified_speed = attr_get_scoped_float(&player->attrs, defaults, "speed", 0.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 200.0F, modified_speed);
    undo_history_new_entry(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base,
                           strv_from_cstr("Edit attribute"));

    /* Undo — should restore original state (baseline, no instance override). */
    undo_history_step_back(&history, &state.gamedata, &state.gamedata_arena, state.gamedata_base);
    player = &state.gamedata.current_level.entities.data[0];
    defaults = entity_resolve_defaults(&state, player->id);
    float restored_speed = attr_get_scoped_float(&player->attrs, defaults, "speed", 0.0F);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 80.0F, restored_speed);

    undo_history_free(&history);
    game_free(&diag, &state);
}
