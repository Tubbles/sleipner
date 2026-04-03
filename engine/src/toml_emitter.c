#include "toml_emitter.h"

#include "attribute.h"
#include "blueprint.h"
#include "entity.h"
#include "error.h"
#include "level.h"
#include "rule.h"

#include "raylib.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define FLOAT_STR_BUFSIZE 64

static int emit_append(char *buffer, int capacity, int offset, const char *format, ...)
    __attribute__((format(printf, 4, 5)));

static int emit_append(char *buffer, int capacity, int offset, const char *format, ...)
{
    if (offset < 0) {
        return offset;
    }

    va_list args;
    va_start(args, format);
    int remaining = capacity - offset;
    // NOLINTNEXTLINE(clang-analyzer-security.VAList) false positive, LLVM #40656
    int written = vsnprintf(buffer + offset, (size_t)(remaining > 0 ? remaining : 0), format, args);
    va_end(args);

    if (written < 0 || offset + written >= capacity) {
        return -1;
    }
    return offset + written;
}

/* ---- Attribute value emitter ---- */

static int emit_attr_value(char *buffer, int capacity, int offset, const Attribute *attr)
{
    switch (attr->type) {
    case ATTR_BOOL:
        if (attr->value.b) {
            return emit_append(buffer, capacity, offset, "%s", "true");
        }
        return emit_append(buffer, capacity, offset, "%s", "false");
    case ATTR_INT:
        return emit_append(buffer, capacity, offset, "%d", attr->value.i);
    case ATTR_FLOAT: {
        char tmp[FLOAT_STR_BUFSIZE];
        (void)snprintf(tmp, sizeof(tmp), "%g", (double)attr->value.f);
        /* TOML requires a decimal point so the parser keeps the value as float, not int. */
        if (strchr(tmp, '.') != nullptr) {
            return emit_append(buffer, capacity, offset, "%s", tmp);
        }
        if (strchr(tmp, 'e') != nullptr) {
            return emit_append(buffer, capacity, offset, "%s", tmp);
        }
        if (strchr(tmp, 'E') != nullptr) {
            return emit_append(buffer, capacity, offset, "%s", tmp);
        }
        return emit_append(buffer, capacity, offset, "%s.0", tmp);
    }
    case ATTR_STRING:
        return emit_append(buffer, capacity, offset, "\"%s\"", attr->value.str.ptr);
    }
    return offset;
}

/* Internal blueprint attr names emitted by dedicated code — skip in generic custom-attr pass */
static bool is_internal_bp_attr(const char *name)
{
    static const char *internal[] = {"name",
                                     "texture",
                                     "src_x",
                                     "src_y",
                                     "src_w",
                                     "src_h",
                                     "collision_offset_x",
                                     "collision_offset_y",
                                     "collision_w",
                                     "collision_h",
                                     "health",
                                     "max_health",
                                     nullptr};
    for (int index = 0; internal[index] != nullptr; index++) {
        if (strcmp(name, internal[index]) == 0) {
            return true;
        }
    }
    return false;
}

static int emit_custom_bp_attrs(char *buffer, int capacity, int offset, const AttrSet *attrs)
{
    for (int index = 0; index < attrs->entries.count; index++) {
        const Attribute *attr = &attrs->entries.data[index];
        if (is_internal_bp_attr(attr->name.ptr)) {
            continue;
        }
        offset = emit_append(buffer, capacity, offset, "%s = ", attr->name.ptr);
        offset = emit_attr_value(buffer, capacity, offset, attr);
        offset = emit_append(buffer, capacity, offset, "\n");
    }
    return offset;
}

static int emit_health_if_present(char *buffer, int capacity, int offset, const AttrSet *attrs)
{
    const Attribute *health = attr_get(attrs, "health");
    const Attribute *max_health = attr_get(attrs, "max_health");
    if (health == nullptr || max_health == nullptr) {
        return offset;
    }
    int current = (health->type == ATTR_INT) ? health->value.i : (int)health->value.f;
    int maximum = (max_health->type == ATTR_INT) ? max_health->value.i : (int)max_health->value.f;
    return emit_append(buffer, capacity, offset, "health = [%d, %d]\n", current, maximum);
}

/* ---- Rule emitters ---- */

static int emit_trigger_value(char *buffer, int capacity, int offset, const Trigger *trigger)
{
    switch (trigger->type) {
    case TRIGGER_INTERACT:
        return emit_append(buffer, capacity, offset, "\"interact\"");
    case TRIGGER_ENTER:
        return emit_append(buffer, capacity, offset, "\"enter\"");
    case TRIGGER_ON_SPAWN:
        return emit_append(buffer, capacity, offset, "\"on_spawn\"");
    case TRIGGER_ON_DESTROY:
        return emit_append(buffer, capacity, offset, "\"on_destroy\"");
    case TRIGGER_DEFEAT:
        return emit_append(buffer, capacity, offset, "\"defeat\"");
    case TRIGGER_EVENT:
        return emit_append(buffer, capacity, offset, "\"event:%s\"", trigger->argument.ptr);
    case TRIGGER_ATTR_CHANGED:
        return emit_append(buffer, capacity, offset, "\"attr_changed:%s\"", trigger->argument.ptr);
    case TRIGGER_TIMER:
        return emit_append(buffer, capacity, offset, "\"timer:%s\"", trigger->argument.ptr);
    case TRIGGER_TIMER_PERIODIC:
        return emit_append(buffer, capacity, offset, "\"timer_periodic:%s\"", trigger->argument.ptr);
    case TRIGGER_COLLIDE:
        if (trigger->argument.len > 0) {
            return emit_append(buffer, capacity, offset, "\"collide:%s\"", trigger->argument.ptr);
        }
        return emit_append(buffer, capacity, offset, "\"collide\"");
    }
    return -1;
}

static int emit_condition_value(char *buffer, int capacity, int offset, const Condition *condition)
{
    switch (condition->type) {
    case COND_FLAG:
        return emit_append(buffer, capacity, offset, "\"flag:%s\"", condition->argument.ptr);
    case COND_NOT_FLAG:
        return emit_append(buffer, capacity, offset, "\"not_flag:%s\"", condition->argument.ptr);
    case COND_ATTR:
        return emit_append(buffer, capacity, offset, "\"self.attr:%s\"", condition->argument.ptr);
    case COND_NOT_ATTR:
        return emit_append(buffer, capacity, offset, "\"self.not_attr:%s\"", condition->argument.ptr);
    case COND_ATTR_LT:
        return emit_append(buffer, capacity, offset, "\"self.attr:%s<%g\"", condition->argument.ptr,
                           (double)condition->compare_value);
    case COND_ATTR_GT:
        return emit_append(buffer, capacity, offset, "\"self.attr:%s>%g\"", condition->argument.ptr,
                           (double)condition->compare_value);
    case COND_ATTR_EQ:
        return emit_append(buffer, capacity, offset, "\"self.attr:%s==%g\"", condition->argument.ptr,
                           (double)condition->compare_value);
    case COND_HAS_ITEM:
        return emit_append(buffer, capacity, offset, "\"has_item:%s\"", condition->argument.ptr);
    case COND_VAR:
        return emit_append(buffer, capacity, offset, "\"var:%s\"", condition->argument.ptr);
    }
    return -1;
}

static int emit_conditions_inline_array(char *buffer, int capacity, int offset, const Condition *conditions, int count)
{
    offset = emit_append(buffer, capacity, offset, "[");
    for (int index = 0; index < count; index++) {
        if (index > 0) {
            offset = emit_append(buffer, capacity, offset, ", ");
        }
        offset = emit_condition_value(buffer, capacity, offset, &conditions[index]);
    }
    return emit_append(buffer, capacity, offset, "]");
}

typedef enum {
    ACTION_EMIT_NO_ARGS,
    ACTION_EMIT_ONE_ARG,
    ACTION_EMIT_TWO_ARGS,
} ActionEmitArgCount;

/* prefix first — avoids 8 bytes of padding between the 4-byte type enum and 8-byte pointer */
typedef struct {
    const char *prefix;
    ActionType type;
    ActionEmitArgCount arg_count;
} ActionEmitEntry;

static const ActionEmitEntry action_emit_table[] = {
    {.prefix = "destroy", .type = ACTION_DESTROY, .arg_count = ACTION_EMIT_NO_ARGS},
    {.prefix = "set_flag:", .type = ACTION_SET_FLAG, .arg_count = ACTION_EMIT_ONE_ARG},
    {.prefix = "clear_flag:", .type = ACTION_CLEAR_FLAG, .arg_count = ACTION_EMIT_ONE_ARG},
    {.prefix = "toggle_attr:", .type = ACTION_TOGGLE_ATTR, .arg_count = ACTION_EMIT_ONE_ARG},
    {.prefix = "fire_event:", .type = ACTION_FIRE_EVENT, .arg_count = ACTION_EMIT_ONE_ARG},
    {.prefix = "give_item:", .type = ACTION_GIVE_ITEM, .arg_count = ACTION_EMIT_ONE_ARG},
    {.prefix = "remove_item:", .type = ACTION_REMOVE_ITEM, .arg_count = ACTION_EMIT_ONE_ARG},
    {.prefix = "play_sound:", .type = ACTION_PLAY_SOUND, .arg_count = ACTION_EMIT_ONE_ARG},
    {.prefix = "dialogue:", .type = ACTION_DIALOGUE, .arg_count = ACTION_EMIT_ONE_ARG},
    {.prefix = "transition:", .type = ACTION_TRANSITION, .arg_count = ACTION_EMIT_TWO_ARGS},
    {.prefix = "spawn:", .type = ACTION_SPAWN, .arg_count = ACTION_EMIT_ONE_ARG},
    {.prefix = "camera_pan:", .type = ACTION_CAMERA_PAN, .arg_count = ACTION_EMIT_ONE_ARG},
    {.prefix = "camera_shake:", .type = ACTION_CAMERA_SHAKE, .arg_count = ACTION_EMIT_ONE_ARG},
    {.prefix = "call:", .type = ACTION_CALL, .arg_count = ACTION_EMIT_ONE_ARG},
    {.prefix = "wait:", .type = ACTION_WAIT, .arg_count = ACTION_EMIT_ONE_ARG},
    {.prefix = "destroy_timer:", .type = ACTION_DESTROY_TIMER, .arg_count = ACTION_EMIT_ONE_ARG},
    {.prefix = "set_attr:", .type = ACTION_SET_ATTR, .arg_count = ACTION_EMIT_TWO_ARGS},
    {.prefix = "add_attr:", .type = ACTION_ADD_ATTR, .arg_count = ACTION_EMIT_TWO_ARGS},
    {.prefix = "change_sprite:", .type = ACTION_CHANGE_SPRITE, .arg_count = ACTION_EMIT_TWO_ARGS},
    {.prefix = "set_var:", .type = ACTION_SET_VAR, .arg_count = ACTION_EMIT_TWO_ARGS},
    {.prefix = "create_timer:", .type = ACTION_CREATE_TIMER, .arg_count = ACTION_EMIT_TWO_ARGS},
    {.prefix = "create_timer_periodic:", .type = ACTION_CREATE_TIMER_PERIODIC, .arg_count = ACTION_EMIT_TWO_ARGS},
    {.prefix = nullptr, .type = ACTION_IF_ELSE, .arg_count = ACTION_EMIT_NO_ARGS}, /* control flow — no prefix */
    {.prefix = nullptr, .type = ACTION_REPEAT, .arg_count = ACTION_EMIT_NO_ARGS},
    {.prefix = nullptr, .type = ACTION_FOR_EACH, .arg_count = ACTION_EMIT_NO_ARGS},
};

static int emit_simple_action_string(char *buffer, int capacity, int offset, const ActionNode *node)
{
    int table_size = (int)(sizeof(action_emit_table) / sizeof(action_emit_table[0]));
    for (int index = 0; index < table_size; index++) {
        const ActionEmitEntry *entry = &action_emit_table[index];
        if (entry->prefix == nullptr || entry->type != node->type) {
            continue;
        }
        if (entry->arg_count == ACTION_EMIT_NO_ARGS) {
            return emit_append(buffer, capacity, offset, "\"%s\"", entry->prefix);
        }
        if (entry->arg_count == ACTION_EMIT_TWO_ARGS && node->second_argument.len > 0) {
            return emit_append(buffer, capacity, offset, "\"%s%s,%s\"", entry->prefix, node->argument.ptr,
                               node->second_argument.ptr);
        }
        return emit_append(buffer, capacity, offset, "\"%s%s\"", entry->prefix, node->argument.ptr);
    }
    return -1; /* unknown or control-flow node */
}

/* Emit a single child ActionNode — simple actions only (no nested control flow at this depth).
 * Avoids mutual recursion between emit_action_node_inline and emit_action_nodes_inline_array. */
static int emit_child_node_simple(char *buffer, int capacity, int offset, const ActionNode *node)
{
    return emit_simple_action_string(buffer, capacity, offset, node);
}

static int emit_child_nodes_inline_array(char *buffer, int capacity, int offset, const ActionNode *nodes, int count)
{
    offset = emit_append(buffer, capacity, offset, "[");
    for (int index = 0; index < count; index++) {
        if (index > 0) {
            offset = emit_append(buffer, capacity, offset, ", ");
        }
        offset = emit_child_node_simple(buffer, capacity, offset, &nodes[index]);
    }
    return emit_append(buffer, capacity, offset, "]");
}

/* Emit a top-level action node: simple actions as quoted strings, control flow as inline tables.
 * Control-flow children are serialised with emit_child_nodes_inline_array (one level deep only)
 * to avoid mutual recursion flagged by misc-no-recursion. */
static int emit_action_node_inline(char *buffer, int capacity, int offset, const ActionNode *node)
{
    switch (node->type) {
    case ACTION_IF_ELSE:
        offset = emit_append(buffer, capacity, offset, "{if = ");
        offset = emit_conditions_inline_array(buffer, capacity, offset, node->conditions.data, node->conditions.count);
        offset = emit_append(buffer, capacity, offset, ", then = ");
        offset = emit_child_nodes_inline_array(buffer, capacity, offset, node->children, node->child_count);
        if (node->else_child_count > 0) {
            offset = emit_append(buffer, capacity, offset, ", else = ");
            offset =
                emit_child_nodes_inline_array(buffer, capacity, offset, node->else_children, node->else_child_count);
        }
        return emit_append(buffer, capacity, offset, "}");
    case ACTION_REPEAT:
        offset = emit_append(buffer, capacity, offset, "{repeat = \"%s\", do = ", node->argument.ptr);
        offset = emit_child_nodes_inline_array(buffer, capacity, offset, node->children, node->child_count);
        return emit_append(buffer, capacity, offset, "}");
    case ACTION_FOR_EACH:
        offset = emit_append(buffer, capacity, offset, "{for_each = \"%s\"", node->argument.ptr);
        if (node->second_argument.len > 0) {
            offset = emit_append(buffer, capacity, offset, ", bind = \"%s\"", node->second_argument.ptr);
        }
        if (node->conditions.count > 0) {
            offset = emit_append(buffer, capacity, offset, ", conditions = ");
            offset =
                emit_conditions_inline_array(buffer, capacity, offset, node->conditions.data, node->conditions.count);
        }
        offset = emit_append(buffer, capacity, offset, ", do = ");
        offset = emit_child_nodes_inline_array(buffer, capacity, offset, node->children, node->child_count);
        return emit_append(buffer, capacity, offset, "}");
    default:
        return emit_simple_action_string(buffer, capacity, offset, node);
    }
}

static int emit_action_nodes_inline_array(char *buffer, int capacity, int offset, const vec_action_node *nodes)
{
    offset = emit_append(buffer, capacity, offset, "[");
    for (int index = 0; index < nodes->count; index++) {
        if (index > 0) {
            offset = emit_append(buffer, capacity, offset, ", ");
        }
        offset = emit_action_node_inline(buffer, capacity, offset, &nodes->data[index]);
    }
    return emit_append(buffer, capacity, offset, "]");
}

static int emit_rule(char *buffer, int capacity, int offset, const Rule *rule)
{
    offset = emit_append(buffer, capacity, offset, "[[blueprint.rule]]\n");
    offset = emit_append(buffer, capacity, offset, "trigger = ");
    offset = emit_trigger_value(buffer, capacity, offset, &rule->trigger);
    offset = emit_append(buffer, capacity, offset, "\n");
    if (rule->conditions.count > 0) {
        offset = emit_append(buffer, capacity, offset, "conditions = ");
        offset = emit_conditions_inline_array(buffer, capacity, offset, rule->conditions.data, rule->conditions.count);
        offset = emit_append(buffer, capacity, offset, "\n");
    }
    if (rule->action_tree.nodes.count > 0) {
        offset = emit_append(buffer, capacity, offset, "actions = ");
        offset = emit_action_nodes_inline_array(buffer, capacity, offset, &rule->action_tree.nodes);
        offset = emit_append(buffer, capacity, offset, "\n");
    }
    return emit_append(buffer, capacity, offset, "\n");
}

static int emit_blueprints(char *buffer, int capacity, int offset, const BlueprintTable *blueprints)
{
    for (int index = 0; index < blueprints->entries.count; index++) {
        const Blueprint *blueprint = &blueprints->entries.data[index];
        Rectangle source = blueprint_get_source(blueprint);
        Vector2 col_offset = blueprint_get_collision_offset(blueprint);
        Vector2 col_size = blueprint_get_collision_size(blueprint);

        offset = emit_append(buffer, capacity, offset, "[[blueprint]]\n");
        offset = emit_append(buffer, capacity, offset, "name = \"%s\"\n", attr_get_string(&blueprint->attrs, "name"));
        const char *texture_name = attr_get_string(&blueprint->attrs, "texture");
        if (texture_name) {
            offset = emit_append(buffer, capacity, offset, "texture = \"%s\"\n", texture_name);
            offset = emit_append(buffer, capacity, offset, "src = [%d, %d, %d, %d]\n", (int)source.x, (int)source.y,
                                 (int)source.width, (int)source.height);
        }
        offset = emit_append(buffer, capacity, offset, "collision_offset = [%d, %d]\n", (int)col_offset.x,
                             (int)col_offset.y);
        offset = emit_append(buffer, capacity, offset, "collision_size = [%d, %d]\n", (int)col_size.x, (int)col_size.y);
        offset = emit_health_if_present(buffer, capacity, offset, &blueprint->attrs);
        offset = emit_custom_bp_attrs(buffer, capacity, offset, &blueprint->attrs);
        offset = emit_append(buffer, capacity, offset, "\n");

        for (int child_index = 0; child_index < blueprint->children.count; child_index++) {
            const BlueprintChild *child = &blueprint->children.data[child_index];
            offset = emit_append(buffer, capacity, offset, "[[blueprint.child]]\n");
            offset = emit_append(buffer, capacity, offset, "blueprint = \"%s\"\n", child->blueprint_name.ptr);
            if (child->tag.len > 0) {
                offset = emit_append(buffer, capacity, offset, "tag = \"%s\"\n", child->tag.ptr);
            }
            if (child->offset.x != 0.0F || child->offset.y != 0.0F) {
                offset = emit_append(buffer, capacity, offset, "offset = [%d, %d]\n", (int)child->offset.x,
                                     (int)child->offset.y);
            }
            offset = emit_append(buffer, capacity, offset, "\n");
        }

        for (int rule_index = 0; rule_index < blueprint->rules.count; rule_index++) {
            offset = emit_rule(buffer, capacity, offset, &blueprint->rules.data[rule_index]);
        }
    }
    return offset;
}

static int emit_levels(char *buffer, int capacity, int offset, const Level *levels, int level_count)
{
    for (int level_index = 0; level_index < level_count; level_index++) {
        const Level *level = &levels[level_index];

        offset = emit_append(buffer, capacity, offset, "[[level]]\n");
        offset = emit_append(buffer, capacity, offset, "name = \"%s\"\n", level->name.ptr);
        offset = emit_append(buffer, capacity, offset, "size = [%d, %d]\n", level->width, level->height);

        if (level->music_name.len > 0) {
            offset = emit_append(buffer, capacity, offset, "music = \"%s\"\n", level->music_name.ptr);
        }
        if (level->floor_width != level->width || level->floor_height != level->height) {
            offset = emit_append(buffer, capacity, offset, "floor_size = [%d, %d]\n", level->floor_width,
                                 level->floor_height);
        }
        if (level->background_tile.len > 0 && strcmp(level->background_tile.ptr, "grass.png") != 0) {
            offset = emit_append(buffer, capacity, offset, "background_tile = \"%s\"\n", level->background_tile.ptr);
        }
        Color tint = level->background_tint;
        if (tint.r != 255 || tint.g != 255 || tint.b != 255) {
            offset = emit_append(buffer, capacity, offset, "background_tint = [%d, %d, %d]\n", tint.r, tint.g, tint.b);
        }
        offset = emit_append(buffer, capacity, offset, "\n");

        for (int entity_index = 0; entity_index < level->entities.count; entity_index++) {
            const Entity *entity = &level->entities.data[entity_index];

            /* Skip child entities — they are instantiated from blueprint children */
            if (entity->parent_index >= 0) {
                continue;
            }

            offset = emit_append(buffer, capacity, offset, "[[level.entity]]\n");
            offset = emit_append(buffer, capacity, offset, "blueprint = \"%s\"\n", entity->blueprint_name.ptr);
            offset = emit_append(buffer, capacity, offset, "pos = [%d, %d]\n", (int)entity->position.x,
                                 (int)entity->position.y);
            offset = emit_append(buffer, capacity, offset, "\n");
        }
    }
    return offset;
}

int toml_emit_gamedata(
    ErrorState *err, char *buffer, int capacity, const BlueprintTable *blueprints, const Level *levels, int level_count)
{
    int offset = 0;

    offset = emit_blueprints(buffer, capacity, offset, blueprints);
    offset = emit_levels(buffer, capacity, offset, levels, level_count);

    if (offset < 0) {
        error_set(err, "buffer too small (capacity %d)", capacity);
    }
    return offset;
}
