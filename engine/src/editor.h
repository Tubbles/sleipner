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
#define HINTS_BAR_HEIGHT 24
#define HINTS_FONT_SIZE 16
#define EDITOR_PANEL_WIDTH 380
#define EDITOR_PANEL_FONT_SIZE 16
#define EDITOR_PANEL_LINE_HEIGHT 20
/* Fixed-size: 4 slots is a hard UI display limit — the watch overlay has room for
 * exactly EDITOR_WATCH_MAX entries; more would overflow the panel. */
#define EDITOR_WATCH_MAX 4

extern const Color debug_text_color;
extern const Color debug_bg_color;
extern const Color debug_log_color;

typedef struct {
    int key;
    int gamepad_button;
} ToggleBinding;

typedef struct {
    int selected_entity_index; /* -1 = nothing selected */
} EditorState;

typedef struct {
    int entity_indices[EDITOR_WATCH_MAX];
    int count;
} WatchList;

bool toggle_pressed(ToggleBinding binding);
void update_editor_camera(Camera2D *camera, InputState input, float delta_time);
void draw_editor_crosshair(RectU32 game_bounds);
void draw_hints_bar(bool editor_mode, const struct EngineContext *hints_ctx);
int find_nearest_entity(const Level *level, Vector2 cursor_world);
void draw_editor_highlights(const GameState *state, const EditorState *editor_state, int hover_entity_index);
void draw_editor_panel(const struct EngineContext *ctx, const GameState *state, const EditorState *editor_state);
void draw_watch_overlay(const struct EngineContext *ctx, const GameState *state, const WatchList *watches);

#endif
