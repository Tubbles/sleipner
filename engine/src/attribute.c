#include "attribute.h"
#include "error.h"

#include <string.h>

const Attribute *attr_get(const AttrSet *set, const char *name)
{
    for (int index = 0; index < set->count; index++) {
        if (strcmp(set->entries[index].name, name) == 0) {
            return &set->entries[index];
        }
    }
    return NULL;
}

static Attribute *find_or_append(struct EngineContext *ctx, AttrSet *set, const char *name)
{
    for (int index = 0; index < set->count; index++) {
        if (strcmp(set->entries[index].name, name) == 0) {
            return &set->entries[index];
        }
    }
    if (set->count >= MAX_ATTRS) {
        error_set(ctx, "attribute set full (max %d), cannot add '%s'", MAX_ATTRS, name);
        return NULL;
    }
    Attribute *entry = &set->entries[set->count];
    strncpy(entry->name, name, MAX_ATTR_NAME - 1);
    entry->name[MAX_ATTR_NAME - 1] = '\0';
    set->count++;
    return entry;
}

bool attr_set_float(struct EngineContext *ctx, AttrSet *set, const char *name, float value)
{
    Attribute *entry = find_or_append(ctx, set, name);
    if (!entry) {
        return false;
    }
    entry->type = ATTR_FLOAT;
    entry->value.f = value;
    return true;
}

bool attr_set_int(struct EngineContext *ctx, AttrSet *set, const char *name, int value)
{
    Attribute *entry = find_or_append(ctx, set, name);
    if (!entry) {
        return false;
    }
    entry->type = ATTR_INT;
    entry->value.i = value;
    return true;
}

bool attr_set_bool(struct EngineContext *ctx, AttrSet *set, const char *name, bool value)
{
    Attribute *entry = find_or_append(ctx, set, name);
    if (!entry) {
        return false;
    }
    entry->type = ATTR_BOOL;
    entry->value.b = value;
    return true;
}

bool attr_set_string(struct EngineContext *ctx, AttrSet *set, AttrStringPair pair)
{
    Attribute *entry = find_or_append(ctx, set, pair.name);
    if (!entry) {
        return false;
    }
    entry->type = ATTR_STRING;
    strncpy(entry->value.s, pair.value, MAX_ATTR_STRING - 1);
    entry->value.s[MAX_ATTR_STRING - 1] = '\0';
    return true;
}

float attr_get_float(const AttrSet *set, const char *name, float fallback)
{
    const Attribute *entry = attr_get(set, name);
    if (entry && entry->type == ATTR_FLOAT) {
        return entry->value.f;
    }
    return fallback;
}

int attr_get_int(const AttrSet *set, const char *name, int fallback)
{
    const Attribute *entry = attr_get(set, name);
    if (entry && entry->type == ATTR_INT) {
        return entry->value.i;
    }
    return fallback;
}

bool attr_get_bool(const AttrSet *set, const char *name, bool fallback)
{
    const Attribute *entry = attr_get(set, name);
    if (entry && entry->type == ATTR_BOOL) {
        return entry->value.b;
    }
    return fallback;
}

const char *attr_get_string(const AttrSet *set, const char *name)
{
    const Attribute *entry = attr_get(set, name);
    if (entry && entry->type == ATTR_STRING) {
        return entry->value.s;
    }
    return NULL;
}

const Attribute *attr_get_scoped(const AttrSet *instance, const AttrSet *blueprint, const char *name)
{
    const Attribute *entry = attr_get(instance, name);
    if (entry) {
        return entry;
    }
    return attr_get(blueprint, name);
}
