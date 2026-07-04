#pragma once

#include "diag.h"
#include "game.h"
#include "input.h"
#include "keyboard_widget.h"
#include "level.h"
#include "raylib.h"
#include "rect.h"
#include "tileset.h"
#include "undo.h"

typedef struct {
    int width;
    int height;
} ScreenSize;

#define DEBUG_BG_ALPHA 180
#define DEBUG_MARGIN 8

#define EDITOR_CAMERA_SPEED 120.0F
#define EDITOR_CROSSHAIR_HALF 6
#define HINTS_BAR_HEIGHT 40
#define HINTS_FONT_SIZE 32
#define EDITOR_PANEL_WIDTH 380
#define EDITOR_PANEL_FONT_SIZE 32
#define EDITOR_PANEL_LINE_HEIGHT 36
/* Fixed-size: 4 slots is a hard UI display limit — the watch overlay has room for
 * exactly EDITOR_WATCH_MAX entries; more would overflow the panel. */
#define EDITOR_WATCH_MAX 4
#define EDITOR_HANDLE_SIZE 4                /* corner handle square side, world pixels */
#define EDITOR_HANDLE_SPEED 60.0F           /* px/s for collision/atlas-region offset/size editing */
#define EDITOR_ATLAS_ZOOM 4.0F              /* scale factor for the atlas texture view (texture px -> world px) */
#define EDITOR_ATLAS_DEFAULT_REGION_SIZE 32 /* default new-region width/height, texture px, before clamping */
#define EDITOR_ATTR_LARGE_STEP 10           /* ±10 step for attribute value adjuster (bumpers/brackets) */
#define EDITOR_ATTR_HUGE_STEP 100           /* ±100 step for value adjuster (L2/R2 / PgDn/PgUp) */
#define EDITOR_TOOLS_ITEM_COUNT 11          /* items in the RADIAL_CTX_TOOLS picker */
#define EDITOR_TOOLS_WATCH_LIST_INDEX 5     /* RADIAL_CTX_TOOLS slot for "Watch list" */
#define EDITOR_TOOLS_LEVELS_INDEX 6         /* RADIAL_CTX_TOOLS slot for "Levels" */
#define EDITOR_TOOLS_TILE_INDEX 7           /* RADIAL_CTX_TOOLS slot for "Tiles" */
#define EDITOR_TOOLS_ATLAS_INDEX 8          /* RADIAL_CTX_TOOLS slot for "Atlas" */
#define EDITOR_TOOLS_ANIM_INDEX 9           /* RADIAL_CTX_TOOLS slot for "Animation" */
#define EDITOR_TOOLS_RULE_INDEX 10          /* RADIAL_CTX_TOOLS slot for "Rules" */
#define EDITOR_PLACE_PAGE_SIZE 5            /* blueprint page-jump size for L1/R1 in scroll picker */
#define ATTR_REPEAT_DELAY 0.4F              /* seconds before auto-repeat starts on hold */
#define ATTR_REPEAT_PERIOD 0.1F             /* initial repeat interval (10 Hz) */
#define ATTR_REPEAT_MIN_PERIOD 0.025F       /* fastest interval after acceleration (40 Hz) */
#define ATTR_REPEAT_ACCEL 4.0F              /* period halves every 1/ACCEL seconds of hold */
#define RADIAL_INNER_RADIUS 50.0F           /* inner donut radius, screen px */
#define RADIAL_OUTER_RADIUS 140.0F          /* outer donut radius, screen px */
#define RADIAL_STICK_THRESHOLD 0.3F         /* min stick magnitude to register a sector */
#define RADIAL_FONT_SIZE 32                 /* label font size */
#define RADIAL_BG_PADDING 4.0F              /* background circle padding beyond outer radius */
#define RADIAL_FULL_CIRCLE_DEG 360.0F       /* degrees in a full circle */
#define RADIAL_NORTH_OFFSET_DEG 90.0F       /* rotation offset so top is north (12 o'clock) */
#define RADIAL_DEG_TO_RAD 0.01745329252F    /* multiplier to convert degrees to radians */
#define WORD_BUILDER_BUF_SIZE 256           /* max length of word builder output */
/* Attribute names created through the editor are typed via the word
 * builder, so WORD_BUILDER_BUF_SIZE is the only enforced bound on an
 * editor-authored attr name. Reuse it here so the stable-identity
 * selection below can never truncate a name it will later need to
 * match exactly. */
#define EDITOR_ATTR_NAME_MAX WORD_BUILDER_BUF_SIZE
#define WORD_BUILDER_PAGE_SIZE 5 /* page-jump size for L1/R1 in word builder */
#define FUZZY_FINDER_PAGE_SIZE 5 /* page-jump size for L1/R1 in name picker */
#define TOAST_DURATION 2.0F      /* seconds before toast fades out */
#define TOAST_FADE_TIME 0.5F     /* seconds of fade-out at the end */
#define TOAST_FONT_SIZE 32       /* toast text font size */
#define ALPHA_MAX 255.0F         /* max alpha value for color byte conversion */
#define RULE_TREE_INDENT_PX 20   /* screen px added to the panel's left margin per tree depth level */

extern const Color debug_text_color;
extern const Color debug_bg_color;
extern const Color debug_log_color;

typedef enum {
    EDITOR_TOP_SCENE,     /* default: entity-focused editing */
    EDITOR_TOP_BLUEPRINT, /* blueprint-focused editing */
    EDITOR_TOP_LEVEL,     /* level list: view all levels, switch the active one in memory */
    EDITOR_TOP_TILE,      /* tile grid: paint the current level's ground/overlay layers (S5.3b, D36) */
    EDITOR_TOP_ATLAS,     /* atlas region browser: define named sprite regions on a texture (S5.4b, D37) */
    EDITOR_TOP_ANIM,      /* animation params + frame scrubber for a blueprint (S5.5, D20) */
    EDITOR_TOP_RULE,      /* read-only rule tree browser for a blueprint's rules (S5.6a) */
} EditorTopMode;

/* Identifies which Level string field the word builder is currently
 * naming, when EditorState.editing_level_string_field != NONE. Read by
 * both editor/level.c (opens the word builder) and editor/widgets.c
 * (word builder CONFIRM dispatch, a different file). NONE must be the
 * zero value so the struct-literal EditorState resets in main.c /
 * test_helpers.c (which don't list this field) leave it cleared. */
typedef enum {
    LEVEL_STRING_FIELD_NONE,
    LEVEL_STRING_FIELD_BACKGROUND_TILE,
    LEVEL_STRING_FIELD_MUSIC,
} LevelStringField;

typedef enum {
    /* Grab / Place / Handles / Delete / Blueprints / Watch list / Levels / Tiles / Atlas / Animation / Rules — 11 items
     */
    RADIAL_CTX_TOOLS,
    RADIAL_CTX_ATTR_TYPE,   /* Float / Int / Bool / String — 4 items */
    RADIAL_CTX_CHILD_PROPS, /* Tag / Offset X / Offset Y — 3 items */
    /* Rule mode leaf editing (S5.6b, editor/rule.c). Each maps its radial
     * index directly onto the corresponding enum's declaration order in
     * rule.h (TriggerType/ConditionType/ActionType), so no separate
     * order table is needed — see RULE_TRIGGER_TYPE_COUNT /
     * RULE_CONDITION_TYPE_COUNT / RULE_ACTION_TYPE_COUNT (editor/internal.h). */
    RADIAL_CTX_TRIGGER_TYPE,   /* TriggerType — 10 items */
    RADIAL_CTX_CONDITION_TYPE, /* ConditionType — 9 items */
    RADIAL_CTX_ACTION_TYPE,    /* ActionType (non-control-flow prefix) — 22 items */
} RadialContext;

typedef enum {
    EDITOR_SUB_BROWSE,
    EDITOR_SUB_DRAG,
    EDITOR_SUB_HANDLES,
    EDITOR_SUB_PLACE,
    EDITOR_SUB_ATTR_EDIT,    /* numeric/bool value adjustment */
    EDITOR_SUB_RADIAL,       /* generic N-item radial picker overlay */
    EDITOR_SUB_WORD_BUILDER, /* string attribute editing via vocabulary picker */
    EDITOR_SUB_FUZZY_FINDER, /* name picker for existing gamedata names */
    EDITOR_SUB_GAMEPAD_KB,   /* two-level radial character picker */
    EDITOR_SUB_WATCH_LIST,   /* scroll picker over the watch list; CONFIRM removes the focused entry */
    EDITOR_SUB_TILE_PAINT,   /* cursor over the current level's tile grid; CONFIRM paints, EDITOR_DELETE erases */
    EDITOR_SUB_TILE_PALETTE, /* scroll picker over the gamedata tileset; CONFIRM selects the paint tile */
    /* Texture picker (atlas_texture_index < 0) or region picker for the chosen
     * texture (atlas_texture_index >= 0), including the "+ NEW REGION" row
     * (S5.4b, D37). */
    EDITOR_SUB_ATLAS_BROWSE,
    /* HANDLES-style dual-stick drag on the zoomed texture view: sets the
     * in-progress region's src rect (atlas_edit_src). CONFIRM commits to
     * gamedata.atlas_regions, CANCEL discards. */
    EDITOR_SUB_ATLAS_REGION_EDIT,
    /* Blueprint list picker (anim_blueprint_index < 0) or the four anim_*
     * params rows -- frames/size/speed/row -- for the selected blueprint
     * (anim_blueprint_index >= 0), S5.5/D20. Value-adjuster editing via
     * ACTION_ATTR_INC/DEC_1/10, same immediate-commit-per-press pattern as
     * Level mode's detail rows (editor/level.c). ACTION_TAB_PREV/NEXT
     * switches to the frame scrubber below. */
    EDITOR_SUB_ANIM_EDIT,
    /* Frame scrubber: ACTION_NAV_LEFT/RIGHT steps anim_frame_index over
     * [0, anim_frames), with a live src-rect preview of the blueprint's
     * texture in the side panel (editor/anim.c). ACTION_TAB_PREV/NEXT (or
     * CANCEL) returns to EDITOR_SUB_ANIM_EDIT. */
    EDITOR_SUB_ANIM_FRAMES,
    /* Blueprint list picker (rule_blueprint_index < 0) or the selected
     * blueprint's rule list (rule_blueprint_index >= 0), S5.6a -- same dual
     * duty as EDITOR_SUB_ANIM_EDIT/anim_blueprint_index above. CONFIRM on a
     * focused rule row opens EDITOR_SUB_RULE_TREE for that rule. Read-only:
     * no editing, no undo integration. */
    EDITOR_SUB_RULE_LIST,
    /* Read-only, navigable indented tree for one rule: trigger, conditions,
     * then a depth-first walk of the action tree (editor/rule.c's
     * rule_tree_flatten). rule_tree_row is the flattened-row cursor.
     * CANCEL returns to EDITOR_SUB_RULE_LIST. */
    EDITOR_SUB_RULE_TREE,
} EditorSubMode;

/* Which tile layer NAV/paint/erase currently act on in EDITOR_SUB_TILE_PAINT
 * (S5.3b, D36). GROUND must be the zero value so a zero-initialized/reset
 * EditorState (all three reset call sites) defaults to painting the ground
 * layer. ACTION_TAB_NEXT toggles between the two. */
typedef enum {
    TILE_LAYER_GROUND,
    TILE_LAYER_OVERLAY,
} TileLayer;

/* The editor attr panel for an entity is split into three sections:
 * persisted (saved to TOML), runtime (live read path, mutated by rules),
 * and blueprint (shared defaults from the blueprint table). Child persisted
 * attrs round-trip through [level.entity.children.<tag>] (S3.3a), so child
 * entities show the persisted section too.
 * Part of EditorState's stable attr-selection identity below, so it lives
 * in the public editor header rather than editor/internal.h. */
typedef enum {
    ATTR_SECTION_PERSISTED,
    ATTR_SECTION_RUNTIME,
    ATTR_SECTION_BLUEPRINT,
} AttrSection;

/* Identity kind for the entity attr panel's selection (see
 * EditorState.selected_attr_kind below). */
typedef enum {
    EDITOR_ATTR_SEL_NONE,  /* nothing selected in the attr panel */
    EDITOR_ATTR_SEL_NAMED, /* selected_attr_name identifies a live attribute in selected_attr_section */
    EDITOR_ATTR_SEL_ADD,   /* selected the ADD sentinel row of selected_attr_section */
} EditorAttrSelKind;

/* Which step of a Rule mode leaf-edit gesture (S5.6b, editor/rule.c) is
 * currently in flight. The whole gesture (type, then argument, then maybe
 * a second argument or compare_value) is staged in EditorState.rule_edit_*
 * below and only committed to the real rule/condition/node once the last
 * needed step confirms — so CANCEL at any point leaves the original data
 * untouched, even mid-chain. NONE must be the zero value so a reset
 * EditorState has no rule edit pending. */
typedef enum {
    RULE_EDIT_FIELD_NONE,
    RULE_EDIT_FIELD_TYPE,            /* trigger/condition/action TYPE radial is open */
    RULE_EDIT_FIELD_ARGUMENT,        /* primary argument/target text entry is open */
    RULE_EDIT_FIELD_SECOND_ARGUMENT, /* action's second_argument text entry, as the tail of a type+argument chain */
    RULE_EDIT_FIELD_FOR_EACH_BIND,   /* standalone: only a for_each node's bind name changes, type/argument untouched */
    RULE_EDIT_FIELD_COMPARE_VALUE,   /* condition's compare_value, as the tail of a type+argument chain */
    RULE_EDIT_FIELD_REPEAT_COUNT,    /* standalone: only a repeat node's count changes */
} RuleEditField;

typedef struct {
    EditorTopMode top_mode;
    int selected_entity_id; /* -1 = nothing selected; stable Entity.id, resolved to an
                             * index via level_find_entity_by_id at point of use. Survives
                             * undo/reload/delete instead of going stale like a raw index. */
    EditorSubMode sub_mode;
    Vector2 saved_position;
    Vector2 saved_col_offset;
    Vector2 saved_col_size;
    int place_blueprint_index; /* index into state->gamedata.blueprints.entries */
    /* Stable identity for the entity attr panel selection, resolved to a display
     * index each frame via editor_resolve_selected_attr_index(). Replaces a raw
     * "index into merged instance+blueprint list", which went stale whenever the
     * row layout shifted (attr added/removed elsewhere, undo, reload). */
    EditorAttrSelKind selected_attr_kind;
    AttrSection selected_attr_section;             /* section the identity refers to (kind != NONE) */
    char selected_attr_name[EDITOR_ATTR_NAME_MAX]; /* attr name for EDITOR_ATTR_SEL_NAMED; unused otherwise */
    float saved_attr_float;                        /* original float value saved on entering ATTR_EDIT */
    int saved_attr_int;                            /* original int value saved on entering ATTR_EDIT */
    bool saved_attr_bool;                          /* original bool value saved on entering ATTR_EDIT */
    float attr_hold_total;                         /* cumulative time held in current direction */
    float attr_hold_subtick;                       /* time since last auto-repeat fire */
    int attr_hold_dir;                             /* -1 / 0 / +1: direction currently held for auto-repeat */
    int radial_selected;                           /* -1 = center/none; 0..N-1 = highlighted sector */
    int radial_confirmed;                          /* -1 = no pending; >=0 = confirmed index (read+cleared in browse) */
    int radial_item_count;                         /* N items in the current picker */
    RadialContext radial_context;                  /* which context opened the picker */
    int word_builder_scroll;                       /* scroll index in vocabulary list (0 = DONE) */
    int word_builder_len;                          /* current built string length */
    char word_builder_buf[WORD_BUILDER_BUF_SIZE];  /* current built string (null-terminated) */
    float toast_timer;                             /* seconds remaining for toast display */
    Strv toast_text;                               /* current toast message (non-owning, points into undo arena) */
    int fuzzy_finder_scroll;                       /* selected index (0 = "[ NEW... ]") */
    const char **fuzzy_finder_items;               /* sorted unique name pointers (gamedata_arena) */
    int fuzzy_finder_item_count;                   /* number of names (excludes the NEW sentinel) */
    KeyboardWidget word_builder_kb;                /* reused two-level radial keyboard for word builder typing */
    bool adding_attr;                              /* true when fuzzy finder is open for adding a runtime attribute */
    bool adding_persisted_attr;                    /* true when fuzzy finder is open for adding a persisted attribute */
    int selected_tree_index;                       /* -1 = not in tree section; >=0 = parent/child/ADD CHILD */
    bool editing_child_tag;                        /* word builder is editing a blueprint child tag */
    bool editing_child_offset;                     /* attr_edit is editing a blueprint child offset */
    int child_edit_axis;                           /* 0=x, 1=y — which offset axis */
    int child_edit_index;                          /* which child in blueprint->children is being edited */
    bool adding_child;                             /* true when fuzzy finder is open for adding a blueprint child */
    int blueprint_list_scroll;                     /* scroll position in blueprint list view */
    int selected_blueprint_index;                  /* -1 = list view; >=0 = detail view */
    int blueprint_attr_index;                      /* -1 = none; index into blueprint attrs (detail view) */
    int blueprint_tree_index;                      /* -1 = not in tree; >=0 = child/ADD CHILD row */
    bool adding_blueprint_attr;                    /* fuzzy finder is adding a blueprint-level attr */
    bool creating_blueprint;                       /* word builder is naming a new blueprint */
    bool duplicating_blueprint;                    /* word builder is naming a duplicate blueprint */
    int watch_list_scroll;                         /* focused index into the watch list picker (0 = first entry) */
    int level_list_scroll;             /* focused index into the level list: 0 = current_level, N = other_levels[N-1] */
    bool level_switch_confirm_pending; /* dirty-check gate: CONFIRM once more switches despite unsaved changes */
    bool creating_level;               /* word builder is naming a new level */
    bool level_detail_open;            /* true = showing the current level's metadata rows, false = the level list */
    int level_detail_row;              /* focused row within the level detail view (see LevelDetailRow in level.c) */
    LevelStringField editing_level_string_field; /* word builder is editing this Level string field, or NONE */
    int selected_tile_id;        /* tile id painted by CONFIRM in TILE_PAINT; 0 = erase (D36 "empty" tile) */
    int tile_cursor_col;         /* paint cursor column, [0, tiles_wide) */
    int tile_cursor_row;         /* paint cursor row, [0, tiles_high) */
    TileLayer tile_active_layer; /* which layer NAV/paint/erase currently act on */
    int tile_palette_scroll;     /* focused tileset id - 1 in TILE_PALETTE (ids run 1..tileset.count-1) */
    /* Atlas mode (S5.4b, D37). atlas_texture_index mirrors
     * selected_blueprint_index's double duty: -1 = texture-picker view,
     * >=0 = index into assets.textures, drilled into that texture's region
     * list. atlas_texture_scroll is the texture-picker cursor, tracked
     * independently so it survives drilling in and out (same relationship
     * blueprint_list_scroll has to selected_blueprint_index). */
    int atlas_texture_scroll;
    int atlas_texture_index;
    int atlas_region_scroll;    /* focused row in the region list; == region count is the "+ NEW REGION" sentinel */
    int atlas_region_index;     /* index into gamedata.atlas_regions being edited in REGION_EDIT; -1 = new region */
    bool creating_atlas_region; /* word builder is naming a new atlas region */
    char atlas_region_pending_name[EDITOR_ATTR_NAME_MAX]; /* stashed name between word-builder DONE and commit */
    Rectangle atlas_edit_src; /* staging src rect (texture px) being dragged in REGION_EDIT */
    /* Animation mode (S5.5, D20). anim_blueprint_index mirrors
     * atlas_texture_index's "-1 = list, >=0 = selected" double duty: -1
     * shows the blueprint list picker (dual duty within
     * EDITOR_SUB_ANIM_EDIT, editor/anim.c), >=0 indexes
     * gamedata.blueprints.entries for the params-edit rows.
     * anim_blueprint_scroll is the list cursor, tracked independently so it
     * survives drilling in/out (same relationship atlas_texture_scroll has
     * to atlas_texture_index). anim_edit_row focuses one of the four anim_*
     * attr rows (frames/size/speed/row) in the params view. anim_frame_index
     * is the scrub cursor in EDITOR_SUB_ANIM_FRAMES, clamped to
     * [0, anim_frames) (or 0 if anim_frames <= 0). */
    int anim_blueprint_index;
    int anim_blueprint_scroll;
    int anim_edit_row;
    int anim_frame_index;
    /* Rule mode (S5.6a). rule_blueprint_index mirrors anim_blueprint_index's
     * dual duty: -1 shows the blueprint list picker (dual duty within
     * EDITOR_SUB_RULE_LIST, editor/rule.c), >=0 indexes
     * gamedata.blueprints.entries and shows that blueprint's rule list
     * instead. rule_blueprint_scroll is the list cursor, tracked
     * independently so it survives drilling in/out (same relationship
     * atlas_texture_scroll has to atlas_texture_index). rule_list_scroll
     * focuses one row of the selected blueprint's rules vec; read-only Rule
     * mode has no "+ ADD RULE" sentinel row, so it doubles as "which rule is
     * open" in EDITOR_SUB_RULE_TREE (no separate field needed). rule_tree_row
     * is the flattened-row cursor within EDITOR_SUB_RULE_TREE (see
     * rule_tree_flatten, editor/rule.c), clamped to
     * [0, rule_tree_row_count(rule)). */
    int rule_blueprint_index;
    int rule_blueprint_scroll;
    int rule_list_scroll;
    int rule_tree_row;
    /* Generic return target for the shared transient-picker submodes
     * (RADIAL/WORD_BUILDER/FUZZY_FINDER/ATTR_EDIT): the sub_mode to
     * restore once the picker closes. Needed because those submodes are
     * reused from top modes whose own "resting" state is not
     * EDITOR_SUB_BROWSE (Rule mode's EDITOR_SUB_RULE_TREE, S5.6b).
     * Defaults to EDITOR_SUB_BROWSE (the zero value), matching every
     * pre-existing call site, which already assumed BROWSE implicitly and
     * never sets this field. Rule mode's step-opening helpers
     * (editor/rule.c) set it to EDITOR_SUB_RULE_TREE before opening a
     * picker; every consumer resets it back to EDITOR_SUB_BROWSE right
     * after reading it, so it never leaks into an unrelated later
     * picker session. */
    EditorSubMode return_sub_mode;
    /* Rule mode leaf editing (S5.6b, editor/rule.c). rule_edit_field
     * drives which shared submode's completion routes where; the pending_*
     * fields stage the in-flight gesture's choices (see RuleEditField's
     * doc comment above) until the gesture's last step commits them
     * atomically. rule_edit_pending_type is an int rather than one of
     * TriggerType/ConditionType/ActionType because its meaning depends on
     * the focused row's kind (rule_tree_row), re-derived at commit time. */
    RuleEditField rule_edit_field;
    int rule_edit_pending_type;
    char rule_edit_pending_argument[EDITOR_ATTR_NAME_MAX];
    char rule_edit_pending_second_argument[EDITOR_ATTR_NAME_MAX];
} EditorState;

typedef struct {
    int watch_ids[EDITOR_WATCH_MAX]; /* stable Entity.id values, not indices */
    int count;
} WatchList;

void update_editor_camera(Camera2D *camera, const InputState *input, const BindingStore *bindings, float delta_time);
void draw_editor_crosshair(RectU32 game_bounds);
void draw_hints_bar(bool editor_mode,
                    const EditorState *editor_state,
                    const BindingStore *bindings,
                    bool is_dirty,
                    ScreenSize screen,
                    Font ui_font);
void draw_toast(const EditorState *editor_state, ScreenSize screen, Font ui_font);
int find_nearest_entity(const Level *level, Vector2 cursor_world);
void draw_editor_highlights(const GameState *state, const EditorState *editor_state, int hover_entity_index);
void draw_editor_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state);
void draw_watch_overlay(ScreenSize screen, const GameState *state, const WatchList *watches);
void draw_collision_handles(const GameState *state, const EditorState *editor_state);
void handle_browse_input(GameState *state,
                         Camera2D *camera,
                         EditorState *editor_state,
                         WatchList *watches,
                         UndoHistory *undo_history,
                         InputState input,
                         float delta_time);
void draw_place_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state);
void draw_place_preview(const GameState *state, const EditorState *editor_state, Camera2D camera);
void handle_mode_transitions(GameState *state, EditorState *editor_state, const InputState *input);
void handle_drag_input(
    GameState *state, EditorState *editor_state, UndoHistory *undo_history, InputState input, float delta_time);
void handle_handle_input(
    GameState *state, EditorState *editor_state, UndoHistory *undo_history, InputState input, float delta_time);
void handle_attr_edit_input(
    GameState *state, EditorState *editor_state, UndoHistory *undo_history, const InputState *input, float delta_time);
void draw_radial_picker(ScreenSize screen, const EditorState *editor_state, Font ui_font);
void handle_radial_input(EditorState *editor_state, const InputState *input, const BindingStore *bindings);
void handle_word_builder_input(
    Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history, const InputState *input);
void draw_word_builder_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state);
void handle_fuzzy_finder_input(Diag *diag,
                               GameState *state,
                               EditorState *editor_state,
                               UndoHistory *undo_history,
                               TextureLookupFn texture_lookup,
                               void *texture_user_data,
                               const InputState *input);
void draw_fuzzy_finder_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state);
void handle_gamepad_kb_input(EditorState *editor_state, const InputState *input, const BindingStore *bindings);
void draw_gamepad_kb(ScreenSize screen, const EditorState *editor_state, Font ui_font);
void handle_blueprint_browse_input(GameState *state,
                                   EditorState *editor_state,
                                   UndoHistory *undo_history,
                                   const InputState *input);
void draw_blueprint_list_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state);
void draw_blueprint_detail_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state);
void handle_watch_list_input(EditorState *editor_state,
                             WatchList *watches,
                             const InputState *input,
                             const BindingStore *bindings);
void draw_watch_list_panel(ScreenSize screen,
                           const GameState *state,
                           const EditorState *editor_state,
                           const WatchList *watches);
void handle_level_browse_input(
    Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history, const InputState *input);
void draw_level_list_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state);
void draw_level_detail_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state);
void handle_tile_paint_input(GameState *state,
                             EditorState *editor_state,
                             UndoHistory *undo_history,
                             const InputState *input);
void handle_tile_palette_input(EditorState *editor_state,
                               const vec_tileset_entry *tileset,
                               const InputState *input,
                               const BindingStore *bindings);
void draw_tile_paint_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state);
void draw_tile_palette_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state);
void draw_tile_paint_cursor(const EditorState *editor_state);
void handle_atlas_browse_input(GameState *state, Camera2D *camera, EditorState *editor_state, const InputState *input);
void handle_atlas_region_edit_input(
    GameState *state, EditorState *editor_state, UndoHistory *undo_history, InputState input, float delta_time);
void draw_atlas_texture_list_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state);
void draw_atlas_region_list_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state);
void draw_atlas_region_edit_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state);
void draw_atlas_texture_view(const GameState *state, const EditorState *editor_state);
void handle_anim_edit_input(GameState *state,
                            EditorState *editor_state,
                            UndoHistory *undo_history,
                            const InputState *input);
void handle_anim_frames_input(GameState *state, EditorState *editor_state, const InputState *input);
void draw_anim_panel(ScreenSize screen, const GameState *state, const EditorState *editor_state);
void handle_rule_list_input(GameState *state, EditorState *editor_state, const InputState *input);
void handle_rule_tree_input(GameState *state,
                            EditorState *editor_state,
                            UndoHistory *undo_history,
                            const InputState *input);
/* Non-const state (unlike its sibling draw_*_panel functions): the tree view
 * flattens the rule's action tree into a scratch-arena buffer to render it
 * (see rule_tree_flatten, editor/rule.c), which needs a mutable Arena. The
 * blueprint-list and rule-list views this also dispatches to only read
 * fixed-shape data and don't need the buffer. */
void draw_rule_panel(ScreenSize screen, GameState *state, const EditorState *editor_state);
