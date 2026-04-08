#pragma once

/* Internal header for editor split files. NOT part of the public API. */

#include "raylib.h"

#include "attribute.h"
#include "blueprint.h"
#include "diag.h"
#include "editor/editor.h"
#include "entity.h"
#include "game.h"
#include "level.h"
#include "undo.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --- Shared helpers: draw.c --- */

void draw_ui_text(Font font, const char *text, int pos_x, int pos_y, int font_size, Color color);
int measure_ui_text(Font font, const char *text, int font_size);

/* --- Shared helpers: core.c --- */

Blueprint *find_blueprint_by_name(GameState *state, const char *name);
int total_attr_count(const GameState *state, const Entity *entity);
Attribute *attr_at_display_index(GameState *state, Entity *entity, int attr_index);
bool is_blueprint_attr(const Entity *entity, int attr_index);
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
void propagate_collision_to_entities(GameState *state, const Blueprint *blueprint);

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

/* --- Cross-file calls: widgets.c (called from core.c) --- */

void fuzzy_finder_build_items(GameState *state, EditorState *editor_state);
