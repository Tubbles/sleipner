/* Bug: editing sprite_offset_x / sprite_offset_y on a blueprint in the editor
 * (1) does not update the entity's visual position live in the scene, and
 * (2) does not get emitted to the TOML output / is not saved.
 *
 * Reported for blueprints that do NOT have sprite_offset initially (e.g. "tree").
 *
 * These tests reproduce both symptoms by loading gamedata with a blueprint that
 * has no sprite_offset, modifying the blueprint's sprite_offset attrs, then
 * asserting on the effective draw position and TOML output.
 *
 * The renderer computes draw position as:
 *   draw_pos = entity->position - sprite_offset
 * where sprite_offset should come from the scoped attr lookup (entity attrs →
 * blueprint attrs). If the renderer reads a cached field instead of the live
 * blueprint attrs, editing the blueprint won't be visible. */

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

/* Minimal fixture: a tree blueprint with NO sprite_offset, placed in a level. */
static const char *fixture_tree_no_sprite_offset = "[[blueprint]]\n"
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

/* Bug 1: After setting sprite_offset_x and sprite_offset_y on the tree
 * blueprint, the tree entity's draw position (as the renderer computes it
 * via entity_draw_position) must reflect the change immediately. The user
 * reported that changing the value in the editor does not visibly move the
 * sprite. */
void test_integration_sprite_offset_edit_updates_entity_live(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_tree_no_sprite_offset, .texture_lookup = dummy_lookup}));

    /* Tree entity at (50, 50) with no sprite_offset — draw position
     * should equal the entity position. */
    const Entity *tree = find_tree_entity(&state);
    TEST_ASSERT_NOT_NULL(tree);
    const AttrSet *defaults = entity_resolve_defaults(&state, tree->id);
    Vector2 pos_before = entity_draw_position(tree, tree->position, defaults);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 50.0F, pos_before.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 50.0F, pos_before.y);

    /* Edit the blueprint attrs — this is what the editor does when the
     * user adjusts sprite_offset_x / sprite_offset_y on the tree blueprint.
     * The editor creates new numeric attrs as ATTR_INT (the user adjusts
     * with arrows, which produces int values). */
    Blueprint *tree_bp = find_tree_blueprint(&state);
    TEST_ASSERT_NOT_NULL(tree_bp);

    Allocator alloc = allocator_arena(&state.gamedata_arena);
    TEST_ASSERT_TRUE(attr_set_int(&alloc, &tree_bp->attrs, "sprite_offset_x", 5));
    TEST_ASSERT_TRUE(attr_set_int(&alloc, &tree_bp->attrs, "sprite_offset_y", 10));

    /* The draw position must now be offset: (50 - 5, 50 - 10) = (45, 40).
     * entity_draw_position uses the scoped attr lookup, so the blueprint
     * change is reflected immediately — no propagation needed. */
    Vector2 pos_after = entity_draw_position(tree, tree->position, defaults);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 45.0F, pos_after.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 40.0F, pos_after.y);

    game_free(&diag, &state);
}

/* Bug 2: After setting sprite_offset on a blueprint that did not
 * originally have it, the TOML emitter must include the sprite_offset
 * line. The user reported that the value is not saved.
 *
 * The editor creates new numeric attrs as ATTR_INT (the default when
 * the user adjusts a value with arrows). blueprint_get_sprite_offset
 * calls attr_get_float which only matches ATTR_FLOAT, so an int-typed
 * attr is invisible to the emitter. This test uses attr_set_int to
 * match what the editor actually produces. */
void test_integration_sprite_offset_edit_emitted_to_toml(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_tree_no_sprite_offset, .texture_lookup = dummy_lookup}));

    /* Set sprite_offset on the tree blueprint as int attrs — matching
     * what the editor produces when the user adjusts the value. */
    Blueprint *tree_bp = find_tree_blueprint(&state);
    TEST_ASSERT_NOT_NULL(tree_bp);

    Allocator alloc = allocator_arena(&state.gamedata_arena);
    TEST_ASSERT_TRUE(attr_set_int(&alloc, &tree_bp->attrs, "sprite_offset_x", 5));
    TEST_ASSERT_TRUE(attr_set_int(&alloc, &tree_bp->attrs, "sprite_offset_y", 10));

    /* Emit TOML — sprite_offset = [5, 10] must appear in the tree
     * blueprint section. */
    char output[8192];
    int written = toml_emit_gamedata(&state.error, output, (int)sizeof(output), &state.gamedata.blueprints,
                                     &state.gamedata.subroutines, &state.gamedata.tileset,
                                     &state.gamedata.atlas_regions, &state.gamedata.current_level, 1);
    TEST_ASSERT_TRUE(written > 0);

    /* The emitted TOML must contain the sprite_offset for the tree */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(output, "sprite_offset = [5, 10]"), "sprite_offset not found in emitted TOML");

    game_free(&diag, &state);
}
