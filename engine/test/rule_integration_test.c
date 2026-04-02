#include "unity.h"
#include "debug.h"
#include "diag.h"
#include "error.h"

static ErrorState test_err;
static DebugState test_dbg;
static Diag test_diag = {&test_err, &test_dbg};

#include "arena.h"
#include "attribute.h"
#include "entity.h"
#include "game.h"
#include "rule.h"
#include "test_helpers.h"

#include "toml.h"

#include <stdlib.h>
#include <string.h>

/* ---- TOML round-trip parsing tests ---- */

void test_rules_parse_from_toml(void)
{
    static const char *toml_str = "name = \"chest\"\n"
                                  "\n"
                                  "[[rule]]\n"
                                  "trigger = \"interact\"\n"
                                  "conditions = [\"flag:has_key\"]\n"
                                  "actions = [\"set_flag:chest_opened\", \"destroy\"]\n";

    char errbuf[200];
    char *buf = strdup(toml_str);
    toml_table_t *root = toml_parse(buf, errbuf, (int)sizeof(errbuf));
    free(buf);
    TEST_ASSERT_NOT_NULL(root);

    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    Allocator alloc = allocator_arena(&arena);

    vec_rule rules = {0};
    TEST_ASSERT_TRUE(rules_parse(&test_diag, &alloc, &rules, root, &arena));
    TEST_ASSERT_EQUAL_INT(1, rules.count);

    const Rule *rule = &rules.data[0];
    TEST_ASSERT_EQUAL_INT(TRIGGER_INTERACT, rule->trigger.type);
    TEST_ASSERT_EQUAL_INT(1, rule->conditions.count);
    TEST_ASSERT_EQUAL_INT(COND_FLAG, rule->conditions.data[0].type);
    TEST_ASSERT_EQUAL_STRING("has_key", rule->conditions.data[0].argument.ptr);
    TEST_ASSERT_EQUAL_INT(2, rule->action_tree.nodes.count);
    TEST_ASSERT_EQUAL_INT(ACTION_SET_FLAG, rule->action_tree.nodes.data[0].type);
    TEST_ASSERT_EQUAL_STRING("chest_opened", rule->action_tree.nodes.data[0].argument.ptr);
    TEST_ASSERT_EQUAL_INT(ACTION_DESTROY, rule->action_tree.nodes.data[1].type);

    toml_free(root);
    arena_free(&arena);
}

void test_rules_parse_no_rules(void)
{
    static const char *toml_str = "name = \"tree\"\n";

    char errbuf[200];
    char *buf = strdup(toml_str);
    toml_table_t *root = toml_parse(buf, errbuf, (int)sizeof(errbuf));
    free(buf);
    TEST_ASSERT_NOT_NULL(root);

    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    Allocator alloc = allocator_arena(&arena);

    vec_rule rules = {0};
    TEST_ASSERT_TRUE(rules_parse(&test_diag, &alloc, &rules, root, &arena));
    TEST_ASSERT_EQUAL_INT(0, rules.count);

    toml_free(root);
    arena_free(&arena);
}

void test_rules_parse_multiple_rules(void)
{
    static const char *toml_str = "name = \"door\"\n"
                                  "\n"
                                  "[[rule]]\n"
                                  "trigger = \"interact\"\n"
                                  "actions = [\"set_flag:opened\"]\n"
                                  "\n"
                                  "[[rule]]\n"
                                  "trigger = \"event:reset\"\n"
                                  "actions = [\"clear_flag:opened\"]\n";

    char errbuf[200];
    char *buf = strdup(toml_str);
    toml_table_t *root = toml_parse(buf, errbuf, (int)sizeof(errbuf));
    free(buf);
    TEST_ASSERT_NOT_NULL(root);

    Arena arena;
    TEST_ASSERT_TRUE(arena_init(&test_err, &arena));
    Allocator alloc = allocator_arena(&arena);

    vec_rule rules = {0};
    TEST_ASSERT_TRUE(rules_parse(&test_diag, &alloc, &rules, root, &arena));
    TEST_ASSERT_EQUAL_INT(2, rules.count);

    TEST_ASSERT_EQUAL_INT(TRIGGER_INTERACT, rules.data[0].trigger.type);
    TEST_ASSERT_EQUAL_INT(TRIGGER_EVENT, rules.data[1].trigger.type);
    TEST_ASSERT_EQUAL_STRING("reset", rules.data[1].trigger.argument.ptr);

    toml_free(root);
    arena_free(&arena);
}

/* ---- Integration: blueprint with rules parsed from gamedata ---- */

static Texture2D rule_test_dummy_texture;

static Texture2D *rule_test_dummy_lookup(const char *texture_name, void *user_data)
{
    (void)texture_name;
    (void)user_data;
    return &rule_test_dummy_texture;
}

static const char *rule_test_gamedata = "[[blueprint]]\n"
                                        "name = \"player\"\n"
                                        "texture = \"player.png\"\n"
                                        "src = [0, 0, 32, 32]\n"
                                        "collision_offset = [-5, 6]\n"
                                        "collision_size = [10, 10]\n"
                                        "behavior = \"player\"\n"
                                        "speed = 80\n"
                                        "\n"
                                        "[[blueprint]]\n"
                                        "name = \"chest\"\n"
                                        "texture = \"chest.png\"\n"
                                        "src = [0, 0, 16, 16]\n"
                                        "collision_offset = [0, 0]\n"
                                        "collision_size = [16, 16]\n"
                                        "\n"
                                        "[[blueprint.rule]]\n"
                                        "trigger = \"interact\"\n"
                                        "actions = [\"set_flag:chest_opened\"]\n"
                                        "\n"
                                        "[[level]]\n"
                                        "name = \"test\"\n"
                                        "size = [320, 240]\n"
                                        "\n"
                                        "[[level.entity]]\n"
                                        "blueprint = \"player\"\n"
                                        "pos = [160, 120]\n"
                                        "\n"
                                        "[[level.entity]]\n"
                                        "blueprint = \"chest\"\n"
                                        "pos = [165, 120]\n";

void test_integration_interact_rule(void)
{
    GameState state;
    TEST_ASSERT_TRUE(game_init(&test_diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &test_diag, &state,
        (GamedataParams){.toml_string = rule_test_gamedata, .texture_lookup = rule_test_dummy_lookup}));

    TEST_ASSERT_TRUE(state.gamedata_loaded);
    TEST_ASSERT_EQUAL_INT(2, state.current_level.entities.count);

    const Blueprint *chest_bp = blueprint_find(&state.blueprints, "chest");
    TEST_ASSERT_NOT_NULL(chest_bp);
    TEST_ASSERT_EQUAL_INT(1, chest_bp->rules.count);
    TEST_ASSERT_EQUAL_INT(TRIGGER_INTERACT, chest_bp->rules.data[0].trigger.type);

    TEST_ASSERT_FALSE(flag_get(&state.flags, "chest_opened"));

    InputState input = {0};
    input.buttons[0] = true;
    game_update(&test_diag, &state, input, 1.0F / 60.0F);

    TEST_ASSERT_TRUE(flag_get(&state.flags, "chest_opened"));

    game_free(&test_diag, &state);
}

void test_integration_condition_blocks_interact(void)
{
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"player\"\n"
                                  "texture = \"player.png\"\n"
                                  "src = [0, 0, 32, 32]\n"
                                  "collision_offset = [-5, 6]\n"
                                  "collision_size = [10, 10]\n"
                                  "behavior = \"player\"\n"
                                  "speed = 80\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"locked_chest\"\n"
                                  "texture = \"chest.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"interact\"\n"
                                  "conditions = [\"flag:has_key\"]\n"
                                  "actions = [\"set_flag:chest_opened\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"player\"\n"
                                  "pos = [160, 120]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"locked_chest\"\n"
                                  "pos = [165, 120]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&test_diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &test_diag, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    InputState input = {0};
    input.buttons[0] = true;
    game_update(&test_diag, &state, input, 1.0F / 60.0F);

    TEST_ASSERT_FALSE(flag_get(&state.flags, "chest_opened"));

    input.buttons[0] = false;
    game_update(&test_diag, &state, input, 1.0F / 60.0F);

    Allocator arena_alloc = allocator_arena(&state.gamedata_arena);
    flag_set(&test_diag, &arena_alloc, &state.flags, "has_key");
    input.buttons[0] = true;
    game_update(&test_diag, &state, input, 1.0F / 60.0F);

    TEST_ASSERT_TRUE(flag_get(&state.flags, "chest_opened"));

    game_free(&test_diag, &state);
}

/* ---- Integration: for_each control flow ---- */

void test_integration_for_each_no_bind_iterates_all_entities(void)
{
    /* counter blueprint: on_spawn -> for_each entities (no bind) -> add_attr:self.count,1
     * Three entities in the level: counter, target_a, target_b (all count = 0).
     * After load, every entity should have count = 1 (one iteration each). */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"counter\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "count = 0\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [{ for_each = \"entities\", do = [\"add_attr:self.count,1\"] }]\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"target\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "count = 0\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"counter\"\n"
                                  "pos = [10, 10]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"target\"\n"
                                  "pos = [50, 10]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"target\"\n"
                                  "pos = [90, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&test_diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &test_diag, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* All three entities must have count = 1 */
    for (int entity_index = 0; entity_index < state.current_level.entities.count; entity_index++) {
        const Entity *entity = &state.current_level.entities.data[entity_index];
        TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&entity->attrs, nullptr, "count", 0.0F));
    }

    game_free(&test_diag, &state);
}

void test_integration_for_each_condition_filter(void)
{
    /* Only entities with is_enemy attr get hit_count incremented.
     * Fixture: one watcher (has the rule), one enemy, one bystander.
     * After load: enemy.hit_count = 1, bystander.hit_count = 0. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"watcher\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [{ for_each = \"entities\", conditions = [\"attr:is_enemy\"], "
                                  "do = [\"add_attr:self.hit_count,1\"] }]\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"enemy\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "is_enemy = 1\n"
                                  "hit_count = 0\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"bystander\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "hit_count = 0\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"watcher\"\n"
                                  "pos = [10, 10]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"enemy\"\n"
                                  "pos = [50, 10]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"bystander\"\n"
                                  "pos = [90, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&test_diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &test_diag, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    TEST_ASSERT_EQUAL_INT(3, state.current_level.entities.count);
    /* entity 1 = enemy, entity 2 = bystander */
    const Entity *enemy = &state.current_level.entities.data[1];
    const Entity *bystander = &state.current_level.entities.data[2];
    TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&enemy->attrs, nullptr, "hit_count", 0.0F));
    TEST_ASSERT_EQUAL_INT(0, (int)attr_get_scoped_float(&bystander->attrs, nullptr, "hit_count", 0.0F));

    game_free(&test_diag, &state);
}

void test_integration_for_each_bind_mode(void)
{
    /* Bind mode: self stays as rule owner; bound variable names the iterated entity.
     * Fixture: "marker" entity has on_spawn rule, for_each bind="item", add_attr:item.tagged,1.
     * All entities (including marker itself -- no self-exclusion) get tagged = 1. */
    static const char *gamedata =
        "[[blueprint]]\n"
        "name = \"marker\"\n"
        "texture = \"t.png\"\n"
        "src = [0, 0, 16, 16]\n"
        "tagged = 0\n"
        "\n"
        "[[blueprint.rule]]\n"
        "trigger = \"on_spawn\"\n"
        "actions = [{ for_each = \"entities\", bind = \"item\", do = [\"add_attr:item.tagged,1\"] }]\n"
        "\n"
        "[[blueprint]]\n"
        "name = \"thing\"\n"
        "texture = \"t.png\"\n"
        "src = [0, 0, 16, 16]\n"
        "tagged = 0\n"
        "\n"
        "[[level]]\n"
        "name = \"test\"\n"
        "size = [320, 240]\n"
        "\n"
        "[[level.entity]]\n"
        "blueprint = \"marker\"\n"
        "pos = [10, 10]\n"
        "\n"
        "[[level.entity]]\n"
        "blueprint = \"thing\"\n"
        "pos = [50, 10]\n"
        "\n"
        "[[level.entity]]\n"
        "blueprint = \"thing\"\n"
        "pos = [90, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&test_diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &test_diag, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* All three entities must have tagged = 1 (bind mode still includes self) */
    for (int entity_index = 0; entity_index < state.current_level.entities.count; entity_index++) {
        const Entity *entity = &state.current_level.entities.data[entity_index];
        TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&entity->attrs, nullptr, "tagged", 0.0F));
    }

    game_free(&test_diag, &state);
}

/* ---- Integration: subroutines ---- */

void test_integration_subroutine_call(void)
{
    /* A subroutine sets a flag. A blueprint rule calls it.
     * After load (on_spawn), the flag must be set. */
    static const char *gamedata = "[[subroutine]]\n"
                                  "name = \"mark_visited\"\n"
                                  "actions = [\"set_flag:visited\"]\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"beacon\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"call:mark_visited\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"beacon\"\n"
                                  "pos = [10, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&test_diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &test_diag, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    TEST_ASSERT_TRUE(flag_get(&state.flags, "visited"));

    game_free(&test_diag, &state);
}

void test_integration_subroutine_inherits_self(void)
{
    /* Subroutine uses self.attr -- must operate on the calling entity. */
    static const char *gamedata = "[[subroutine]]\n"
                                  "name = \"increment\"\n"
                                  "actions = [\"add_attr:self.count,1\"]\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"counter\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "count = 0\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"call:increment\", \"call:increment\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"counter\"\n"
                                  "pos = [10, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&test_diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &test_diag, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* Called twice -- count must be 2 */
    const Entity *counter = &state.current_level.entities.data[0];
    TEST_ASSERT_EQUAL_INT(2, (int)attr_get_scoped_float(&counter->attrs, nullptr, "count", 0.0F));

    game_free(&test_diag, &state);
}

void test_integration_subroutine_missing_is_soft_fail(void)
{
    /* Calling a non-existent subroutine must not crash -- subsequent actions still run. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"thing\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "count = 0\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"call:nonexistent\", \"add_attr:self.count,1\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"thing\"\n"
                                  "pos = [10, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&test_diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &test_diag, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* count must be 1 -- add_attr ran after the failed call: */
    const Entity *thing = &state.current_level.entities.data[0];
    TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&thing->attrs, nullptr, "count", 0.0F));

    game_free(&test_diag, &state);
}

/* ---- Integration: timers ---- */

void test_integration_timer_oneshot_fires_once(void)
{
    /* Entity creates a one-shot timer on_spawn; after one tick past duration it fires and
     * sets a flag. A second tick must NOT fire again. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"thing\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"create_timer:tick,0.5\"]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"timer:tick\"\n"
                                  "actions = [\"add_attr:self.fired_count,1\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"thing\"\n"
                                  "pos = [10, 10]\n"
                                  "fired_count = 0\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&test_diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &test_diag, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* 1 timer created, none fired yet */
    TEST_ASSERT_EQUAL_INT(1, state.timers.count);
    const Entity *thing = &state.current_level.entities.data[0];
    TEST_ASSERT_EQUAL_INT(0, (int)attr_get_scoped_float(&thing->attrs, nullptr, "fired_count", 0.0F));

    /* Advance past duration -- timer fires once */
    game_update(&test_diag, &state, (InputState){0}, 0.6F);
    TEST_ASSERT_EQUAL_INT(0, state.timers.count);
    TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&thing->attrs, nullptr, "fired_count", 0.0F));

    /* Second tick -- no timer left, count stays at 1 */
    game_update(&test_diag, &state, (InputState){0}, 0.6F);
    TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&thing->attrs, nullptr, "fired_count", 0.0F));

    game_free(&test_diag, &state);
}

void test_integration_timer_periodic_fires_repeatedly(void)
{
    /* Periodic timer fires every 0.5 s -- advance 1.1 s and expect 2 fires. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"thing\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"create_timer_periodic:pulse,0.5\"]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"timer_periodic:pulse\"\n"
                                  "actions = [\"add_attr:self.pulse_count,1\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"thing\"\n"
                                  "pos = [10, 10]\n"
                                  "pulse_count = 0\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&test_diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &test_diag, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* Advance 0.6 s -- one fire */
    game_update(&test_diag, &state, (InputState){0}, 0.6F);
    const Entity *thing = &state.current_level.entities.data[0];
    TEST_ASSERT_EQUAL_INT(1, (int)attr_get_scoped_float(&thing->attrs, nullptr, "pulse_count", 0.0F));

    /* Advance another 0.6 s -- second fire; timer still alive */
    game_update(&test_diag, &state, (InputState){0}, 0.6F);
    TEST_ASSERT_EQUAL_INT(2, (int)attr_get_scoped_float(&thing->attrs, nullptr, "pulse_count", 0.0F));
    TEST_ASSERT_EQUAL_INT(1, state.timers.count);

    game_free(&test_diag, &state);
}

void test_integration_timer_destroy_cancels(void)
{
    /* Entity creates a timer on_spawn and destroys it on a separate event.
     * After the destroy event no fire should occur even past the duration. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"thing\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"create_timer:tick,0.5\"]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"event:cancel\"\n"
                                  "actions = [\"destroy_timer:tick\"]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"timer:tick\"\n"
                                  "actions = [\"add_attr:self.fired_count,1\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"thing\"\n"
                                  "pos = [10, 10]\n"
                                  "fired_count = 0\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&test_diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &test_diag, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    TEST_ASSERT_EQUAL_INT(1, state.timers.count);

    /* Fire cancel event -- timer removed */
    TriggerEvent cancel = {.type = TRIGGER_EVENT, .entity_index = -1};
    Allocator heap_alloc = allocator_heap();
    (void)str_from_cstr(&heap_alloc, &cancel.argument, "cancel");
    int cancel_count = state.current_level.entities.count;
    const AttrSet *cancel_defaults[64];
    for (int index = 0; index < cancel_count; index++) {
        cancel_defaults[index] = entity_resolve_defaults(&state, state.current_level.entities.data[index].id);
    }
    Allocator rule_alloc = allocator_arena(&state.gamedata_arena);
    rules_evaluate_batch(&test_diag, &rule_alloc, state.current_level.entities.data, cancel_count, &cancel, 1,
                         &state.flags, &state.vars, &state.rule_table, &state.subroutines, &state.timers,
                         cancel_defaults, &state.transition);
    str_free(&heap_alloc, &cancel.argument);

    TEST_ASSERT_EQUAL_INT(0, state.timers.count);

    /* Advance past duration -- no fire */
    game_update(&test_diag, &state, (InputState){0}, 0.6F);
    const Entity *thing = &state.current_level.entities.data[0];
    TEST_ASSERT_EQUAL_INT(0, (int)attr_get_scoped_float(&thing->attrs, nullptr, "fired_count", 0.0F));

    game_free(&test_diag, &state);
}

/* ---- Integration: on_destroy trigger ---- */

void test_integration_on_destroy_fires(void)
{
    /* Entity destroys itself on_spawn; on_destroy rule must run while it is inactive. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"thing\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"destroy\"]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_destroy\"\n"
                                  "actions = [\"set_flag:thing_destroyed\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"thing\"\n"
                                  "pos = [10, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&test_diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &test_diag, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* Entity must be inactive and the on_destroy flag must be set */
    const Entity *thing = &state.current_level.entities.data[0];
    TEST_ASSERT_FALSE(attr_get_bool(&thing->attrs, "active", true));
    TEST_ASSERT_TRUE(flag_get(&state.flags, "thing_destroyed"));

    game_free(&test_diag, &state);
}

/* ---- Integration: defeat trigger ---- */

void test_integration_defeat_fires_when_health_drops_to_zero(void)
{
    /* Entity starts with health=5; on_spawn subtracts 10 -> health=-5 (crosses 0 -> defeat). */
    /* health = [current, max] is the blueprint format; parse_health stores it as health=5 */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"enemy\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "health = [5, 10]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"on_spawn\"\n"
                                  "actions = [\"add_attr:self.health,-10\"]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"defeat\"\n"
                                  "actions = [\"set_flag:enemy_defeated\"]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"enemy\"\n"
                                  "pos = [10, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&test_diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &test_diag, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    TEST_ASSERT_TRUE(flag_get(&state.flags, "enemy_defeated"));

    game_free(&test_diag, &state);
}

/* ---- Integration: collide trigger ---- */

void test_integration_collide_fires_on_overlap(void)
{
    /* Two solid entities placed at the same position -- they overlap immediately.
     * After the first game_update both should receive TRIGGER_COLLIDE.
     * The "rock" blueprint has a collide rule that sets a flag. */
    static const char *gamedata = "[[blueprint]]\n"
                                  "name = \"rock\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "collision_size = [16, 16]\n"
                                  "\n"
                                  "[[blueprint.rule]]\n"
                                  "trigger = \"collide\"\n"
                                  "actions = [\"set_flag:rock_hit\"]\n"
                                  "\n"
                                  "[[blueprint]]\n"
                                  "name = \"barrel\"\n"
                                  "texture = \"t.png\"\n"
                                  "src = [0, 0, 16, 16]\n"
                                  "collision_size = [16, 16]\n"
                                  "\n"
                                  "[[level]]\n"
                                  "name = \"test\"\n"
                                  "size = [320, 240]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"rock\"\n"
                                  "pos = [10, 10]\n"
                                  "\n"
                                  "[[level.entity]]\n"
                                  "blueprint = \"barrel\"\n"
                                  "pos = [10, 10]\n";

    GameState state;
    TEST_ASSERT_TRUE(game_init(&test_diag, &state, (RectU32){320, 240}));
    TEST_ASSERT_TRUE(game_load_gamedata(
        &test_diag, &state, (GamedataParams){.toml_string = gamedata, .texture_lookup = rule_test_dummy_lookup}));

    /* No collide event yet -- prev_solid_collisions initialised to false */
    TEST_ASSERT_FALSE(flag_get(&state.flags, "rock_hit"));

    /* First update -- overlap detected for the first time -> fire */
    game_update(&test_diag, &state, (InputState){0}, 0.016F);
    TEST_ASSERT_TRUE(flag_get(&state.flags, "rock_hit"));

    game_free(&test_diag, &state);
}
