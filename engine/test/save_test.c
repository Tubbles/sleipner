#include "unity.h"

#include "alloc.h"
#include "attribute.h"
#include "debug.h"
#include "diag.h"
#include "entity.h"
#include "error.h"
#include "game.h"
#include "input.h"
#include "map.h"
#include "progression.h"
#include "rule.h"
#include "save.h"
#include "str.h"
#include "strv.h"
#include "test_helpers.h"

#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static ErrorState test_err;
static DebugState test_dbg;
static Diag test_diag = {&test_err, &test_dbg};

/* Builds a SaveState exercising every field the S6.15c schema covers: a
 * couple of set flags, one var of each typed-value kind (int/float/bool/
 * string), a couple of stacked items, and two levels' worth of
 * level_deltas -- one entity per level carries custom attrs, and one is
 * inactive (a soft-destroyed/"defeated" entity), per the round-trip test's
 * requirements. */
static SaveState build_sample_save(void)
{
    SaveState save = {0};
    save.current_level_name = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&save.current_level_name, "field"));

    flag_set(&test_diag, &test_heap_alloc, &save.progression.flags, "door_open");
    flag_set(&test_diag, &test_heap_alloc, &save.progression.flags, "met_wizard");

    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &save.progression.vars, "gold", 42));
    TEST_ASSERT_TRUE(attr_set_float(&test_heap_alloc, &save.progression.vars, "speed_multiplier", 1.5F));
    TEST_ASSERT_TRUE(attr_set_bool(&test_heap_alloc, &save.progression.vars, "has_key", true));
    TEST_ASSERT_TRUE(attr_set_string(&test_heap_alloc, &save.progression.vars,
                                     (AttrStringPair){.name = "hero_name", .value = "Zelda"}));

    item_give(&test_diag, &test_heap_alloc, &save.progression.items, "sword");
    item_give(&test_diag, &test_heap_alloc, &save.progression.items, "potion");
    item_give(&test_diag, &test_heap_alloc, &save.progression.items, "potion");
    item_give(&test_diag, &test_heap_alloc, &save.progression.items, "potion");

    save.progression.level_deltas = map_strv_level_delta_new(test_heap_alloc);

    /* Map keys must be heap-owned, never a bare view into a string
     * literal -- free_save_state frees every stored key the same way
     * save_deserialize's own (str_from_toml_datum-copied) keys are freed,
     * mirroring progression_capture_level_delta's real key-ownership rule
     * (progression.c). */
    Str field_name = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&field_name, "field"));
    LevelDelta field_delta = {.entities = vec_entity_delta_new(test_heap_alloc)};
    EntityDelta chest = {.id = 5, .position = {120.5F, 80.25F}, .active = true};
    TEST_ASSERT_TRUE(attr_set_bool(&test_heap_alloc, &chest.attrs, "opened", true));
    TEST_ASSERT_TRUE(vec_entity_delta_push(&field_delta.entities, chest));
    EntityDelta destroyed_rock = {.id = 7, .position = {40.0F, 40.0F}, .active = false};
    TEST_ASSERT_TRUE(vec_entity_delta_push(&field_delta.entities, destroyed_rock));
    TEST_ASSERT_TRUE(map_strv_level_delta_set(&save.progression.level_deltas, str_to_strv(field_name), field_delta));

    Str interior_name = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_cstr(&interior_name, "interior"));
    LevelDelta interior_delta = {.entities = vec_entity_delta_new(test_heap_alloc)};
    EntityDelta enemy = {.id = 12, .position = {200.0F, 150.0F}, .active = true};
    TEST_ASSERT_TRUE(attr_set_int(&test_heap_alloc, &enemy.attrs, "health", 3));
    TEST_ASSERT_TRUE(vec_entity_delta_push(&interior_delta.entities, enemy));
    TEST_ASSERT_TRUE(
        map_strv_level_delta_set(&save.progression.level_deltas, str_to_strv(interior_name), interior_delta));

    return save;
}

static const EntityDelta *find_entity_delta(const LevelDelta *delta, int entity_id)
{
    for (int index = 0; index < delta->entities.count; index++) {
        if (delta->entities.data[index].id == entity_id) {
            return &delta->entities.data[index];
        }
    }
    return nullptr;
}

/* Deep-frees a SaveState built (directly or via save_deserialize) against
 * test_heap_alloc, so heap-allocator tests stay ASan/LSan-clean -- mirrors
 * test_flag_set_free/test_attr_set_free's "free against test_heap_alloc"
 * convention, extended to the two container kinds (ItemSet, the
 * level_deltas map) that don't already have a test_helpers.h wrapper. */
static void free_save_state(SaveState *save)
{
    str_free(&save->current_level_name);
    test_flag_set_free(&save->progression.flags);
    test_attr_set_free(&save->progression.vars);
    item_set_free(&test_heap_alloc, &save->progression.items);

    map_strv_level_delta *level_deltas = &save->progression.level_deltas;
    for (int index = 0; index < level_deltas->capacity; index++) {
        if (level_deltas->entries[index].state != MAP_ENTRY_OCCUPIED) {
            continue;
        }
        LevelDelta *delta = &level_deltas->entries[index].value;
        for (int entity_index = 0; entity_index < delta->entities.count; entity_index++) {
            test_attr_set_free(&delta->entities.data[entity_index].attrs);
        }
        vec_entity_delta_free(&delta->entities);
        test_heap_alloc.free_fn(test_heap_alloc.ctx, (void *)level_deltas->entries[index].key.ptr);
    }
    map_strv_level_delta_free(level_deltas);
}

void test_save_round_trip(void)
{
    error_clear(&test_err);
    SaveState original = build_sample_save();

    Str serialized = save_serialize(&original, &test_heap_alloc, &test_err);
    TEST_ASSERT_NOT_NULL(serialized.ptr);

    SaveState restored = {0};
    TEST_ASSERT_TRUE(save_deserialize(serialized.ptr, &test_heap_alloc, &test_err, &restored));

    TEST_ASSERT_EQUAL_STRING("field", restored.current_level_name.ptr);

    TEST_ASSERT_TRUE(flag_get(&restored.progression.flags, "door_open"));
    TEST_ASSERT_TRUE(flag_get(&restored.progression.flags, "met_wizard"));
    TEST_ASSERT_FALSE(flag_get(&restored.progression.flags, "never_set"));

    const Attribute *gold = attr_get(&restored.progression.vars, "gold");
    TEST_ASSERT_NOT_NULL(gold);
    TEST_ASSERT_EQUAL_INT(ATTR_INT, gold->type);
    TEST_ASSERT_EQUAL_INT(42, gold->value.i);

    const Attribute *speed = attr_get(&restored.progression.vars, "speed_multiplier");
    TEST_ASSERT_NOT_NULL(speed);
    TEST_ASSERT_EQUAL_INT(ATTR_FLOAT, speed->type);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 1.5F, speed->value.f);

    const Attribute *has_key = attr_get(&restored.progression.vars, "has_key");
    TEST_ASSERT_NOT_NULL(has_key);
    TEST_ASSERT_EQUAL_INT(ATTR_BOOL, has_key->type);
    TEST_ASSERT_TRUE(has_key->value.b);

    const Attribute *hero_name = attr_get(&restored.progression.vars, "hero_name");
    TEST_ASSERT_NOT_NULL(hero_name);
    TEST_ASSERT_EQUAL_INT(ATTR_STRING, hero_name->type);
    TEST_ASSERT_EQUAL_STRING("Zelda", hero_name->value.str.ptr);

    TEST_ASSERT_EQUAL_INT(1, item_count(&restored.progression.items, "sword"));
    TEST_ASSERT_EQUAL_INT(3, item_count(&restored.progression.items, "potion"));
    TEST_ASSERT_EQUAL_INT(0, item_count(&restored.progression.items, "shield"));

    const LevelDelta *field_delta =
        map_strv_level_delta_get(&restored.progression.level_deltas, strv_from_cstr("field"));
    TEST_ASSERT_NOT_NULL(field_delta);
    TEST_ASSERT_EQUAL_INT(2, field_delta->entities.count);

    const EntityDelta *chest = find_entity_delta(field_delta, 5);
    TEST_ASSERT_NOT_NULL(chest);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 120.5F, chest->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001F, 80.25F, chest->position.y);
    TEST_ASSERT_TRUE(chest->active);
    TEST_ASSERT_TRUE(attr_get_bool(&chest->attrs, "opened", false));

    const EntityDelta *destroyed_rock = find_entity_delta(field_delta, 7);
    TEST_ASSERT_NOT_NULL(destroyed_rock);
    TEST_ASSERT_FALSE(destroyed_rock->active);

    const LevelDelta *interior_delta =
        map_strv_level_delta_get(&restored.progression.level_deltas, strv_from_cstr("interior"));
    TEST_ASSERT_NOT_NULL(interior_delta);
    TEST_ASSERT_EQUAL_INT(1, interior_delta->entities.count);
    const EntityDelta *enemy = find_entity_delta(interior_delta, 12);
    TEST_ASSERT_NOT_NULL(enemy);
    TEST_ASSERT_EQUAL_INT(3, attr_get_int(&enemy->attrs, "health", 0));

    str_free(&serialized);
    free_save_state(&original);
    free_save_state(&restored);
}

void test_save_deserialize_malformed_toml_fails(void)
{
    error_clear(&test_err);
    const char *malformed = "[save]\nversion = 1\ncurrent_level = \"unterminated";
    SaveState result = {0};
    TEST_ASSERT_FALSE(save_deserialize(malformed, &test_heap_alloc, &test_err, &result));
    TEST_ASSERT_NOT_NULL(error_get(&test_err));
}

void test_save_deserialize_missing_save_section_fails(void)
{
    error_clear(&test_err);
    const char *no_save_table = "[not_a_save]\nversion = 1\n";
    SaveState result = {0};
    TEST_ASSERT_FALSE(save_deserialize(no_save_table, &test_heap_alloc, &test_err, &result));
    TEST_ASSERT_NOT_NULL(error_get(&test_err));
}

void test_save_deserialize_minimal_valid_is_empty(void)
{
    error_clear(&test_err);
    const char *minimal = "[save]\nversion = 1\ncurrent_level = \"field\"\n";
    SaveState result = {0};
    TEST_ASSERT_TRUE(save_deserialize(minimal, &test_heap_alloc, &test_err, &result));

    TEST_ASSERT_EQUAL_STRING("field", result.current_level_name.ptr);
    TEST_ASSERT_EQUAL_INT(0, result.progression.flags.names.count);
    TEST_ASSERT_EQUAL_INT(0, result.progression.vars.entries.count);
    TEST_ASSERT_EQUAL_INT(0, result.progression.items.counts.count);
    TEST_ASSERT_EQUAL_INT(0, result.progression.level_deltas.count);

    free_save_state(&result);
}

void test_save_deserialize_unsupported_version_fails(void)
{
    error_clear(&test_err);
    const char *future_save = "[save]\nversion = 999\ncurrent_level = \"field\"\n";
    SaveState result = {0};
    TEST_ASSERT_FALSE(save_deserialize(future_save, &test_heap_alloc, &test_err, &result));
    TEST_ASSERT_NOT_NULL(error_get(&test_err));
}

/* --- S6.15d1, D33: save file I/O + autosave mechanism (black-box, real
 * filesystem) --- */

/* Single-level fixture: item_giver's interact gives "key" and sets flag
 * "met_wizard" (covers both flags and items in one save.progression), and
 * chest's interact sets an INSTANCE attr + soft-destroys itself (mirrors
 * integration_test.c's fixture_entity_delta) -- so one save/load round
 * trip exercises flags, items, and a level_delta entity in one pass. */
static const char *fixture_save_round_trip = "[[blueprint]]\n"
                                             "name = \"player\"\n"
                                             "texture = \"player.png\"\n"
                                             "src = [0, 0, 32, 32]\n"
                                             "collision_offset = [0, 0]\n"
                                             "collision_size = [16, 16]\n"
                                             "behavior = \"player\"\n"
                                             "speed = 80\n"
                                             "\n"
                                             "[[blueprint]]\n"
                                             "name = \"item_giver\"\n"
                                             "texture = \"rock.png\"\n"
                                             "src = [0, 0, 16, 16]\n"
                                             "collision_offset = [0, 0]\n"
                                             "collision_size = [16, 16]\n"
                                             "solid = false\n"
                                             "\n"
                                             "[[blueprint.rule]]\n"
                                             "trigger = \"interact\"\n"
                                             "actions = [\"give_item:key\", \"set_flag:met_wizard\"]\n"
                                             "\n"
                                             "[[blueprint]]\n"
                                             "name = \"chest\"\n"
                                             "texture = \"rock.png\"\n"
                                             "src = [0, 0, 16, 16]\n"
                                             "collision_offset = [0, 0]\n"
                                             "collision_size = [16, 16]\n"
                                             "solid = false\n"
                                             "\n"
                                             "[[blueprint.rule]]\n"
                                             "trigger = \"interact\"\n"
                                             "actions = [\"set_attr:opened,true\", \"set_attr:active,false\"]\n"
                                             "\n"
                                             "[[level]]\n"
                                             "name = \"field\"\n"
                                             "size = [320, 240]\n"
                                             "\n"
                                             "[[level.entity]]\n"
                                             "blueprint = \"player\"\n"
                                             "pos = [50, 50]\n"
                                             "\n"
                                             "[[level.entity]]\n"
                                             "blueprint = \"item_giver\"\n"
                                             "pos = [150, 50]\n"
                                             "\n"
                                             "[[level.entity]]\n"
                                             "blueprint = \"chest\"\n"
                                             "pos = [50, 150]\n";

/* Two-level fixture for the autosave-on-transition test: "door"'s enter
 * trigger sends the player to (120, 90) in "interior", which deliberately
 * does NOT match interior's own authored player spawn (80, 60) -- so if
 * save_load's apply step were a no-op, the restored position would read
 * back as the authored (80, 60) instead of the captured (120, 90),
 * making the assertion a real differentiator rather than a coincidence. */
static const char *fixture_autosave_transition = "[[blueprint]]\n"
                                                 "name = \"player\"\n"
                                                 "texture = \"player.png\"\n"
                                                 "src = [0, 0, 32, 32]\n"
                                                 "collision_offset = [0, 0]\n"
                                                 "collision_size = [16, 16]\n"
                                                 "behavior = \"player\"\n"
                                                 "speed = 80\n"
                                                 "\n"
                                                 "[[blueprint]]\n"
                                                 "name = \"door\"\n"
                                                 "texture = \"rock.png\"\n"
                                                 "src = [0, 0, 16, 16]\n"
                                                 "collision_offset = [0, 0]\n"
                                                 "collision_size = [32, 32]\n"
                                                 "solid = false\n"
                                                 "\n"
                                                 "[[blueprint.rule]]\n"
                                                 "trigger = \"enter\"\n"
                                                 "actions = [\"transition:interior,120,90\"]\n"
                                                 "\n"
                                                 "[[level]]\n"
                                                 "name = \"field\"\n"
                                                 "size = [320, 240]\n"
                                                 "\n"
                                                 "[[level.entity]]\n"
                                                 "blueprint = \"player\"\n"
                                                 "pos = [100, 100]\n"
                                                 "\n"
                                                 "[[level.entity]]\n"
                                                 "blueprint = \"door\"\n"
                                                 "pos = [200, 100]\n"
                                                 "\n"
                                                 "[[level]]\n"
                                                 "name = \"interior\"\n"
                                                 "size = [160, 120]\n"
                                                 "\n"
                                                 "[[level.entity]]\n"
                                                 "blueprint = \"player\"\n"
                                                 "pos = [80, 60]\n";

/* Walk the player toward `destination` until within `range` pixels or
 * `max_iterations` frames elapse. Local copy of integration_test.c's
 * walk_player_to (file-local static there too, no shared header) --
 * mirrors this file's own parse_save_attr precedent of duplicating a
 * small established idiom across files rather than forcing one open. */
static int walk_player_to_target(TestGame *game, float range, Vector2 destination, int max_iterations)
{
    for (int iteration = 0; iteration < max_iterations; iteration++) {
        const Entity *player = game_get_player_const(&game->state);
        float delta_x = destination.x - player->position.x;
        float delta_y = destination.y - player->position.y;
        if ((delta_x * delta_x) + (delta_y * delta_y) <= range * range) {
            return iteration;
        }
        InputState step = {0};
        input_state_set_gp_axis(&step, GAMEPAD_AXIS_LEFT_X, delta_x > 0.0F ? 1.0F : -1.0F);
        input_state_set_gp_axis(&step, GAMEPAD_AXIS_LEFT_Y, delta_y > 0.0F ? 1.0F : -1.0F);
        test_advance_frame(game, step);
    }
    return max_iterations;
}

/* Full serialize -> file -> deserialize -> apply loop: drive real
 * gameplay (give an item, set a flag, open+soft-destroy a chest) via the
 * real interact input, save_write to a temp file, mutate the LIVE state
 * directly (proving what follows isn't a no-op), then save_load from
 * that file and assert every mutated field was overwritten back to what
 * was captured at save time. Verified to fail (mutated values survive)
 * against a save_load whose apply step (the level_loader_fn call, the
 * progression_apply_level_delta call, or the state->progression
 * reassignment) is stubbed to a no-op. */
void test_save_write_then_load_round_trips_via_file(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_save_round_trip));

    Entity *giver = test_find_entity_by_blueprint(&game.state, "item_giver");
    TEST_ASSERT_NOT_NULL(giver);
    (void)walk_player_to_target(&game, 10.0F, giver->position, 300);
    InputState give = {0};
    input_state_press_gp_button(&give, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, give);
    TEST_ASSERT_TRUE(item_has(&game.state.progression.items, "key"));
    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "met_wizard"));

    Entity *chest = test_find_entity_by_blueprint(&game.state, "chest");
    TEST_ASSERT_NOT_NULL(chest);
    (void)walk_player_to_target(&game, 10.0F, chest->position, 300);
    InputState open_chest = {0};
    input_state_press_gp_button(&open_chest, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    test_advance_frame(&game, open_chest);
    chest = test_find_entity_by_blueprint(&game.state, "chest");
    TEST_ASSERT_NOT_NULL(chest);
    TEST_ASSERT_TRUE(attr_get_bool(&chest->attrs, "opened", false));
    TEST_ASSERT_FALSE(attr_get_bool(&chest->attrs, "active", true));

    Vector2 saved_player_position = game_get_player_const(&game.state)->position;

    char save_path[128];
    (void)snprintf(save_path, sizeof(save_path), "/tmp/sleipner_save_test_%d_roundtrip.toml", (int)getpid());

    TEST_ASSERT_TRUE(save_write(&game.diag, &game.state, save_path));

    /* Mutate the live state directly: clear the flag, double the item
     * count, re-close and reactivate the chest, and move the player away. */
    Allocator progression_alloc = allocator_arena(&game.state.progression_arena);
    flag_clear(&progression_alloc, &game.state.progression.flags, "met_wizard");
    item_give(&game.diag, &progression_alloc, &game.state.progression.items, "key");
    TEST_ASSERT_EQUAL_INT(2, item_count(&game.state.progression.items, "key"));

    Allocator gamedata_alloc = allocator_arena(&game.state.gamedata_arena);
    chest = test_find_entity_by_blueprint(&game.state, "chest");
    TEST_ASSERT_NOT_NULL(chest);
    TEST_ASSERT_TRUE(attr_set_bool(&gamedata_alloc, &chest->attrs, "opened", false));
    TEST_ASSERT_TRUE(attr_set_bool(&gamedata_alloc, &chest->attrs, "active", true));

    Entity *player = game_get_player(&game.state);
    TEST_ASSERT_NOT_NULL(player);
    player->position = (Vector2){0.0F, 0.0F};

    TEST_ASSERT_TRUE(save_load(&game.diag, &game.state, save_path, game.frame_ctx.level_loader_fn,
                               game.frame_ctx.level_loader_user_data, &game.undo_history));

    TEST_ASSERT_EQUAL_STRING("field", game.state.gamedata.current_level.name.ptr);
    TEST_ASSERT_TRUE(flag_get(&game.state.progression.flags, "met_wizard"));
    TEST_ASSERT_EQUAL_INT(1, item_count(&game.state.progression.items, "key"));

    chest = test_find_entity_by_blueprint(&game.state, "chest");
    TEST_ASSERT_NOT_NULL(chest);
    TEST_ASSERT_TRUE(attr_get_bool(&chest->attrs, "opened", false));
    TEST_ASSERT_FALSE(attr_get_bool(&chest->attrs, "active", true));

    const Entity *restored_player = game_get_player_const(&game.state);
    TEST_ASSERT_FLOAT_WITHIN(0.5F, saved_player_position.x, restored_player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.5F, saved_player_position.y, restored_player->position.y);

    test_game_teardown(&game);
    (void)remove(save_path);
}

void test_save_load_missing_file_fails(void)
{
    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_save_round_trip));

    char missing_path[128];
    (void)snprintf(missing_path, sizeof(missing_path), "/tmp/sleipner_save_test_%d_missing.toml", (int)getpid());
    (void)remove(missing_path); /* guarantee absence, mirrors preferences_test.c's tmp_path convention */

    error_clear(&game.state.error);
    TEST_ASSERT_FALSE(save_load(&game.diag, &game.state, missing_path, game.frame_ctx.level_loader_fn,
                                game.frame_ctx.level_loader_user_data, &game.undo_history));
    TEST_ASSERT_NOT_NULL(error_get(&game.state.error));

    test_game_teardown(&game);
}

/* Drives a real transition (walking into "door") with frame_ctx.autosave_fn
 * wired to save_autosave, redirecting platform_saves_dir's resolution to a
 * temp directory via XDG_DATA_HOME (the env-var override platform_paths.c
 * reads -- see platform_paths_test.c for the same convention). Asserts the
 * autosave file actually landed on disk, then mutates the live player
 * position and proves save_load restores the position run_transition_swap's
 * autosave hook captured (the door's spawn point, 120,90 -- deliberately
 * different from interior's own authored player position, 80,60, so a
 * broken apply step would be caught rather than coincidentally matching). */
void test_autosave_on_transition_writes_and_round_trips(void)
{
    const char *original_xdg_data_home = getenv("XDG_DATA_HOME");
    char original_xdg_data_home_copy[256] = {0};
    bool had_original_xdg_data_home = original_xdg_data_home != nullptr;
    if (had_original_xdg_data_home) {
        (void)snprintf(original_xdg_data_home_copy, sizeof(original_xdg_data_home_copy), "%s", original_xdg_data_home);
    }

    char xdg_data_home[128];
    (void)snprintf(xdg_data_home, sizeof(xdg_data_home), "/tmp/sleipner_save_test_%d_xdg", (int)getpid());
    TEST_ASSERT_EQUAL_INT(0, setenv("XDG_DATA_HOME", xdg_data_home, 1));

    TestGame game;
    TEST_ASSERT_TRUE(test_game_setup(&game, fixture_autosave_transition));
    game.frame_ctx.autosave_fn = save_autosave;

    InputState right = {0};
    input_state_set_gp_axis(&right, GAMEPAD_AXIS_LEFT_X, 1.0F);
    int max_iterations = 300;
    int iteration = 0;
    while (iteration < max_iterations && strcmp(game.state.gamedata.current_level.name.ptr, "field") == 0) {
        test_advance_frame(&game, right);
        iteration++;
    }
    TEST_ASSERT_TRUE_MESSAGE(iteration < max_iterations, "transition to 'interior' should fire within 300 frames");
    TEST_ASSERT_EQUAL_STRING("interior", game.state.gamedata.current_level.name.ptr);

    char autosave_path[192];
    (void)snprintf(autosave_path, sizeof(autosave_path), "%s/sleipner/saves/autosave.toml", xdg_data_home);
    TEST_ASSERT_TRUE(FileExists(autosave_path));

    Entity *player = game_get_player(&game.state);
    TEST_ASSERT_NOT_NULL(player);
    player->position = (Vector2){0.0F, 0.0F};

    TEST_ASSERT_TRUE(save_load(&game.diag, &game.state, autosave_path, game.frame_ctx.level_loader_fn,
                               game.frame_ctx.level_loader_user_data, &game.undo_history));
    TEST_ASSERT_EQUAL_STRING("interior", game.state.gamedata.current_level.name.ptr);

    const Entity *restored_player = game_get_player_const(&game.state);
    TEST_ASSERT_FLOAT_WITHIN(0.5F, 120.0F, restored_player->position.x);
    TEST_ASSERT_FLOAT_WITHIN(0.5F, 90.0F, restored_player->position.y);

    test_game_teardown(&game);

    if (had_original_xdg_data_home) {
        (void)setenv("XDG_DATA_HOME", original_xdg_data_home_copy, 1);
    } else {
        (void)unsetenv("XDG_DATA_HOME");
    }
    (void)remove(autosave_path);
    char saves_dir[160];
    (void)snprintf(saves_dir, sizeof(saves_dir), "%s/sleipner/saves", xdg_data_home);
    (void)rmdir(saves_dir);
    char sleipner_dir[160];
    (void)snprintf(sleipner_dir, sizeof(sleipner_dir), "%s/sleipner", xdg_data_home);
    (void)rmdir(sleipner_dir);
    (void)rmdir(xdg_data_home);
}
