#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include "blueprint.h"
#include "level.h"
#include "rule.h"

struct EngineContext;

/* Cleanup helpers for test data structures */
void test_blueprint_table_free(BlueprintTable *table);
void test_blueprint_free(Blueprint *blueprint);
void test_level_free(Level *level);
void test_entity_free(Entity *entity);
void test_flag_set_free(FlagSet *flags);
void test_attr_set_free(AttrSet *set);

#endif
