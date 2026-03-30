#include "helpers_test.h"
#include "str.h"

void test_blueprint_table_free(BlueprintTable *table)
{
    for (int index = 0; index < table->entries.count; index++) {
        test_blueprint_free(&table->entries.data[index]);
    }
    vec_blueprint_free(&table->entries, NULL);
}

void test_blueprint_free(Blueprint *blueprint)
{
    for (int index = 0; index < blueprint->children.count; index++) {
        str_free(NULL, &blueprint->children.data[index].blueprint_name);
        str_free(NULL, &blueprint->children.data[index].tag);
    }
    vec_blueprint_child_free(&blueprint->children, NULL);
    test_attr_set_free(&blueprint->attrs);
}

void test_level_free(Level *level)
{
    level_free(NULL, level);
}

void test_entity_free(Entity *entity)
{
    str_free(NULL, &entity->blueprint_name);
    str_free(NULL, &entity->tag);
    test_attr_set_free(&entity->attrs);
}

void test_flag_set_free(FlagSet *flags)
{
    flag_set_free(NULL, flags);
}

void test_attr_set_free(AttrSet *set)
{
    attr_set_free(NULL, set);
}
