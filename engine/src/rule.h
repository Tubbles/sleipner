#pragma once

#include "alloc.h"
#include "arena.h"
#include "entity.h"
#include "map.h" // IWYU pragma: export
#include "str.h"
#include "strv.h"
#include "vec.h" // IWYU pragma: export

#include <stdbool.h>

#include "diag.h"

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
    TRIGGER_TIMER,
    TRIGGER_TIMER_PERIODIC,
    TRIGGER_ON_DESTROY,
    TRIGGER_DEFEAT,
    TRIGGER_COLLIDE,
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
    ACTION_CREATE_TIMER_PERIODIC,
    ACTION_DESTROY_TIMER,
    ACTION_IF_ELSE,
    ACTION_REPEAT,
    ACTION_FOR_EACH,
} ActionType;

/* --- Local variable scope chain (C stack frames — no arena allocation) --- */
typedef struct LocalScope {
    Str bind_name;                  /* the bound name, e.g. "enemy" */
    int entity_index;               /* which entity is bound */
    const struct LocalScope *outer; /* enclosing scope, nullptr at top level */
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

    /* For control flow nodes: index lists into the owning ActionTree's flat
     * node pool (ActionTree.nodes). Empty for simple (non-control-flow) nodes. */
    vec_int children;
    vec_int else_children;
};

VEC_DECL(action_node, ActionNode)

typedef struct {
    vec_action_node nodes; /* flat pool: every node in the tree, top-level and nested */
    vec_int roots;         /* indices into nodes of the top-level actions, in order */
} ActionTree;

/* --- Rule --- */
typedef struct Rule {
    Trigger trigger;
    vec_condition conditions;
    ActionTree action_tree;
} Rule;

VEC_DECL(rule, Rule)

/* --- Timers --- */
typedef struct {
    Str name;
    int entity_index; /* owning entity */
    float remaining;  /* seconds until next fire */
    float duration;   /* full period (periodic) or original duration (one-shot) */
    bool periodic;
} Timer;

VEC_DECL(timer, Timer)

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
void flag_set(Diag *diag, Allocator *alloc, FlagSet *flags, const char *name);
void flag_clear(Allocator *alloc, FlagSet *flags, const char *name);
void flag_set_free(Allocator *alloc, FlagSet *flags);

/* --- Trigger events --- */
typedef struct {
    TriggerType type;
    int entity_index;
    Str argument;
} TriggerEvent;

VEC_DECL(trigger_event, TriggerEvent)

/* --- Parsing (from TOML) --- */
[[nodiscard]] bool trigger_parse(Diag *diag, Allocator *alloc, Trigger *trigger, const char *string);
[[nodiscard]] bool condition_parse(Diag *diag, Allocator *alloc, Condition *condition, const char *string);
[[nodiscard]] bool action_node_parse(Diag *diag, Allocator *alloc, ActionNode *node, toml_datum_t value);
[[nodiscard]] bool
rules_parse(Diag *diag, Allocator *alloc, vec_rule *rules, toml_table_t *toml_blueprint_table, Arena *arena);
[[nodiscard]] bool
subroutines_parse(Diag *diag, Allocator *alloc, vec_subroutine *subroutines, toml_table_t *toml_root, Arena *arena);

/* --- Display labels ---
 *
 * Bare TOML keywords for each type's canonical vocabulary, independent of
 * argument/compare_value. Consumed by the editor's read-only rule tree view
 * (S5.6a, editor/rule.c) so it doesn't hand-roll a second copy of the
 * vocabulary trigger_parse/condition_parse (above) and action_mappings/
 * toml_emitter.c's emit_trigger_value/emit_condition_value already encode. */
const char *trigger_type_label(TriggerType type);
const char *condition_type_label(ConditionType type);
/* Non-owning view into action_mappings' own prefix string with the trailing
 * ':' trimmed (e.g. "set_flag" not "set_flag:"). Empty for the three
 * control-flow types (ACTION_IF_ELSE/ACTION_REPEAT/ACTION_FOR_EACH), which
 * aren't in action_mappings (they have no TOML prefix -- see
 * toml_emitter.c's action_emit_table) -- callers render those by kind. */
Strv action_type_label(ActionType type);

/* --- Trigger matching --- */
bool trigger_matches(const Trigger *trigger, const TriggerEvent *event);

/* --- Entity view: one entity paired with its blueprint-resolved attribute
 * defaults. Built once at the game.c batch boundary and passed through the
 * rule VM in place of the old parallel entities[] + entity_defaults[] arrays. */
typedef struct {
    Entity *entity;
    const AttrSet *defaults;
} EntityView;

/* --- Condition evaluation --- */
typedef struct {
    const Entity *entity;
    int entity_index;
    const EntityView *views;
    int view_count;
    const FlagSet *flags;
    const AttrSet *local_vars;
    const AttrSet *global_vars;
} ConditionContext;

bool conditions_evaluate(const Condition *conditions, int count, ConditionContext context);

/* --- Level transition request (written by ACTION_TRANSITION, read by game loop) --- */
typedef struct {
    bool pending;
    Str level; /* target level name */
    float x;   /* player spawn X */
    float y;   /* player spawn Y */
} TransitionRequest;

/* --- Action execution --- */
#define MAX_CALL_DEPTH 32

typedef struct {
    Entity *entity;
    int entity_index;
    EntityView *views;
    int view_count;
    FlagSet *flags;
    vec_trigger_event *event_queue;
    AttrSet *local_vars;
    AttrSet *global_vars;
    /* Backs writes to `flags` and `global_vars` only — both live in the
     * process-lifetime progression arena (see progression.h), which
     * survives the level-transition/hot-reload arena_restore that
     * rewinds everything else here. `local_vars` stays on the caller's
     * `alloc` (see action_node_execute / rules_evaluate_batch). */
    Allocator *progression_alloc;
    const LocalScope *scope;            /* entity binding chain — walked by resolve_target */
    const vec_subroutine *subroutines;  /* read-only subroutine table */
    vec_timer *timers;                  /* mutable timer list for create_timer/destroy_timer */
    TransitionRequest *transition;      /* written by transition action */
    int call_depth;                     /* recursion guard for call: */
    const vec_action_node *action_pool; /* pool the current tree's children/else_children indices resolve against */
} ActionContext;

[[nodiscard]] bool action_node_execute(Diag *diag, Allocator *alloc, const ActionNode *node, ActionContext context);

/* --- Evaluation loop ---
 * `alloc` is the persistent gamedata allocator (the ACTION_TRANSITION handler
 * reads `transition->level`, allocated from it, after the batch returns).
 * `progression_alloc` backs writes to `flags` and `global_vars` (see
 * ActionContext.progression_alloc). `scratch_alloc` backs the per-batch
 * cascade event vecs and may be safely rewound as soon as the batch
 * returns. Keeping the three Allocator* params non-adjacent avoids
 * bugprone-easily-swappable-parameters between any pair of them. */
void rules_evaluate_batch(Diag *diag,
                          Allocator *alloc,
                          EntityView *views,
                          int view_count,
                          const TriggerEvent *events,
                          int event_count,
                          FlagSet *flags,
                          AttrSet *global_vars,
                          Allocator *progression_alloc,
                          map_entity_ruleset *rule_table,
                          const vec_subroutine *subroutines,
                          vec_timer *timers,
                          Allocator *scratch_alloc,
                          TransitionRequest *transition);
