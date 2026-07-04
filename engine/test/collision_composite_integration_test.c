/* S4.5/D28: a blueprint with an authored [[blueprint.collision]] composite
 * must have that composite deep-copied into the instantiated entity's
 * collision_region, so entity_collision_region returns the authored shape
 * instead of the one-rect fallback derived from collision_offset/size. A
 * blueprint with no [[blueprint.collision]] must keep the pre-existing
 * one-rect behavior unchanged. */

#include "unity.h"
#include "attribute.h"
#include "collision.h"
#include "diag.h"
#include "entity.h"
#include "game.h"

#include "raylib.h"

#include <string.h>

/* "player" has no composite (one-rect fallback still applies). "boulder"
 * authors a two-primitive composite (rect + circle, non-zero offsets) that
 * is much narrower than the collision_offset/collision_size bounding box it
 * still declares — collision_offset=[0,0]/collision_size=[32,32] alone would
 * fall back to a 32x32 box. */
static const char *fixture_composite_collision = "[[blueprint]]\n"
                                                 "name = \"player\"\n"
                                                 "texture = \"player.png\"\n"
                                                 "src = [0, 0, 32, 32]\n"
                                                 "collision_offset = [0, 0]\n"
                                                 "collision_size = [16, 16]\n"
                                                 "behavior = \"player\"\n"
                                                 "speed = 80\n"
                                                 "\n"
                                                 "[[blueprint]]\n"
                                                 "name = \"boulder\"\n"
                                                 "texture = \"boulder.png\"\n"
                                                 "src = [0, 0, 32, 32]\n"
                                                 "collision_offset = [0, 0]\n"
                                                 "collision_size = [32, 32]\n"
                                                 "\n"
                                                 "[[blueprint.collision]]\n"
                                                 "kind = \"rect\"\n"
                                                 "offset = [-8, 0]\n"
                                                 "size = [16, 24]\n"
                                                 "\n"
                                                 "[[blueprint.collision]]\n"
                                                 "kind = \"circle\"\n"
                                                 "offset = [8, -4]\n"
                                                 "radius = 10\n"
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
                                                 "blueprint = \"boulder\"\n"
                                                 "pos = [50, 50]\n";

static Texture2D dummy_texture;

static Texture2D *dummy_lookup(const char *texture_name, void *user_data)
{
    (void)texture_name;
    (void)user_data;
    return &dummy_texture;
}

static const Entity *find_entity_by_blueprint(const GameState *state, const char *blueprint_name)
{
    for (int index = 0; index < state->gamedata.current_level.entities.count; index++) {
        const Entity *entity = &state->gamedata.current_level.entities.data[index];
        if (strcmp(entity->blueprint_name.ptr, blueprint_name) == 0) {
            return entity;
        }
    }
    return nullptr;
}

/* The instantiated boulder entity must carry the authored two-primitive
 * composite (rect then circle, in authoring order), not the one-rect
 * fallback the collision_offset/collision_size attrs alone would produce. */
void test_integration_composite_collision_entity_uses_authored_shape(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_composite_collision, .texture_lookup = dummy_lookup}));

    const Entity *boulder = find_entity_by_blueprint(&state, "boulder");
    TEST_ASSERT_NOT_NULL(boulder);
    const AttrSet *defaults = entity_resolve_defaults(&state, boulder->id);

    CollisionPrimitive prim_storage;
    CollisionShape region = entity_collision_region(boulder, defaults, &prim_storage);

    TEST_ASSERT_EQUAL_INT(2, region.prims.count);
    TEST_ASSERT_EQUAL_INT(COLLIDER_RECT, region.prims.data[0].kind);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, -8.0F, region.prims.data[0].offset.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 0.0F, region.prims.data[0].offset.y);
    TEST_ASSERT_EQUAL_INT(COLLIDER_CIRCLE, region.prims.data[1].kind);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 8.0F, region.prims.data[1].offset.x);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, -4.0F, region.prims.data[1].offset.y);
    TEST_ASSERT_FLOAT_WITHIN(0.1F, 10.0F, region.prims.data[1].circle.radius);

    /* Regression guard: a blueprint with no [[blueprint.collision]] (player)
     * must still get the pre-existing one-rect fallback, not an empty or
     * composite region. */
    const Entity *player = find_entity_by_blueprint(&state, "player");
    TEST_ASSERT_NOT_NULL(player);
    const AttrSet *player_defaults = entity_resolve_defaults(&state, player->id);
    CollisionPrimitive player_prim_storage;
    CollisionShape player_region = entity_collision_region(player, player_defaults, &player_prim_storage);
    TEST_ASSERT_EQUAL_INT(1, player_region.prims.count);
    TEST_ASSERT_EQUAL_INT(COLLIDER_RECT, player_region.prims.data[0].kind);

    game_free(&diag, &state);
}

/* The authored composite is narrower than the collision_offset/collision_size
 * bounding box the blueprint still declares (32x32). A probe point inside
 * that 32x32 box but outside both authored primitives must NOT overlap the
 * entity's actual collision_region, even though it WOULD overlap the
 * one-rect shape the same attrs would produce without the composite —
 * demonstrating collision resolution actually follows the composite. */
void test_integration_composite_collision_narrower_than_one_rect_fallback(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &diag, &state, (GamedataParams){.toml_string = fixture_composite_collision, .texture_lookup = dummy_lookup}));

    const Entity *boulder = find_entity_by_blueprint(&state, "boulder");
    TEST_ASSERT_NOT_NULL(boulder);
    const AttrSet *defaults = entity_resolve_defaults(&state, boulder->id);

    CollisionPrimitive boulder_prim_storage;
    CollisionShape boulder_shape = entity_collision_region(boulder, defaults, &boulder_prim_storage);
    TEST_ASSERT_EQUAL_INT(2, boulder_shape.prims.count);

    CollisionPrimitive probe_prim = {.kind = COLLIDER_CIRCLE, .circle = {.radius = 0.5F}};
    CollisionShape probe_shape = {.prims = {.data = &probe_prim, .count = 1, .capacity = 1}};
    Vector2 probe_position = {boulder->position.x + 14.0F, boulder->position.y + 14.0F};

    TEST_ASSERT_FALSE(composite_overlap(&probe_shape, probe_position, 0.0F, &boulder_shape, boulder->position, 0.0F));

    /* Same probe against the one-rect shape collision_offset=[0,0]/
     * collision_size=[32,32] would produce absent the composite (see
     * entity.c entity_collision_rect_prim: offset = collision_offset +
     * size/2, half extents = size/2). */
    CollisionPrimitive fallback_prim = {
        .kind = COLLIDER_RECT,
        .offset = {16.0F, 16.0F},
        .rect = {.half_w = 16.0F, .half_h = 16.0F},
    };
    CollisionShape fallback_shape = {.prims = {.data = &fallback_prim, .count = 1, .capacity = 1}};
    TEST_ASSERT_TRUE(composite_overlap(&probe_shape, probe_position, 0.0F, &fallback_shape, boulder->position, 0.0F));

    game_free(&diag, &state);
}
