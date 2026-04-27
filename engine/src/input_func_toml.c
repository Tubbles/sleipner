#include "alloc.h"
#include "error.h"
#include "input_func.h"
#include "toml.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOML_BINDINGS_ERRBUF 200

/* Read an optional `gamepad = N` field; defaults to 0. */
static int parse_gamepad_id(toml_table_t *part)
{
    toml_datum_t gamepad = toml_int_in(part, "gamepad");
    if (gamepad.ok) {
        return (int)gamepad.u.i;
    }
    return 0;
}

/* Read an optional `scale = X` field; defaults to 1.0F. */
static float parse_scale(toml_table_t *part)
{
    toml_datum_t scale = toml_double_in(part, "scale");
    if (scale.ok) {
        return (float)scale.u.d;
    }
    toml_datum_t scale_int = toml_int_in(part, "scale");
    if (scale_int.ok) {
        return (float)scale_int.u.i;
    }
    return 1.0F;
}

/* Resolve a string field via the given lookup function. Returns the
 * resolved code on success; on failure sets `err` and returns -1. The
 * source string is freed before returning. */
static int resolve_named_field(
    toml_table_t *part, const char *field, const char *kind_label, int (*lookup)(const char *), ErrorState *err)
{
    toml_datum_t value = toml_string_in(part, field);
    if (!value.ok) {
        error_set(err, "kind=\"%s\" missing '%s' field", kind_label, field);
        return -1;
    }
    int code = lookup(value.u.s);
    if (code < 0) {
        error_set(err, "unknown %s name '%s'", field, value.u.s);
    }
    free(value.u.s);
    return code;
}

static bool parse_key_part(toml_table_t *part, AtomicInput *out, ErrorState *err)
{
    int code = resolve_named_field(part, "key", "key", input_func_key_from_name, err);
    if (code < 0) {
        return false;
    }
    out->kind = ATOM_KEY;
    out->int_a = code;
    return true;
}

static bool parse_gp_button_part(toml_table_t *part, AtomicInput *out, ErrorState *err)
{
    int code = resolve_named_field(part, "button", "gamepad_button", input_func_gp_button_from_name, err);
    if (code < 0) {
        return false;
    }
    out->kind = ATOM_GP_BUTTON;
    out->int_a = code;
    out->int_b = parse_gamepad_id(part);
    return true;
}

static bool parse_gp_axis_part(toml_table_t *part, AtomicInput *out, ErrorState *err)
{
    int code = resolve_named_field(part, "axis", "gamepad_axis", input_func_gp_axis_from_name, err);
    if (code < 0) {
        return false;
    }
    out->kind = ATOM_GP_AXIS;
    out->int_a = code;
    out->int_b = parse_gamepad_id(part);
    out->scale = parse_scale(part);
    return true;
}

static bool parse_gp_trigger_part(toml_table_t *part, AtomicInput *out, ErrorState *err)
{
    int code = resolve_named_field(part, "axis", "gamepad_trigger", input_func_gp_axis_from_name, err);
    if (code < 0) {
        return false;
    }
    out->kind = ATOM_GP_TRIGGER;
    out->int_a = code;
    out->int_b = parse_gamepad_id(part);
    out->scale = parse_scale(part);
    return true;
}

static bool resolve_optional_key(toml_table_t *part, const char *field, int *out_code, ErrorState *err)
{
    toml_datum_t value = toml_string_in(part, field);
    if (!value.ok) {
        *out_code = 0;
        return true;
    }
    int code = input_func_key_from_name(value.u.s);
    if (code < 0) {
        error_set(err, "unknown %s name '%s'", field, value.u.s);
        free(value.u.s);
        return false;
    }
    free(value.u.s);
    *out_code = code;
    return true;
}

static bool parse_kb_axis_part(toml_table_t *part, AtomicInput *out, ErrorState *err)
{
    int neg_code = 0;
    int pos_code = 0;
    if (!resolve_optional_key(part, "neg_key", &neg_code, err)) {
        return false;
    }
    if (!resolve_optional_key(part, "pos_key", &pos_code, err)) {
        return false;
    }
    if (neg_code == 0 && pos_code == 0) {
        error_set(err, "kind=\"kb_axis\" needs at least one of neg_key/pos_key");
        return false;
    }
    out->kind = ATOM_KB_AXIS;
    out->int_a = neg_code;
    out->int_b = pos_code;
    out->scale = parse_scale(part);
    return true;
}

static bool parse_part(toml_table_t *part, AtomicInput *out, ErrorState *err)
{
    toml_datum_t kind = toml_string_in(part, "kind");
    if (!kind.ok) {
        error_set(err, "binding part missing 'kind'");
        return false;
    }
    out->scale = 1.0F;
    out->int_b = 0;
    bool success = false;
    if (strcmp(kind.u.s, "key") == 0) {
        success = parse_key_part(part, out, err);
    } else if (strcmp(kind.u.s, "gamepad_button") == 0) {
        success = parse_gp_button_part(part, out, err);
    } else if (strcmp(kind.u.s, "gamepad_axis") == 0) {
        success = parse_gp_axis_part(part, out, err);
    } else if (strcmp(kind.u.s, "gamepad_trigger") == 0) {
        success = parse_gp_trigger_part(part, out, err);
    } else if (strcmp(kind.u.s, "kb_axis") == 0) {
        success = parse_kb_axis_part(part, out, err);
    } else {
        error_set(err, "unknown binding kind '%s'", kind.u.s);
    }
    free(kind.u.s);
    return success;
}

static bool parse_physical(toml_table_t *binding, PhysicalInput *out, Allocator alloc, ErrorState *err)
{
    toml_array_t *parts = toml_array_in(binding, "parts");
    if (!parts) {
        error_set(err, "binding missing 'parts' array");
        return false;
    }
    out->parts = vec_atomic_input_new(alloc);
    int count = toml_array_nelem(parts);
    for (int index = 0; index < count; index++) {
        toml_table_t *part = toml_table_at(parts, index);
        if (!part) {
            error_set(err, "binding parts[%d] is not a table", index);
            return false;
        }
        AtomicInput atom = {0};
        if (!parse_part(part, &atom, err)) {
            return false;
        }
        if (!vec_atomic_input_push(&out->parts, atom)) {
            error_set(err, "out of memory pushing atom");
            return false;
        }
    }
    return true;
}

static bool parse_alternatives(toml_table_t *func, vec_physical_input *out, Allocator alloc, ErrorState *err)
{
    toml_array_t *bindings = toml_array_in(func, "bindings");
    *out = vec_physical_input_new(alloc);
    if (!bindings) {
        return true; /* function present but no bindings array = empty alternatives */
    }
    int count = toml_array_nelem(bindings);
    for (int index = 0; index < count; index++) {
        toml_table_t *binding = toml_table_at(bindings, index);
        if (!binding) {
            error_set(err, "bindings[%d] is not a table", index);
            return false;
        }
        PhysicalInput physical;
        if (!parse_physical(binding, &physical, alloc, err)) {
            return false;
        }
        if (!vec_physical_input_push(out, physical)) {
            error_set(err, "out of memory pushing alternative");
            return false;
        }
    }
    return true;
}

bool input_func_load_bindings_toml(BindingStore *store, Allocator alloc, ErrorState *err, const char *toml_path)
{
    if (!toml_path) {
        return true;
    }
    FILE *file = fopen(toml_path, "re");
    if (!file) {
        if (errno == ENOENT) {
            return true; /* missing file is OK — defaults remain */
        }
        error_set(err, "fopen(%s): %s", toml_path, strerror(errno));
        return false;
    }
    char errbuf[TOML_BINDINGS_ERRBUF];
    toml_table_t *root = toml_parse_file(file, errbuf, (int)sizeof(errbuf));
    (void)fclose(file);
    if (!root) {
        error_set(err, "toml_parse_file(%s): %s", toml_path, errbuf);
        return false;
    }
    toml_table_t *function = toml_table_in(root, "function");
    if (!function) {
        toml_free(root);
        return true; /* no overrides */
    }
    for (int action = 0; action < ACTION_COUNT; action++) {
        const char *name = input_func_action_toml_name((InputAction)action);
        if (!name) {
            continue;
        }
        toml_table_t *func = toml_table_in(function, name);
        if (!func) {
            continue;
        }
        vec_physical_input parsed;
        if (!parse_alternatives(func, &parsed, alloc, err)) {
            error_wrap(err, "function.%s", name);
            toml_free(root);
            return false;
        }
        store->actions[action].alternatives = parsed;
    }
    for (int axis = 0; axis < AXIS_COUNT; axis++) {
        const char *name = input_func_axis_toml_name((InputAxis)axis);
        if (!name) {
            continue;
        }
        toml_table_t *func = toml_table_in(function, name);
        if (!func) {
            continue;
        }
        vec_physical_input parsed;
        if (!parse_alternatives(func, &parsed, alloc, err)) {
            error_wrap(err, "function.%s", name);
            toml_free(root);
            return false;
        }
        store->axes[axis].alternatives = parsed;
    }
    toml_free(root);
    return true;
}
