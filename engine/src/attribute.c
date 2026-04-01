#include "attribute.h"
#include "alloc.h"
#include "str.h"
#include "strv.h"
#include "vec.h"

VEC_IMPL(attribute, Attribute)

const Attribute *attr_get(const AttrSet *set, const char *name)
{
    for (int index = 0; index < set->entries.count; index++) {
        if (strv_eq_cstr(str_to_strv(set->entries.data[index].name), name)) {
            return &set->entries.data[index];
        }
    }
    return nullptr;
}

static Attribute *find_or_append(Allocator *alloc, AttrSet *set, const char *name)
{
    set->entries.alloc = *alloc;
    for (int index = 0; index < set->entries.count; index++) {
        if (strv_eq_cstr(str_to_strv(set->entries.data[index].name), name)) {
            Attribute *entry = &set->entries.data[index];
            /* Free old string value so callers can safely overwrite with any type. */
            if (entry->type == ATTR_STRING) {
                str_free(alloc, &entry->value.str);
                entry->value = (AttrValue){0};
            }
            return entry;
        }
    }
    Attribute new_entry = {0};
    if (!str_from_cstr(alloc, &new_entry.name, name)) {
        return nullptr;
    }
    if (!vec_attribute_push(&set->entries, new_entry)) {
        str_free(alloc, &new_entry.name);
        return nullptr;
    }
    return &set->entries.data[set->entries.count - 1];
}

void attr_set_free(Allocator *alloc, AttrSet *set)
{
    for (int index = 0; index < set->entries.count; index++) {
        str_free(alloc, &set->entries.data[index].name);
        if (set->entries.data[index].type == ATTR_STRING) {
            str_free(alloc, &set->entries.data[index].value.str);
        }
    }
    vec_attribute_free(&set->entries);
    *set = (AttrSet){0};
}

bool attr_set_float(Allocator *alloc, AttrSet *set, const char *name, float value)
{
    Attribute *entry = find_or_append(alloc, set, name);
    if (!entry) {
        return false;
    }
    entry->type = ATTR_FLOAT;
    entry->value.f = value;
    return true;
}

bool attr_set_int(Allocator *alloc, AttrSet *set, const char *name, int value)
{
    Attribute *entry = find_or_append(alloc, set, name);
    if (!entry) {
        return false;
    }
    entry->type = ATTR_INT;
    entry->value.i = value;
    return true;
}

bool attr_set_bool(Allocator *alloc, AttrSet *set, const char *name, bool value)
{
    Attribute *entry = find_or_append(alloc, set, name);
    if (!entry) {
        return false;
    }
    entry->type = ATTR_BOOL;
    entry->value.b = value;
    return true;
}

bool attr_set_string(Allocator *alloc, AttrSet *set, AttrStringPair pair)
{
    Attribute *entry = find_or_append(alloc, set, pair.name);
    if (!entry) {
        return false;
    }
    entry->type = ATTR_STRING;
    return str_from_cstr(alloc, &entry->value.str, pair.value);
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
    return nullptr;
}

void attr_remove(Allocator *alloc, AttrSet *set, const char *name)
{
    for (int index = 0; index < set->entries.count; index++) {
        if (!strv_eq_cstr(str_to_strv(set->entries.data[index].name), name)) {
            continue;
        }
        str_free(alloc, &set->entries.data[index].name);
        if (set->entries.data[index].type == ATTR_STRING) {
            str_free(alloc, &set->entries.data[index].value.str);
        }
        int last = set->entries.count - 1;
        if (index != last) {
            set->entries.data[index] = set->entries.data[last];
        }
        set->entries.count--;
        return;
    }
}

const Attribute *attr_get_scoped(const AttrSet *instance, const AttrSet *blueprint, const char *name)
{
    const Attribute *entry = attr_get(instance, name);
    if (entry) {
        return entry;
    }
    if (!blueprint) {
        return nullptr;
    }
    return attr_get(blueprint, name);
}

float attr_get_scoped_float(const AttrSet *instance, const AttrSet *defaults, const char *name, float fallback)
{
    const Attribute *entry = attr_get_scoped(instance, defaults, name);
    if (!entry) {
        return fallback;
    }
    if (entry->type == ATTR_FLOAT) {
        return entry->value.f;
    }
    if (entry->type == ATTR_INT) {
        return (float)entry->value.i;
    }
    return fallback;
}

int attr_get_scoped_int(const AttrSet *instance, const AttrSet *defaults, const char *name, int fallback)
{
    const Attribute *entry = attr_get_scoped(instance, defaults, name);
    if (!entry) {
        return fallback;
    }
    if (entry->type == ATTR_INT) {
        return entry->value.i;
    }
    if (entry->type == ATTR_FLOAT) {
        return (int)entry->value.f;
    }
    return fallback;
}

bool attr_get_scoped_bool(const AttrSet *instance, const AttrSet *defaults, const char *name, bool fallback)
{
    const Attribute *entry = attr_get_scoped(instance, defaults, name);
    if (entry && entry->type == ATTR_BOOL) {
        return entry->value.b;
    }
    return fallback;
}

const char *attr_get_scoped_string(const AttrSet *instance, const AttrSet *defaults, const char *name)
{
    const Attribute *entry = attr_get_scoped(instance, defaults, name);
    if (entry && entry->type == ATTR_STRING) {
        return entry->value.str.ptr;
    }
    return nullptr;
}
