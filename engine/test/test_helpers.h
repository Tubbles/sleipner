#pragma once

#include "alloc.h"
#include "attribute.h"
#include "blueprint.h"
#include "diag.h"
#include "editor/editor.h"
#include "entity.h"
#include "frame.h"
#include "game.h"
#include "input.h"
#include "level.h"
#include "menu.h"
#include "raylib.h"
#include "rule.h"
#include "undo.h"

/* Construct a heap-backed allocator (malloc/realloc/free). Test-only. */
Allocator allocator_heap(void);

/* Call once before running tests to initialise the heap allocator. */
void test_helpers_init(void);

/* Heap allocator for test code — initialised by test_helpers_init(). */
extern Allocator test_heap_alloc;

/* Cleanup helpers for test data structures */
void test_blueprint_table_free(BlueprintTable *table);
void test_blueprint_free(Blueprint *blueprint);
void test_level_free(Level *level);
void test_entity_free(Entity *entity);
void test_flag_set_free(FlagSet *flags);
void test_attr_set_free(AttrSet *set);

/* --- Black-box integration test fixture ---
 *
 * Sane-defaults bundle around the persistent objects production main.c
 * holds across a frame loop, so headless integration tests can drive
 * frame_update against a real GameState without re-implementing the
 * setup boilerplate. Add new fields and defaults here as new top-level
 * pieces of state appear in main.c. */
typedef struct {
    GameState state;
    UndoHistory undo_history;
    MenuState menu;
    EditorState editor_state;
    Camera2D editor_camera;
    WatchList watches;
    bool font_preview_enabled;
    bool quit_requested;
    Diag diag;
    FrameContext frame_ctx;
} TestGame;

/* Initialise a TestGame from a TOML string. Mirrors main.c's startup:
 * game_init, game_load_gamedata, undo_history_init, push the "Initial"
 * baseline undo entry, menu_init, EditorState with sentinel -1s,
 * frame_ctx wired to the struct's own fields. Returns false on any
 * underlying failure; on success the caller must pair with
 * test_game_teardown. Does not need a writable filesystem. */
[[nodiscard]] bool test_game_setup(TestGame *out, const char *toml_string);

void test_game_teardown(TestGame *game);

/* Drive one black-box frame at a 1/60s delta. Wires through
 * frame_update — same dispatch path production runs. */
void test_advance_frame(TestGame *game, InputState input);

/* Drive N consecutive frames with the same input. Useful for soaking
 * a held axis for a fixed wall-clock duration. */
void test_advance_frames(TestGame *game, InputState input, int frames);

/* Locate the display index of a named INT attribute on `entity`, by
 * walking attr_at_display_index. Returns -1 on miss. Mirrors the
 * iteration order the editor uses when the user navigates the
 * attribute panel. */
int test_find_int_attr_display_index(GameState *state, Entity *entity, const char *name);

/* Read the player entity's named INT attribute. Returns the fallback
 * 0 if the attribute is missing — callers that want to assert on the
 * presence of the attribute should use test_find_int_attr_display_index
 * directly. */
int test_player_int_attr(GameState *state, const char *name);
