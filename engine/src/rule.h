#pragma once

#include "alloc.h"
#include "arena.h"
#include "effect.h"
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

/* --- Local variable scope chain (owned by an ExecFrame on the rule VM's
 * fixed-size execution frame stack, rule.c -- still a plain C-stack array,
 * no arena allocation) --- */
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

/* --- Suspendable rule continuations (S6.7b, D24) ---
 *
 * `wait:` (and, since S6.7c, blocking `dialogue:`) needs to suspend a
 * rule's in-progress ExecFrame stack (rule.c) across frame boundaries and
 * resume it later. A continuation must never hold a raw pointer into
 * anything volatile: the entity array can reallocate (S6.6 spawn) and
 * per-batch scratch is gone by the next frame. So everything here is a
 * stable reference (entity id, node/pool index) re-resolved at resume
 * time, never a live pointer.
 *
 * ExecFrameKind is declared here (not file-local to rule.c, unlike S6.7a)
 * because ExecFrameSnapshot needs to name it. */
typedef enum {
    EXEC_FRAME_SEQUENCE, /* plain ordered child list: rule/subroutine roots, an if_else branch, or a call's
                          * subroutine roots */
    EXEC_FRAME_REPEAT,   /* re-runs `indices` repeat_remaining more times, looping child_cursor back to 0 between
                          * passes */
    EXEC_FRAME_FOR_EACH, /* re-runs `indices` once per matching entity, rebinding for_each_entity_index/
                          * for_each_scope between passes */
} ExecFrameKind;

/* One frame of a suspended ExecFrame stack, snapshotted as pool/node
 * indices instead of pointers. `pool_id` is -1 for the rule's own
 * action_tree, or an index into GamedataState.subroutines otherwise --
 * this is what lets a CALL frame be snapshotted at all (S6.7a's raw
 * vec_action_node* pointer could not be). `node_index` is -1 for a frame
 * that runs a root list directly (the rule's own roots when pool_id==-1,
 * or that subroutine's roots when pool_id>=0) rather than a control-flow
 * node's children -- mirroring ExecFrame.node_index in rule.c.
 * `else_branch` only means something when the resolved node is
 * ACTION_IF_ELSE: which branch's children this frame is mid-way through
 * (the condition is not re-evaluated at resume -- doing so could diverge
 * from the branch actually taken). `for_each_entity_index`/
 * `bound_entity_id` are EXEC_FRAME_FOR_EACH-only: the raw view-index scan
 * cursor (best-effort restart point -- the view array may have shifted by
 * resume time) and the stable id of the entity actually bound (used to
 * re-resolve the correct view at resume; the continuation is dropped if
 * that entity is gone). */
typedef struct {
    int pool_id;
    int node_index;
    int child_cursor;
    ExecFrameKind kind;
    bool else_branch;
    int repeat_remaining;
    int for_each_entity_index;
    int bound_entity_id;
    /* ActionContext.call_depth as of this frame's push, so the
     * MAX_CALL_DEPTH recursion guard stays correct across a resume
     * (e.g. a subroutine that waits, then calls another subroutine after
     * waking) instead of resetting to 0. */
    int call_depth;
} ExecFrameSnapshot;

VEC_DECL(exec_frame_snapshot, ExecFrameSnapshot)

/* What wakes a suspended continuation. WAKE_TIMER is ticked down by
 * rules_resume_continuations each frame; WAKE_DIALOGUE_CLOSED (S6.7c) has
 * no countdown -- it becomes due the moment DialogueState.active goes
 * false, checked directly rather than ticked. */
typedef enum {
    WAKE_TIMER,
    WAKE_DIALOGUE_CLOSED,
} WakeKind;

typedef struct {
    WakeKind kind;
    float remaining; /* WAKE_TIMER only: seconds until due, ticked by rules_resume_continuations */
} Wake;

/* A suspended rule execution. `entity_id`/`rule_index` identify the RULE
 * OWNER (re-resolved via map_entity_ruleset_get(rule_table, entity_id)
 * ->data[rule_index] at resume) -- not necessarily the entity bound at
 * the moment of suspension, which may be some other entity reached
 * through a for_each (see ExecFrameSnapshot.bound_entity_id for that).
 * `frames` is the live ExecFrame stack at suspend time, bottom (the
 * rule's root sequence) to top (the frame executing the wait), so
 * resuming replays it in the same order roots/for_each/call frames were
 * originally pushed. `local_vars` is a plain value copy: its backing
 * vec_attribute entries already live in the gamedata allocator (see
 * evaluate_entity_rules), so copying the AttrSet header is a complete,
 * safe snapshot -- nothing here is scratch- or stack-backed. */
typedef struct {
    int entity_id;
    int rule_index;
    vec_exec_frame_snapshot frames;
    AttrSet local_vars;
    Wake wake;
} RuleContinuation;

VEC_DECL(rule_continuation, RuleContinuation)

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

/* --- ItemSet (inventory: item-name -> count, S6.8a, D25) ---
 *
 * Unlimited, stackable by count; item ids are display-name strings in v1
 * (equipment/categories and the pause-menu inventory grid are deferred).
 * Keyed on Strv, reusing strv_hash (strv.h) the same way map_strv_sound
 * (audio.h) does -- the only other Strv-keyed map in the codebase.
 *
 * Key lifetime is the trap here: `item_give`'s `name` argument is a raw
 * C string view into an ActionNode's `argument` (a Str parsed into
 * gamedata_arena at load time), which is REWOUND by every level
 * transition/hot-reload. But ItemSet lives in the process-lifetime
 * progression_arena (ProgressionState, progression.h) alongside FlagSet,
 * so a map key pointing straight into gamedata_arena would dangle after
 * the first transition. `item_give` copies the name into `alloc`
 * (progression_alloc) on first insert, and only mutates the existing
 * entry's value (never its key) on every subsequent give -- see the
 * function body comment for why that makes a transient, gamedata-arena
 * key safe to pass as the lookup argument. */
MAP_DECL(strv_int, Strv, int)

typedef struct {
    map_strv_int counts;
} ItemSet;

/* 0 if `name` has never been given, or has been removed back down to 0. */
int item_count(const ItemSet *items, const char *name);
/* item_count(items, name) > 0. */
bool item_has(const ItemSet *items, const char *name);
/* +1, creating the entry (count 1) if absent. `alloc` must be the
 * process-lifetime progression allocator -- see the ItemSet doc comment
 * above for why a fresh key is copied into it rather than borrowed from
 * the caller's (gamedata-arena-backed) `name`. */
void item_give(Diag *diag, Allocator *alloc, ItemSet *items, const char *name);
/* -1; removes the entry entirely once its count reaches 0. No-op if
 * `name` was never given (or already removed). Never allocates -- a
 * decrement/removal never needs progression_alloc. */
void item_remove(ItemSet *items, const char *name);
void item_set_free(Allocator *alloc, ItemSet *items);

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
    /* Backs COND_HAS_ITEM (S6.8a). nullptr is tolerated -- has_item
     * reads as false, mirroring how other conditions treat a missing
     * lookup as not-met. */
    const ItemSet *items;
} ConditionContext;

bool conditions_evaluate(const Condition *conditions, int count, ConditionContext context);

/* --- Level transition request (written by ACTION_TRANSITION, read by game loop) --- */
typedef struct {
    bool pending;
    Str level; /* target level name */
    float x;   /* player spawn X */
    float y;   /* player spawn Y */
} TransitionRequest;

VEC_DECL(str, Str) /* one dialogue page per entry -- see DialogueState.pages below */

/* --- Blocking dialogue state (written by ACTION_DIALOGUE, S6.7c, D24) ---
 *
 * Lives on GameState, NOT GamedataState -- like TransitionRequest above,
 * `dialogue:` writes into it DIRECTLY from the rule VM rather than through
 * EffectQueue's deferred per-frame drain, because D24 requires dialogue to
 * BLOCK: the world freezes and the triggering rule suspends until the
 * player closes the box, which a fire-and-forget queue can't express (see
 * ActionContext.dialogue's doc comment below). Not undo-snapshotted, and
 * reset to inactive everywhere GamedataState.continuations is reset
 * (game_load_gamedata's early-reset block, setup_current_level_runtime) --
 * `pages` is allocated from gamedata_arena, so it must not outlive that
 * arena's rewind on reload/transition/level-switch.
 *
 * `pages` is the dialogue action's whole argument -- a single, unsplit
 * ONE_ARG string (dialogue text may contain commas/spaces, so the generic
 * two-arg comma split coord-style actions use must not apply here, see
 * rule.c's ActionMapping.single_arg) -- split on DIALOGUE_PAGE_DELIMITER,
 * one page if the delimiter is absent (dialogue_open). Each page is copied
 * into the caller's allocator at open time even though the source text
 * already lives in gamedata_arena, cheap insurance that keeps the
 * ownership story simple.
 *
 * `reveal_elapsed` is seconds since `current_page` was last opened or
 * advanced; dialogue_revealed_char_count derives the number of characters
 * to show from it (the typewriter effect). Reset to 0 whenever
 * `current_page` advances. */
typedef struct {
    bool active;
    vec_str pages;
    int current_page;
    float reveal_elapsed;
} DialogueState;

/* Characters revealed per second by the typewriter effect. Must stay
 * >= 1.0 -- dialogue_confirm's skip-to-full logic sets `reveal_elapsed`
 * to the page's character count (not character-count / this rate) as a
 * deliberately oversized value, which only guarantees full reveal on the
 * next computation if this rate is at least 1 char/sec. */
#define DIALOGUE_CHARS_PER_SECOND 30.0F

/* Delimiter that splits one dialogue action's text into multiple pages. */
#define DIALOGUE_PAGE_DELIMITER '|'

/* How many of `page_length` characters are revealed after `elapsed`
 * seconds at `chars_per_second`. Clamped to [0, page_length]. Exposed for
 * direct unit testing (rule_test.c); production callers pass
 * DIALOGUE_CHARS_PER_SECOND. */
int dialogue_revealed_char_count(float elapsed, float chars_per_second, int page_length);

/* Splits `text` on DIALOGUE_PAGE_DELIMITER into `state->pages` (one page
 * if the delimiter is absent), copying each page via `alloc`. Resets to
 * page 0, elapsed 0, active = true. Returns false only if a page copy
 * allocation fails. */
[[nodiscard]] bool dialogue_open(DialogueState *state, Allocator alloc, Strv text);

/* One frame's CONFIRM press against an active dialogue: if the current
 * page is mid-reveal, skips it to fully revealed; else if there is a next
 * page, advances to it and resets the typewriter; else (last page, fully
 * revealed) closes the dialogue (active = false). No-op if `state` is not
 * active. */
void dialogue_confirm(DialogueState *state);

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
    /* ACTION_GIVE_ITEM/ACTION_REMOVE_ITEM target (S6.8a). Same
     * progression-arena lifetime and progression_alloc backing as
     * `flags`/`global_vars` -- see the doc comment below and ItemSet's
     * own (rule.h, above). */
    ItemSet *items;
    /* Backs writes to `flags`, `global_vars`, and `items` only — all
     * three live in the process-lifetime progression arena (see
     * progression.h), which survives the level-transition/hot-reload
     * arena_restore that rewinds everything else here. `local_vars`
     * stays on the caller's `alloc` (see action_node_execute /
     * rules_evaluate_batch). */
    Allocator *progression_alloc;
    const LocalScope *scope;           /* entity binding chain — walked by resolve_target */
    const vec_subroutine *subroutines; /* read-only subroutine table */
    vec_timer *timers;                 /* mutable timer list for create_timer/destroy_timer */
    TransitionRequest *transition;     /* written by transition action */
    EffectQueue *effects;              /* effect channel -- not yet written by any action (S6.2 scaffold) */
    /* Written DIRECTLY by ACTION_DIALOGUE (S6.7c, D24), unlike the
     * deferred EffectQueue effects above: dialogue must suspend the rule
     * until the box closes, which the per-frame drain can't express.
     * nullptr is tolerated the same way effects/transition are -- the
     * action logs and skips opening but still suspends (see
     * handle_dialogue_action, rule.c). */
    DialogueState *dialogue;
    /* Sink for ACTION_WAIT (S6.7b, D24) and ACTION_DIALOGUE (S6.7c): when
     * execution suspends, the snapshot is pushed here. nullptr is
     * tolerated (a suspend silently no-ops, logged, like the effects/
     * transition nullptr fallbacks above) so callers that never suspend
     * (most direct action_node_execute test callers) don't need to wire
     * it up. */
    vec_rule_continuation *continuations;
    int call_depth;                     /* recursion guard for call: */
    const vec_action_node *action_pool; /* pool the current tree's children/else_children indices resolve against */
    /* Which pool `action_pool` is: -1 for the rule's own action_tree,
     * else an index into GamedataState.subroutines. Kept in lockstep with
     * action_pool everywhere it changes (evaluate_entity_rules,
     * expand_call_node, rules_resume_continuations) so a continuation
     * snapshot can record a serializable reference instead of the raw
     * action_pool pointer. */
    int pool_id;
    /* Rule identity this execution belongs to -- constant across
     * for_each/call/if_else rebinding (only for_each rebinds `entity`/
     * `entity_index`/`scope` above). Used solely to fill in
     * RuleContinuation.entity_id/rule_index if a wait suspends;
     * unrelated to which entity is currently bound for simple actions. */
    int rule_owner_entity_id;
    int rule_index;
} ActionContext;

[[nodiscard]] bool action_node_execute(Diag *diag, Allocator *alloc, const ActionNode *node, ActionContext context);

/* --- Evaluation loop ---
 * `alloc` is the persistent gamedata allocator (the ACTION_TRANSITION handler
 * reads `transition->level`, allocated from it, after the batch returns).
 * `progression_alloc` backs writes to `flags`, `global_vars`, and `items`
 * (see ActionContext.progression_alloc). `scratch_alloc` backs the per-batch
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
                          ItemSet *items,
                          Allocator *progression_alloc,
                          map_entity_ruleset *rule_table,
                          const vec_subroutine *subroutines,
                          vec_timer *timers,
                          Allocator *scratch_alloc,
                          TransitionRequest *transition,
                          EffectQueue *effects,
                          DialogueState *dialogue,
                          vec_rule_continuation *continuations);

/* --- Continuation resume pass (S6.7b, D24) ---
 * Must run before normal trigger detection/evaluation each frame: ticks
 * every WAKE_TIMER continuation's `remaining` down by delta_time, checks
 * every WAKE_DIALOGUE_CLOSED continuation against `dialogue->active`
 * (S6.7c -- due the moment it goes false, no countdown), and for each one
 * that becomes due, re-resolves its entity by id (dropping the
 * continuation silently if it's gone), rebuilds its ExecFrame stack from
 * the snapshot, and resumes execution. A continuation that completes runs
 * its rule's remaining sibling roots (still per-root isolated, see
 * execute_rule_roots in rule.c); one that suspends again (another wait or
 * dialogue) is replaced by the fresh snapshot. `views`/`view_count` must
 * be built the same way the caller builds them for rules_evaluate_batch
 * (same frame, same entity array) -- see game.c's build_entity_views.
 * `event_queue` receives any events a resumed action fires (fire_event,
 * destroy, defeat); the caller should feed these into the same frame's
 * own trigger cascade, mirroring how rules_evaluate_batch's internal
 * cascade already handles events fired by normally-triggered rules. No
 * scratch allocator is needed here -- nothing but `continuations` itself
 * (via `alloc`) ever grows. */
void rules_resume_continuations(Diag *diag,
                                Allocator *alloc,
                                EntityView *views,
                                int view_count,
                                vec_trigger_event *event_queue,
                                FlagSet *flags,
                                AttrSet *global_vars,
                                ItemSet *items,
                                Allocator *progression_alloc,
                                map_entity_ruleset *rule_table,
                                const vec_subroutine *subroutines,
                                vec_timer *timers,
                                TransitionRequest *transition,
                                EffectQueue *effects,
                                DialogueState *dialogue,
                                vec_rule_continuation *continuations,
                                float delta_time);
