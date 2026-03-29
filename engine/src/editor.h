#ifndef EDITOR_H
#define EDITOR_H

#include "engine_context.h"
#include "game.h"
#include "input.h"
#include "level.h"
#include "raylib.h"
#include "rect.h"

#define DEBUG_BG_ALPHA 180
#define DEBUG_MARGIN 8

#define EDITOR_CAMERA_SPEED 120.0F
#define EDITOR_CROSSHAIR_HALF 6
#define HINTS_BAR_HEIGHT 28
#define HINTS_FONT_SIZE 20
#define EDITOR_PANEL_WIDTH 380
#define EDITOR_PANEL_FONT_SIZE 20
#define EDITOR_PANEL_LINE_HEIGHT 24
/* Fixed-size: 4 slots is a hard UI display limit — the watch overlay has room for
 * exactly EDITOR_WATCH_MAX entries; more would overflow the panel. */
#define EDITOR_WATCH_MAX 4
#define EDITOR_HANDLE_SIZE 4          /* corner handle square side, world pixels */
#define EDITOR_HANDLE_SPEED 60.0F     /* px/s for collision offset/size editing */
#define EDITOR_ATTR_LARGE_STEP 10     /* ±10 step for attribute value adjuster (bumpers/brackets) */
#define EDITOR_ATTR_HUGE_STEP 100     /* ±100 step for value adjuster (L2/R2 / PgDn/PgUp) */
#define EDITOR_PLACE_PAGE_SIZE 5      /* blueprint page-jump size for L1/R1 in scroll picker */
#define ATTR_REPEAT_DELAY 0.4F        /* seconds before auto-repeat starts on hold */
#define ATTR_REPEAT_PERIOD 0.1F       /* initial repeat interval (10 Hz) */
#define ATTR_REPEAT_MIN_PERIOD 0.025F /* fastest interval after acceleration (40 Hz) */
#define ATTR_REPEAT_ACCEL 4.0F        /* period halves every 1/ACCEL seconds of hold */

extern const Color debug_text_color;
extern const Color debug_bg_color;
extern const Color debug_log_color;

typedef struct {
    int key;
    int gamepad_button;
} ToggleBinding;

typedef enum {
    EDITOR_SUB_BROWSE,
    EDITOR_SUB_DRAG,
    EDITOR_SUB_HANDLES,
    EDITOR_SUB_PLACE,
    EDITOR_SUB_ATTR_EDIT, /* numeric/bool value adjustment */
} EditorSubMode;

typedef struct {
    int selected_entity_index; /* -1 = nothing selected */
    EditorSubMode sub_mode;
    Vector2 saved_position;
    Vector2 saved_col_offset;
    Vector2 saved_col_size;
    int place_blueprint_index; /* index into state->blueprints.entries */
    int selected_attr_index;   /* -1 = none; index into merged instance+blueprint list */
    float saved_attr_float;    /* original float value saved on entering ATTR_EDIT */
    int saved_attr_int;        /* original int value saved on entering ATTR_EDIT */
    bool saved_attr_bool;      /* original bool value saved on entering ATTR_EDIT */
    float attr_hold_total;     /* cumulative time held in current direction */
    float attr_hold_subtick;   /* time since last auto-repeat fire */
    int attr_hold_dir;         /* -1 / 0 / +1: direction currently held for auto-repeat */
} EditorState;

typedef struct {
    int entity_indices[EDITOR_WATCH_MAX];
    int count;
} WatchList;

bool toggle_pressed(ToggleBinding binding);
void update_editor_camera(Camera2D *camera, InputState input, float delta_time);
void draw_editor_crosshair(RectU32 game_bounds);
void draw_hints_bar(bool editor_mode, const EditorState *editor_state, const struct EngineContext *hints_ctx);
int find_nearest_entity(const Level *level, Vector2 cursor_world);
void draw_editor_highlights(const GameState *state, const EditorState *editor_state, int hover_entity_index);
void draw_editor_panel(const struct EngineContext *ctx, const GameState *state, const EditorState *editor_state);
void draw_watch_overlay(const struct EngineContext *ctx, const GameState *state, const WatchList *watches);
void draw_collision_handles(const GameState *state, const EditorState *editor_state);
void handle_browse_input(struct EngineContext *ctx,
                         GameState *state,
                         Camera2D *camera,
                         EditorState *editor_state,
                         WatchList *watches,
                         InputState input,
                         float delta_time);
void draw_place_panel(const struct EngineContext *ctx, const GameState *state, const EditorState *editor_state);
void draw_place_preview(const GameState *state, const EditorState *editor_state, Camera2D camera);
void handle_mode_transitions(const GameState *state, EditorState *editor_state);
void handle_drag_input(GameState *state, EditorState *editor_state, InputState input, float delta_time);
void handle_handle_input(
    struct EngineContext *ctx, GameState *state, EditorState *editor_state, InputState input, float delta_time);
void handle_attr_edit_input(GameState *state, EditorState *editor_state, float delta_time);

#endif
