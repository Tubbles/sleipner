#include "test_helpers.h"

void test_blueprint_table_free(struct EngineContext *ctx, BlueprintTable *table)
{
    for (int index = 0; index < table->entries.count; index++) {
        test_blueprint_free(ctx, &table->entries.data[index]);
    }
    vec_blueprint_free(&table->entries);
}

void test_blueprint_free(struct EngineContext *ctx, Blueprint *blueprint)
{
    vec_blueprint_child_free(&blueprint->children);
    test_attr_set_free(ctx, &blueprint->attrs);
}

void test_level_free(struct EngineContext *ctx, Level *level)
{
    for (int index = 0; index < level->entity_count; index++) {
        test_entity_free(ctx, &level->entities[index]);
    }
}

void test_entity_free(struct EngineContext *ctx, Entity *entity)
{
    test_attr_set_free(ctx, &entity->attrs);
}

void test_flag_set_free(struct EngineContext *ctx, FlagSet *flags)
{
    flag_set_free(ctx, flags);
}

void test_attr_set_free(struct EngineContext *ctx, AttrSet *set)
{
    attr_set_free(ctx, set);
}
