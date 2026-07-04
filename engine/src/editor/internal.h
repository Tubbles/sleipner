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
