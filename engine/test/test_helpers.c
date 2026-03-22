#include "test_helpers.h"

void test_blueprint_table_free(BlueprintTable *table)
{
    for (int index = 0; index < table->entries.count; index++) {
        test_blueprint_free(&table->entries.data[index]);
    }
    vec_blueprint_free(&table->entries);
}

void test_blueprint_free(Blueprint *blueprint)
{
    vec_blueprint_child_free(&blueprint->children);
    test_attr_set_free(&blueprint->attrs);
}

void test_level_free(Level *level)
{
    for (int index = 0; index < level->entity_count; index++) {
        test_entity_free(&level->entities[index]);
    }
}

void test_entity_free(Entity *entity)
{
    test_attr_set_free(&entity->attrs);
}

void test_flag_set_free(struct EngineContext *ctx, FlagSet *flags)
{
    flag_set_free(ctx, flags);
}

void test_attr_set_free(AttrSet *set)
{
    vec_attribute_free(&set->entries);
}
