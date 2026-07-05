#include "unity.h"

#include "attribute.h"
#include "debug.h"
#include "diag.h"
#include "error.h"
#include "map.h"
#include "progression.h"
#include "rule.h"
#include "save.h"
#include "str.h"
#include "strv.h"
#include "test_helpers.h"

#include "raylib.h"

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
