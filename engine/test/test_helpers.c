#include "test_helpers.h"
#include "str.h"

void test_blueprint_table_free(struct EngineContext *ctx, BlueprintTable *table)
{
    for (int index = 0; index < table->entries.count; index++) {
        test_blueprint_free(ctx, &table->entries.data[index]);
    }
    vec_blueprint_free(&table->entries);
}

void test_blueprint_free(struct EngineContext *ctx, Blueprint *blueprint)
{
    str_free(ctx, &blueprint->name);
    str_free(ctx, &blueprint->extends_name);
    str_free(ctx, &blueprint->texture_name);
    for (int index = 0; index < blueprint->children.count; index++) {
        str_free(ctx, &blueprint->children.data[index].blueprint_name);
        str_free(ctx, &blueprint->children.data[index].tag);
    }
    vec_blueprint_child_free(&blueprint->children);
    test_attr_set_free(ctx, &blueprint->attrs);
}

void test_level_free(struct EngineContext *ctx, Level *level)
{
    level_free(ctx, level);
}

void test_entity_free(struct EngineContext *ctx, Entity *entity)
{
    str_free(ctx, &entity->blueprint_name);
    str_free(ctx, &entity->tag);
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
