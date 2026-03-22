#ifndef RULE_H
#define RULE_H

#include "alloc.h"
#include "arena.h"
#include "entity.h"
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
    char argument[MAX_ARG];
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
    char argument[MAX_ARG];
    float compare_value;
} Condition;

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

/* --- Control flow nodes --- */
typedef struct ActionNode ActionNode;

struct ActionNode {
    ActionType type;
    char argument[MAX_ARG];
    char second_argument[MAX_ARG];

    // For control flow nodes
    ActionNode *children;
    int child_count;
    ActionNode *else_children;
    int else_child_count;
};

typedef struct {
    ActionNode *nodes;
    int count;
} ActionTree;

/* --- Rule (named struct to match forward declaration in blueprint.h) --- */
typedef struct Rule {
    Trigger trigger;
    Condition conditions[MAX_CONDITIONS];
    int condition_count;
    ActionTree action_tree;
} Rule;

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
    char argument[MAX_ARG];
} TriggerEvent;

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
[[nodiscard]] bool trigger_parse(struct EngineContext *ctx, Trigger *trigger, const char *string);
[[nodiscard]] bool condition_parse(struct EngineContext *ctx, Condition *condition, const char *string);
[[nodiscard]] bool action_node_parse(struct EngineContext *ctx, ActionNode *node, toml_datum_t value, Arena *arena);
[[nodiscard]] bool rules_parse(struct EngineContext *ctx, RuleSet *rules, void *toml_blueprint_table, Arena *arena);

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
typedef struct {
    Entity *entity;
    int entity_index;
    Entity *entities;
    int entity_count;
    FlagSet *flags;
    TriggerEventQueue *event_queue;
    AttrSet *local_vars;
    AttrSet *global_vars;
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
                          AttrSet *global_vars);

#endif
