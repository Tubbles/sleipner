#include "attribute.h"
#include "error.h"
#include "str.h"
#include "strv.h"
#include "vec.h"

#include <string.h>

VEC_IMPL(attribute, Attribute)

const Attribute *attr_get(const AttrSet *set, const char *name)
{
    for (int index = 0; index < set->entries.count; index++) {
        if (strv_eq_cstr(str_to_strv(set->entries.data[index].name), name)) {
            return &set->entries.data[index];
        }
    }
    return NULL;
}

static Attribute *find_or_append(struct EngineContext *ctx, AttrSet *set, const char *name)
{
    for (int index = 0; index < set->entries.count; index++) {
        if (strv_eq_cstr(str_to_strv(set->entries.data[index].name), name)) {
            Attribute *entry = &set->entries.data[index];
            /* Free old string value so callers can safely overwrite with any type. */
            if (entry->type == ATTR_STRING) {
                str_free(NULL, &entry->value.str);
                entry->value = (AttrValue){0};
            }
            return entry;
        }
    }
    Attribute new_entry = {0};
    if (!str_from_cstr(NULL, &new_entry.name, name)) {
        error_set(ctx, "attribute name alloc failed for '%s'", name);
        return NULL;
    }
    if (!vec_attribute_push(&set->entries, new_entry, NULL)) {
        str_free(NULL, &new_entry.name);
        error_set(ctx, "attribute push failed for '%s'", name);
        return NULL;
    }
    return &set->entries.data[set->entries.count - 1];
}

void attr_set_free(struct EngineContext *ctx, AttrSet *set)
{
    for (int index = 0; index < set->entries.count; index++) {
        str_free(NULL, &set->entries.data[index].name);
        if (set->entries.data[index].type == ATTR_STRING) {
            str_free(NULL, &set->entries.data[index].value.str);
        }
    }
    vec_attribute_free(&set->entries, NULL);
    *set = (AttrSet){0};
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
    return str_from_cstr(NULL, &entry->value.str, pair.value);
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
        return entry->value.str.ptr;
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
