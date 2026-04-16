/* Bug: editing collision_offset_x / collision_offset_y on a blueprint in the
 * editor (1) does not update the entity's collision rect live in the scene,
 * and (2) does not get emitted to the TOML output / is not saved.
 *
 * Reported for blueprints that DO have collision_offset initially (e.g. "tree").
 *
 * Root cause for (1): entity_update_collision read cached fields on Entity
 * instead of the scoped attr lookup chain. Fix: remove all cached collision
 * fields, compute via entity_collision_rect using scoped attrs.
 *
 * Root cause for (2): the editor creates new numeric attrs as ATTR_INT, but
 * blueprint_get_collision_offset called attr_get_float which only matched
 * ATTR_FLOAT. Already fixed by the attr_get_float INT->float conversion
 * in the sprite_offset commit. */

#include "unity.h"
#include "alloc.h"
#include "attribute.h"
#include "blueprint.h"
#include "diag.h"
#include "entity.h"
#include "game.h"
#include "toml_emitter.h"

#include "raylib.h"

#include <string.h>

/* Minimal fixture: a tree blueprint WITH collision_offset/size, placed in a level. */
static const char *fixture_tree_with_collision = "[[blueprint]]\n"
                                                 "name = \"player\"\n"
                                                 "texture = \"player.png\"\n"
                                                 "src = [0, 0, 32, 32]\n"
                                                 "collision_offset = [0, 0]\n"
                                                 "collision_size = [16, 16]\n"
                                                 "behavior = \"player\"\n"
                                                 "speed = 80\n"
                                                 "\n"
                                                 "[[blueprint]]\n"
                                                 "name = \"tree\"\n"
                                                 "texture = \"tree.png\"\n"
                                                 "src = [0, 0, 32, 48]\n"
                                                 "collision_offset = [8, 32]\n"
                                                 "collision_size = [16, 12]\n"
                                                 "solid = true\n"
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
                                                 "blueprint = \"tree\"\n"
                                                 "pos = [50, 50]\n";

static Texture2D dummy_texture;

static Texture2D *dummy_lookup(const char *texture_name, void *user_data)
{
    (void)texture_name;
    (void)user_data;
    return &dummy_texture;
}

/* Find the tree entity in the level. */
static const Entity *find_tree_entity(const GameState *state)
{
    for (int index = 0; index < state->gamedata.current_level.entities.count; index++) {
        const Entity *entity = &state->gamedata.current_level.entities.data[index];
        if (strcmp(entity->blueprint_name.ptr, "tree") == 0) {
            return entity;
        }
    }
    return nullptr;
}

/* Find the tree blueprint by iterating the blueprint table. */
static Blueprint *find_tree_blueprint(GameState *state)
{
    for (int index = 0; index < state->gamedata.blueprints.entries.count; index++) {
        Blueprint *blueprint = &state->gamedata.blueprints.entries.data[index];
        const char *name = attr_get_string(&blueprint->attrs, "name");
        if (name && strcmp(name, "tree") == 0) {
            return blueprint;
        }
    }
    return nullptr;
}

/* Bug 1: After setting collision_offset_x and collision_offset_y on the tree
 * blueprint, the tree entity's collision rect (as the engine computes it
 * via entity_collision_rect) must reflect the change immediately. */
void test_integration_collision_offset_edit_updates_entity_live(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_tree_with_collision, .texture_lookup = dummy_lookup}));

    /* Tree entity at (50, 50) with collision_offset (8, 32) and size (16, 12).
     * Collision rect should be (58, 82, 16, 12). */
    const Entity *tree = find_tree_entity(&state);
    TEST_ASSERT_NOT_NULL(tree);
    const AttrSet *defaults = entity_resolve_defaults(&state, tree->id);
    Rectangle col_before = entity_collision_rect(tree, defaults);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 58.0F, col_before.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 82.0F, col_before.y);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 16.0F, col_before.width);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 12.0F, col_before.height);

    /* Edit the blueprint attrs — simulating what the editor does. */
    Blueprint *tree_bp = find_tree_blueprint(&state);
    TEST_ASSERT_NOT_NULL(tree_bp);

    Allocator alloc = allocator_arena(&state.gamedata_arena);
    TEST_ASSERT_TRUE(attr_set_int(&alloc, &tree_bp->attrs, "collision_offset_x", 2));
    TEST_ASSERT_TRUE(attr_set_int(&alloc, &tree_bp->attrs, "collision_offset_y", 5));

    /* Collision rect must now reflect (50+2, 50+5, 16, 12) = (52, 55, 16, 12). */
    Rectangle col_after = entity_collision_rect(tree, defaults);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 52.0F, col_after.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 55.0F, col_after.y);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 16.0F, col_after.width);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 12.0F, col_after.height);

    game_free(&diag, &state);
}

/* Bug 2: After setting collision_offset/size on a blueprint, the TOML emitter
 * must include the collision_offset and collision_size lines. The editor
 * creates new numeric attrs as ATTR_INT. */
void test_integration_collision_offset_edit_emitted_to_toml(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_tree_with_collision, .texture_lookup = dummy_lookup}));

    /* Set collision_offset on the tree blueprint as int attrs — matching
     * what the editor produces when the user adjusts the value. */
    Blueprint *tree_bp = find_tree_blueprint(&state);
    TEST_ASSERT_NOT_NULL(tree_bp);

    Allocator alloc = allocator_arena(&state.gamedata_arena);
    TEST_ASSERT_TRUE(attr_set_int(&alloc, &tree_bp->attrs, "collision_offset_x", 3));
    TEST_ASSERT_TRUE(attr_set_int(&alloc, &tree_bp->attrs, "collision_offset_y", 7));

    /* Emit TOML — collision_offset = [3, 7] must appear in the tree
     * blueprint section. */
    char output[8192];
    int written = toml_emit_gamedata(&state.error, output, (int)sizeof(output), &state.gamedata.blueprints,
                                     &state.gamedata.current_level, 1);
    TEST_ASSERT_TRUE(written > 0);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(output, "collision_offset = [3, 7]"),
                                 "collision_offset not found in emitted TOML");

    game_free(&diag, &state);
}
