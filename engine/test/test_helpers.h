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
#include "settings.h"
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
    SettingsState settings;
    EditorState editor_state;
    Camera2D editor_camera;
    WatchList watches;
    bool font_preview_enabled;
    bool quit_requested;
    Diag diag;
    FrameContext frame_ctx;
    /* Held TOML string. The frame_ctx.level_loader_fn re-loads from
     * here on transitions, mirroring how production main.c re-reads
     * gamedata.toml from disk. */
    const char *toml_string;
    /* Populated by the recording preferences_save_fn fake wired in
     * test_game_setup_with_level: incremented on every invocation, and
     * the data_dir the fake observed at save time. Lets tests assert
     * the save was actually invoked with the committed value, not just
     * that save_preferences_requested was raised and consumed. */
    int preferences_save_count;
    char saved_data_dir[512];
} TestGame;

/* Initialise a TestGame from a TOML string. Mirrors main.c's startup:
 * game_init, game_load_gamedata, undo_history_init, push the "Initial"
 * baseline undo entry, menu_init, EditorState with sentinel -1s,
 * frame_ctx wired to the struct's own fields. Returns false on any
 * underlying failure; on success the caller must pair with
 * test_game_teardown. Does not need a writable filesystem.
 *
 * test_game_setup loads the level marked default in the TOML.
 * test_game_setup_with_level loads the named level instead, for tests
 * that need to exercise a non-default starting level out of the same
 * fixture. Passing level_name = nullptr is equivalent to test_game_setup. */
[[nodiscard]] bool test_game_setup(TestGame *out, const char *toml_string);
[[nodiscard]] bool test_game_setup_with_level(TestGame *out, const char *toml_string, const char *level_name);

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

/* Locate the first entity in the current level whose blueprint name
 * matches `blueprint_name`. Returns nullptr if none found. Lifts the
 * raw `state.gamedata.current_level.entities.data[N]` index access
 * out of test bodies. */
Entity *test_find_entity_by_blueprint(GameState *state, const char *blueprint_name);

/* Count entities in the current level with the given blueprint name. */
int test_count_entities_by_blueprint(GameState *state, const char *blueprint_name);

/* Compute the same collision Rectangle the engine uses for collision
 * queries, by resolving the entity's blueprint defaults and applying
 * collision_offset / collision_size. Reach for this instead of
 * entity_resolve_defaults + entity_collision_rect in test bodies. */
Rectangle test_entity_collision_rect(GameState *state, const Entity *entity);
