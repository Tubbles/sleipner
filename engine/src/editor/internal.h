#pragma once

/* Internal header for editor split files. NOT part of the public API. */

#include "raylib.h"

#include "attribute.h"
#include "blueprint.h"
#include "diag.h"
#include "editor/editor.h"
#include "entity.h"
#include "game.h"
#include "input_func.h"
#include "level.h"
#include "rule.h"
#include "undo.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --- HUD hint table ---
 *
 * Each editor submode declares a static EditorHintTable describing the verbs
 * relevant to that mode. The hints bar in draw.c renders a table by looking
 * each action up in the BindingStore via input_func_label, so on-screen
 * labels are always authoritative — they cannot drift from the bindings the
 * handler actually reads. */
typedef struct {
    InputAction action;
    const char *description; /* mode-specific verb, e.g. "Edit" not "Confirm" */
} EditorActionHint;

typedef struct {
    const EditorActionHint *hints;
    int count;
    const char *mode_label; /* shown as "[Mode]" at the left of the hints bar */
} EditorHintTable;

/* --- Shared helpers: draw.c --- */

void draw_ui_text(Font font, const char *text, int pos_x, int pos_y, int font_size, Color color);
int measure_ui_text(Font font, const char *text, int font_size);

/* Render "[Mode] | A/Ent: Confirm | Ctrl+Z/L1+Left: Undo | ..." into out.
 * Skips entries whose action has no alternatives in the store. Truncates at a
 * separator if cap is exceeded and appends " +N" for the dropped entries.
 * Always null-terminates. Returns bytes written (excluding null). */
int editor_hint_table_render(const BindingStore *store, const EditorHintTable *table, char *out, int cap);

/* --- Per-submode hint table accessors ---
 *
 * Each table is owned by the file that handles the submode; this header
 * re-exports the accessors so the hints bar (editor/draw.c) can look up the
 * current table by submode. */
const EditorHintTable *browse_hints_table(void);
const EditorHintTable *drag_hints_table(void);
const EditorHintTable *handles_hints_table(void);
const EditorHintTable *attr_edit_hints_table(void);
const EditorHintTable *radial_hints_table(void);
const EditorHintTable *word_builder_hints_table(void);
const EditorHintTable *fuzzy_finder_hints_table(void);
const EditorHintTable *gamepad_kb_hints_table(void);
const EditorHintTable *blueprint_list_hints_table(void);
const EditorHintTable *blueprint_detail_hints_table(void);
const EditorHintTable *watch_list_hints_table(void);
const EditorHintTable *level_list_hints_table(void);
const EditorHintTable *level_detail_hints_table(void);
const EditorHintTable *tile_paint_hints_table(void);
const EditorHintTable *tile_palette_hints_table(void);
const EditorHintTable *atlas_browse_hints_table(void);
const EditorHintTable *atlas_region_edit_hints_table(void);
const EditorHintTable *anim_blueprint_list_hints_table(void);
const EditorHintTable *anim_edit_hints_table(void);
const EditorHintTable *anim_frames_hints_table(void);
const EditorHintTable *rule_blueprint_list_hints_table(void);
const EditorHintTable *rule_list_hints_table(void);
const EditorHintTable *rule_tree_hints_table(void);

/* --- Shared helpers: core.c --- */

/* AttrSection lives in editor/editor.h — it is part of EditorState's
 * stable attr-selection identity, not just a core.c implementation detail. */

typedef enum {
    ATTR_ROW_KIND_ATTR,    /* row is an attribute, index_in_section is the AttrSet index */
    ATTR_ROW_KIND_ADD,     /* row is an ADD sentinel for the section */
    ATTR_ROW_KIND_INVALID, /* row index is out of range */
} AttrRowKind;

typedef struct {
    AttrRowKind kind;
    AttrSection section;
    int index_in_section;
} AttrRow;

/* Row layout helpers. Every entity, root or child, has three sections
 * (persisted, runtime, blueprint). Child persisted attrs round-trip through
 * [level.entity.children.<tag>] (S3.3a). Each section ends in its own ADD
 * sentinel row. */
int total_attr_count(const GameState *state, const Entity *entity);
bool entity_has_persisted_section(const Entity *entity);
AttrRow attr_row_at(const GameState *state, const Entity *entity, int attr_index);

/* Resolve EditorState's stable attr identity (selected_attr_kind/section/name)
 * to a display index for `entity`, by scanning attr_row_at over every row.
 * Returns -1 if the identity is EDITOR_ATTR_SEL_NONE or no longer matches any
 * row (e.g. the named attribute was removed elsewhere). */
int editor_resolve_selected_attr_index(const GameState *state, const Entity *entity, const EditorState *editor_state);

/* --- Shared type: rule.c (rule tree row model, S5.6a) ---
 *
 * One flattened row of a Rule's read-only tree view (editor/rule.c). The
 * trigger and the rule's own conditions sit at depth 0, ahead of the action
 * tree; the action tree is then walked depth-first over its flat node pool
 * (ActionTree.nodes), each control-flow node's `children` before its
 * `else_children`, recursing into nested control flow. This is the row
 * model later Rule-mode slices (b/c/d) build leaf/structural editing on. */
typedef enum {
    RULE_TREE_ROW_TRIGGER,
    RULE_TREE_ROW_CONDITION,
    RULE_TREE_ROW_ACTION,
} RuleTreeRowKind;

typedef struct {
    RuleTreeRowKind kind;
    /* CONDITION: index into rule->conditions. ACTION: index into
     * rule->action_tree.nodes (the flat pool). TRIGGER: unused (-1). */
    int node_index;
    int depth; /* indentation depth, 0 = top-level (trigger/conditions/root actions) */
    /* ACTION rows only: true if this node was reached via its immediate
     * parent's else_children list (not inherited from an outer ancestor). */
    bool is_else_branch;
    /* ACTION rows only: true for the first row of one specific
     * else_children list traversal -- draw the "else:" divider before it.
     * Unlike is_else_branch, this never repeats for later siblings in the
     * same else list (see editor/rule.c's rule_tree_flatten_children). */
    bool is_else_start;
} RuleTreeRow;

/* Total flattened row count for `rule`: trigger (1) + rule-level conditions
 * + every node in the action tree's flat pool (S2.3) -- every pool node is
 * reachable from roots/children/else_children by construction, so no tree
 * walk is needed to compute this, just the three counts. Callers size a
 * caller-owned RuleTreeRow buffer with this before calling
 * rule_tree_flatten. */
int rule_tree_row_count(const Rule *rule);

/* Fills `out` (caller-allocated, sized to at least rule_tree_row_count(rule))
 * with one row per trigger/condition/action-node, in display order. Pure
 * function: no allocation, no GameState dependency -- callers own where the
 * buffer lives (see draw_rule_tree_panel's scratch-arena buffer, editor/rule.c). */
void rule_tree_flatten(const Rule *rule, RuleTreeRow *out);

Blueprint *find_blueprint_by_name(GameState *state, const char *name);
Attribute *attr_at_display_index(GameState *state, Entity *entity, int attr_index);
bool is_blueprint_attr(const GameState *state, const Entity *entity, int attr_index);
AttrSet *attr_section_set(GameState *state, Entity *entity, AttrSection section);
int tree_section_total(const Entity *entity, const Blueprint *blueprint);
bool tree_is_parent_row(const Entity *entity, int tree_index);
bool tree_is_add_child_row(const Entity *entity, const Blueprint *blueprint, int tree_index);
int tree_child_index(const Entity *entity, int tree_index);
int place_visible_count(int screen_height);
int find_place_blueprint_index(const GameState *state, const EditorState *editor_state);
void mark_deleted_descendants(const Level *level, bool *is_deleted, int count);

/* --- Shared helpers: child.c --- */

int find_child_entity(const Level *level, int parent_index, const char *blueprint_name, const char *tag);

/* --- Cross-file calls: attr.c (called from core.c) --- */

void dispatch_attr_type_change(GameState *state, EditorState *editor_state, int confirmed, UndoHistory *undo_history);
void dispatch_child_props(GameState *state, EditorState *editor_state, int confirmed);
/* --- Cross-file calls: attr.c (called from widgets.c) --- */

void confirm_child_tag_edit(Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history);

/* --- Shared helpers: attr.c (called from rule.c) ---
 *
 * The +/-1 / +/-10 / +/-100 hold-and-accelerate value-adjuster shape is
 * implemented three times in attr.c already (handle_attr_edit_input for
 * Attribute int/float, handle_child_offset_edit for BlueprintChild
 * offsets) — Rule mode's numeric leaf editing (repeat count, condition
 * compare_value, S5.6b) is a fourth instance of the same shape, over
 * different backing storage again. Rather than factor the whole loop out
 * (the codebase doesn't already have that abstraction and the loop body
 * is ~10 lines), only the three small, reusable, near-pure pieces are
 * exported so editor/rule.c doesn't duplicate their logic: read the
 * pressed-delta actions, read the held direction, and reset the hold
 * timers. The accelerate-and-repeat loop itself is written again in
 * rule.c, matching attr.c's own existing duplication style. */
int read_value_delta(const InputState *input, const BindingStore *bindings);
int read_held_dir(const InputState *input, const BindingStore *bindings);
void reset_attr_hold(EditorState *editor_state);

/* --- Cross-file calls: child.c --- */

void remove_blueprint_child(GameState *state, EditorState *editor_state, UndoHistory *undo_history, int child_idx);
void propagate_child_tag(GameState *state, const Blueprint *blueprint, int child_idx, const char *old_tag);
void propagate_child_offset(GameState *state, const Blueprint *blueprint, int child_idx);
void add_blueprint_child(Diag *diag,
                         GameState *state,
                         EditorState *editor_state,
                         UndoHistory *undo_history,
                         const char *child_blueprint_name,
                         TextureLookupFn texture_lookup,
                         void *texture_user_data);

/* --- Cross-file calls: blueprint.c --- */

void create_blank_blueprint(GameState *state, EditorState *editor_state, UndoHistory *undo_history, const char *name);
void duplicate_blueprint(GameState *state, EditorState *editor_state, UndoHistory *undo_history, const char *name);

/* --- Cross-file calls: widgets.c (called from core.c) --- */

void fuzzy_finder_build_items(GameState *state, EditorState *editor_state);

/* --- Cross-file calls: level.c (called from widgets.c) --- */

/* Validate `name` (non-empty, not already used by current_level or
 * other_levels), build a Level with the S5.2b defaults (640x360, floor
 * size matching, "grass.png" background, white tint, empty music,
 * next_entity_id 0, empty entities vec), append it to other_levels, and
 * activate it via level_activate so the new empty level becomes current.
 * Invalid/duplicate names toast and leave gamedata untouched. */
void create_new_level(
    Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history, const char *name);

/* Write word_builder_buf into the current level's background_tile or
 * music_name Str (whichever editing_level_string_field names), then push
 * an undo entry. Caller resets editing_level_string_field afterward. */
void confirm_level_string_edit(GameState *state, EditorState *editor_state, UndoHistory *undo_history);

/* --- Cross-file calls: atlas.c (called from widgets.c) --- */

/* Validate/stash a new atlas region's name (see atlas.c for the full
 * contract) and stage its default src rect. Returns true and expects the
 * caller to switch to EDITOR_SUB_ATLAS_REGION_EDIT; false means a toast
 * was raised and the caller should return to EDITOR_SUB_ATLAS_BROWSE. */
[[nodiscard]] bool start_new_atlas_region(GameState *state, EditorState *editor_state, const char *name);

/* --- Cross-file calls: anim.c (called from core.c) --- */

/* Entering Animation mode from the Tools radial (dispatch_radial_confirm):
 * mirrors enter_tile_mode/enter_atlas_mode's split-out-for-complexity
 * rationale. Also resolves S5.5's blueprint-picking choice: if a scene
 * entity is currently selected, its blueprint is preselected (params-edit
 * view opens directly); otherwise anim_blueprint_index lands on -1, which
 * EDITOR_SUB_ANIM_EDIT's dual duty (editor/anim.c) renders as the
 * blueprint list picker. */
void enter_anim_mode(GameState *state, EditorState *editor_state);

/* --- Cross-file calls: rule.c (called from core.c) --- */

/* Entering Rule mode from the Tools radial (dispatch_radial_confirm):
 * mirrors enter_anim_mode's blueprint-picking choice above -- if a scene
 * entity is currently selected, its blueprint is preselected (rule list
 * opens directly); otherwise rule_blueprint_index lands on -1, which
 * EDITOR_SUB_RULE_LIST's dual duty (editor/rule.c) renders as the
 * blueprint list picker. Read-only (S5.6a): no undo integration. */
void enter_rule_mode(GameState *state, EditorState *editor_state);

/* --- Rule mode leaf editing (S5.6b, editor/rule.c) ---
 *
 * Radial item counts for RADIAL_CTX_TRIGGER_TYPE/CONDITION_TYPE/ACTION_TYPE
 * (editor/editor.h) -- each radial index maps directly onto the
 * corresponding rule.h enum's declaration order, so widgets.c's
 * radial_label needs the count to bounds-check but no separate order
 * table. ACTION_TYPE_COUNT covers exactly the non-control-flow prefix
 * types (ActionType's first 22 declared values); ACTION_IF_ELSE/REPEAT/
 * FOR_EACH are the last 3 and are never offered on this radial (see
 * begin_rule_edit_for_row, editor/rule.c). */
#define RULE_TRIGGER_TYPE_COUNT 10
#define RULE_CONDITION_TYPE_COUNT 9
#define RULE_ACTION_TYPE_COUNT 22

/* Radial label for one RADIAL_CTX_CONDITION_TYPE sector: unlike
 * trigger_type_label/action_type_label, condition_type_label (rule.h)
 * collapses COND_ATTR_LT/GT/EQ down to the bare noun "attr", which would
 * make three of the nine radial sectors show the same label -- this
 * disambiguates them with the comparison operator, mirroring
 * editor/rule.c's own condition_operator used for the tree row labels.
 * Returns a raylib TextFormat ring-buffer string (safe for immediate
 * per-frame draw use only, matching every other radial_label branch). */
const char *rule_condition_radial_label(int index);

/* Called from EDITOR_SUB_RULE_TREE's CONFIRM (handle_rule_tree_input) when
 * nothing is currently staged: inspects the focused row (rule_tree_row)
 * and opens whichever first step its edit gesture needs -- a TYPE radial
 * for trigger/condition/simple-action rows, or a standalone
 * numeric/word-builder step for a repeat/for_each control-flow row. No-op
 * for if_else rows (nothing on the row itself is directly editable; its
 * predicate isn't yet a navigable row -- see DESIGN.md's Rule mode
 * roadmap) and for rows that don't resolve. */
void begin_rule_edit_for_row(GameState *state, EditorState *editor_state);

/* Dispatches a confirmed RADIAL_CTX_TRIGGER_TYPE/CONDITION_TYPE/ACTION_TYPE
 * pick (editor_state->radial_confirmed, cleared by this call) to the
 * matching staging step. Called from handle_rule_tree_input, which is only
 * ever reached from EDITOR_TOP_RULE, so any pending radial_confirmed seen
 * there is guaranteed to be one of these three rule contexts. */
void dispatch_rule_radial_confirm(GameState *state, EditorState *editor_state, UndoHistory *undo_history);

/* Current (pre-edit) text of the field rule_edit_field names for the
 * focused row -- the primary argument, or the second_argument when
 * rule_edit_field is RULE_EDIT_FIELD_SECOND_ARGUMENT. Used to prefill the
 * word builder / fuzzy-finder-NEW path so editing feels like adjusting the
 * existing value rather than starting blank. Returns "" if nothing
 * resolves. Never empty-vs-nullptr ambiguous -- always a valid C string. */
const char *rule_edit_current_argument_text(GameState *state, const EditorState *editor_state);

/* Completes one RULE_EDIT_FIELD_ARGUMENT or RULE_EDIT_FIELD_SECOND_ARGUMENT
 * step with the chosen text (from either the fuzzy finder's picked item or
 * the word builder's finished buffer). If the gesture needs a further step
 * (a two-arg action's second_argument, or a comparison condition's
 * compare_value), stages the text and opens that next step; otherwise
 * commits the whole staged gesture to the real rule data, pushes an undo
 * entry, and returns to EDITOR_SUB_RULE_TREE. Always sets
 * editor_state->sub_mode itself -- callers must not also assign it. */
void rule_edit_argument_step_complete(GameState *state,
                                      EditorState *editor_state,
                                      UndoHistory *undo_history,
                                      const char *text);

/* Completes the standalone RULE_EDIT_FIELD_FOR_EACH_BIND step: writes
 * `text` into the focused for_each node's second_argument (the loop's bind
 * name) only -- type/argument/conditions are untouched, unlike the
 * TYPE-radial-driven chain above. Pushes an undo entry. */
void commit_rule_for_each_bind(GameState *state,
                               EditorState *editor_state,
                               UndoHistory *undo_history,
                               const char *text);

/* Drives the standalone RULE_EDIT_FIELD_REPEAT_COUNT / COMPARE_VALUE
 * numeric sessions from EDITOR_SUB_ATTR_EDIT (called from attr.c's
 * handle_attr_edit_input once it sees one of those two field tags). Mirrors
 * attr.c's own handle_child_offset_edit shape (hold/accelerate via the
 * exported read_value_delta/read_held_dir/reset_attr_hold, CONFIRM commits,
 * CANCEL discards) but stages purely in editor_state->saved_attr_int and
 * only writes the real condition/node field on CONFIRM, so CANCEL never
 * mutates -- see RuleEditField's doc comment (editor/editor.h). */
void handle_rule_numeric_edit_input(
    GameState *state, EditorState *editor_state, UndoHistory *undo_history, const InputState *input, float delta_time);
