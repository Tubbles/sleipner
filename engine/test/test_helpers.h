#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include "blueprint.h"
#include "level.h"
#include "rule.h"

struct EngineContext;

/* Cleanup helpers for test data structures */
void test_blueprint_table_free(struct EngineContext *ctx, BlueprintTable *table);
void test_blueprint_free(struct EngineContext *ctx, Blueprint *blueprint);
void test_level_free(struct EngineContext *ctx, Level *level);
void test_entity_free(struct EngineContext *ctx, Entity *entity);
void test_flag_set_free(struct EngineContext *ctx, FlagSet *flags);
void test_attr_set_free(struct EngineContext *ctx, AttrSet *set);

#endif
