#ifndef ATTRIBUTE_H
#define ATTRIBUTE_H

#include <stdbool.h>

struct EngineContext;

#define MAX_ATTR_NAME 32
#define MAX_ATTR_STRING 64
#define MAX_ATTRS 32

typedef enum {
    ATTR_FLOAT,
    ATTR_INT,
    ATTR_BOOL,
    ATTR_STRING,
} AttrType;

typedef union {
    float f;
    int i;
    bool b;
    char s[MAX_ATTR_STRING];
} AttrValue;

typedef struct {
    char name[MAX_ATTR_NAME];
    AttrType type;
    AttrValue value;
} Attribute;

typedef struct {
    Attribute entries[MAX_ATTRS];
    int count;
} AttrSet;

/* Find an attribute by name. Returns NULL if not found. */
const Attribute *attr_get(const AttrSet *set, const char *name);

/* Set an attribute. Overwrites if name exists, appends if new.
 * Returns true on success, false if the set is full. */
[[nodiscard]] bool attr_set_float(struct EngineContext *ctx, AttrSet *set, const char *name, float value);
[[nodiscard]] bool attr_set_int(struct EngineContext *ctx, AttrSet *set, const char *name, int value);
[[nodiscard]] bool attr_set_bool(struct EngineContext *ctx, AttrSet *set, const char *name, bool value);
typedef struct {
    const char *name;
    const char *value;
} AttrStringPair;

[[nodiscard]] bool attr_set_string(struct EngineContext *ctx, AttrSet *set, AttrStringPair pair);

/* Convenience getters with fallback defaults. */
float attr_get_float(const AttrSet *set, const char *name, float fallback);
int attr_get_int(const AttrSet *set, const char *name, int fallback);
bool attr_get_bool(const AttrSet *set, const char *name, bool fallback);
const char *attr_get_string(const AttrSet *set, const char *name);

/* Look up an attribute by name, falling back to a second set if not found
 * in the first. Used for instance -> blueprint scoping. */
const Attribute *attr_get_scoped(const AttrSet *instance, const AttrSet *blueprint, const char *name);

#endif
