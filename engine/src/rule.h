#ifndef RULE_H
#define RULE_H

#include "alloc.h"
#include "arena.h"
#include "entity.h"
#include "map.h" // IWYU pragma: export
#include "str.h"
#include "vec.h" // IWYU pragma: export

#include <stdbool.h>

struct EngineContext;

// Include actual TOML headers instead of forward declarations
// This makes dependencies explicit and maintains module integrity
#include "toml.h"

#define MAX_CONDITIONS 8
#define MAX_ACTIONS 16
#define MAX_RULES 16
#define MAX_ARG 64
#define MAX_EVENT_CASCADES 8
#define INTERACT_RANGE 24.0F

/* --- Trigger types --- */
typedef enum {
    TRIGGER_INTERACT,
    TRIGGER_ENTER,
    TRIGGER_ON_SPAWN,
    TRIGGER_EVENT,
    TRIGGER_ATTR_CHANGED,
} TriggerType;

typedef struct {
    TriggerType type;
    Str argument;
} Trigger;

/* --- Condition types --- */
typedef enum {
    COND_FLAG,
    COND_NOT_FLAG,
    COND_ATTR,
    COND_NOT_ATTR,
    COND_ATTR_LT,
    COND_ATTR_GT,
    COND_ATTR_EQ,
    COND_HAS_ITEM,
    COND_VAR,
} ConditionType;

typedef struct {
    ConditionType type;
    Str argument;
    float compare_value;
} Condition;

VEC_DECL(condition, Condition)

/* --- Action types --- */
typedef enum {
    ACTION_SET_FLAG,
    ACTION_CLEAR_FLAG,
    ACTION_SET_ATTR,
    ACTION_ADD_ATTR,
    ACTION_TOGGLE_ATTR,
    ACTION_DESTROY,
    ACTION_FIRE_EVENT,
    ACTION_GIVE_ITEM,
    ACTION_REMOVE_ITEM,
    ACTION_CHANGE_SPRITE,
    ACTION_PLAY_SOUND,
    ACTION_DIALOGUE,
    ACTION_TRANSITION,
    ACTION_SPAWN,
    ACTION_CAMERA_PAN,
    ACTION_CAMERA_SHAKE,
    ACTION_CALL,
    ACTION_SET_VAR,
    ACTION_WAIT,
    ACTION_CREATE_TIMER,
    ACTION_DESTROY_TIMER,
    ACTION_IF_ELSE,
    ACTION_REPEAT,
    ACTION_FOR_EACH,
} ActionType;

/* --- Local variable scope chain (C stack frames — no arena allocation) --- */
typedef struct LocalScope {
    Str bind_name;                  /* the bound name, e.g. "enemy" */
    int entity_index;               /* which entity is bound */
    const struct LocalScope *outer; /* enclosing scope, NULL at top level */
} LocalScope;

/* --- Control flow nodes --- */
typedef struct ActionNode ActionNode;

struct ActionNode {
    ActionType type;
    Str argument;        /* simple: target; repeat: count; for_each: collection */
    Str second_argument; /* simple: value;                for_each: bind name   */

    /* Pre-parsed conditions: if/else uses this as the predicate;
     * for_each uses it as the per-iteration filter. */
    vec_condition conditions;

    /* For control flow nodes (raw pointers — self-referential, can't use vec) */
    ActionNode *children;
    int child_count;
    ActionNode *else_children;
    int else_child_count;
};

VEC_DECL(action_node, ActionNode)

typedef struct {
    vec_action_node nodes;
} ActionTree;

/* --- Rule --- */
typedef struct Rule {
    Trigger trigger;
    vec_condition conditions;
    ActionTree action_tree;
} Rule;

VEC_DECL(rule, Rule)

/* --- Subroutines (named, reusable action sequences) --- */
typedef struct {
    Str name;
    ActionTree action_tree;
} Subroutine;

VEC_DECL(subroutine, Subroutine)

/* Maps entity ID (int) to the entity's rule set (vec_rule shallow copy). */
MAP_DECL(entity_ruleset, int, vec_rule)

/* --- FlagSet (global boolean flags) --- */
typedef struct {
    Str name;
} FlagName;

VEC_DECL(flag_name, FlagName)

typedef struct {
    vec_flag_name names;
} FlagSet;

bool flag_get(const FlagSet *flags, const char *name);
void flag_set(Allocator *alloc, FlagSet *flags, const char *name);
void flag_clear(Allocator *alloc, FlagSet *flags, const char *name);
void flag_set_free(Allocator *alloc, FlagSet *flags);

/* --- Trigger events --- */
typedef struct {
    TriggerType type;
    int entity_index;
    Str argument;
} TriggerEvent;

VEC_DECL(trigger_event, TriggerEvent)

#define MAX_CASCADE_EVENTS 64

typedef struct {
    TriggerEvent events[MAX_CASCADE_EVENTS];
    int count;
} TriggerEventQueue;

[[nodiscard]] static inline bool trigger_event_queue_push(TriggerEventQueue *queue, TriggerEvent event)
{
    if (queue->count >= MAX_CASCADE_EVENTS) {
        return false;
    }
    queue->events[queue->count] = event;
    queue->count++;
    return true;
}

/* --- Parsing (from TOML) --- */
[[nodiscard]] bool trigger_parse(struct EngineContext *ctx, Allocator *alloc, Trigger *trigger, const char *string);
[[nodiscard]] bool
condition_parse(struct EngineContext *ctx, Allocator *alloc, Condition *condition, const char *string);
[[nodiscard]] bool action_node_parse(struct EngineContext *ctx, Allocator *alloc, ActionNode *node, toml_datum_t value);
[[nodiscard]] bool
rules_parse(struct EngineContext *ctx, Allocator *alloc, vec_rule *rules, void *toml_blueprint_table, Arena *arena);
[[nodiscard]] bool subroutines_parse(
    struct EngineContext *ctx, Allocator *alloc, vec_subroutine *subroutines, void *toml_root, Arena *arena);

/* --- Trigger matching --- */
bool trigger_matches(const Trigger *trigger, const TriggerEvent *event);

/* --- Condition evaluation --- */
typedef struct {
    const Entity *entity;
    const Entity *entities;
    int entity_count;
    const FlagSet *flags;
    const AttrSet *local_vars;
    const AttrSet *global_vars;
} ConditionContext;

bool conditions_evaluate(const Condition *conditions, int count, ConditionContext context);

/* --- Action execution --- */
#define MAX_CALL_DEPTH 32

typedef struct {
    Entity *entity;
    int entity_index;
    Entity *entities;
    int entity_count;
    FlagSet *flags;
    TriggerEventQueue *event_queue;
    AttrSet *local_vars;
    AttrSet *global_vars;
    const LocalScope *scope;           /* entity binding chain — walked by resolve_target */
    const vec_subroutine *subroutines; /* read-only subroutine table */
    int call_depth;                    /* recursion guard for call: */
} ActionContext;

[[nodiscard]] bool
action_node_execute(struct EngineContext *ctx, Allocator *alloc, const ActionNode *node, ActionContext context);

/* --- Evaluation loop --- */
void rules_evaluate_batch(struct EngineContext *ctx,
                          Allocator *alloc,
                          Entity *entities,
                          int entity_count,
                          const TriggerEvent *events,
                          int event_count,
                          FlagSet *flags,
                          AttrSet *global_vars,
                          map_entity_ruleset *rule_table,
                          const vec_subroutine *subroutines);

#endif
