#pragma once

#include "alloc.h"
#include "attribute.h"
#include "blueprint.h"
#include "diag.h"
#include "discovery_screen.h"
#include "editor/editor.h"
#include "entity.h"
#include "frame.h"
#include "game.h"
#include "input.h"
#include "inventory_screen.h"
#include "level.h"
#include "menu.h"
#include "raylib.h"
#include "rule.h"
#include "save_screen.h"
#include "settings.h"
#include "undo.h"

/* Buffer size for test_recording_gamedata_save's in-memory TOML emit.
 * main.c's own MAX_GAMEDATA_SIZE isn't reachable here (main.c isn't
 * linked into the test binary, see test_level_loader below); this is a
 * separate constant sized the same way for test fixtures, which are
 * far smaller than real gamedata.toml. */
#define TEST_MAX_GAMEDATA_SIZE (256UL * 1024)

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
    InventoryScreen inventory;
    SaveScreen save_screen;
    DiscoveryScreen discovery_screen;
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
    /* Populated by test_recording_gamedata_save (see below). Unlike the
     * preferences fake, this is NOT wired into frame_ctx.save_fn by
     * default — tests that need it set frame_ctx.save_fn explicitly, so
     * other tests keep the null (silent no-op) SAVE path unaffected. */
    int gamedata_save_count;
    char saved_gamedata_buf[TEST_MAX_GAMEDATA_SIZE];
    int saved_gamedata_length;
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

/* Simulate a hot-reload event: production's poll_hot_reload calls
 * load_gamedata (nullptr level_name) whenever gamedata.toml's mtime
 * changes on disk. The mtime polling and disk read are I/O plumbing
 * already excluded from headless tests (see test_level_loader above);
 * this drives the same game_load_gamedata call against the fixture's
 * in-memory TOML instead. */
[[nodiscard]] bool test_trigger_hot_reload(TestGame *game);

/* MenuRestoreFn fake mirroring main.c's menu_dispatch_restore: reloads
 * gamedata from the held toml_string (nullptr level_name, matching
 * production's disk-mtime-triggered reload) and resets progression via
 * game_reset_progression. main.c itself isn't linked into the test
 * binary, so tests wire this into frame_ctx.restore_fn instead, the
 * same way test_recording_preferences_save stands in for
 * dispatch_save_preferences. */
void test_restore_fn(
    Diag *diag, GameState *state, EditorState *editor_state, WatchList *watches, UndoHistory *undo_history);

/* MenuSaveFn fake for Level-mode round-trip tests: builds the same
 * current_level + other_levels combined array main.c's save_gamedata
 * builds, emits it via toml_emit_gamedata into game->saved_gamedata_buf
 * (no filesystem access), and marks undo history saved on success. Not
 * wired into test_game_setup_with_level's default frame_ctx (which
 * keeps save_fn nullptr, matching all other tests) — tests that exercise
 * the menu SAVE path against real gamedata content set
 * game.frame_ctx.save_fn = test_recording_gamedata_save explicitly. */
void test_recording_gamedata_save(Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history);

/* Compute the left-stick (x, y) at the midpoint of RADIAL_CTX_TOOLS sector
 * `item_index`, given the CURRENT EDITOR_TOOLS_ITEM_COUNT, build an
 * InputState with that stick position plus a CONFIRM press, and advance one
 * frame — replacing the "InputState radial_confirm = {0}; set_gp_axis x2;
 * press CONFIRM; advance_frame" block every existing radial test used to
 * hardcode. Mirrors draw_radial_picker's sector-midpoint math
 * (editor/widgets.c: start = index * sector_deg - RADIAL_NORTH_OFFSET_DEG,
 * mid = start + sector_deg / 2) rather than inverting
 * radial_sector_from_stick, so a new Tools-radial entry that bumps
 * EDITOR_TOOLS_ITEM_COUNT never again requires recomputing hardcoded
 * stick coordinates by hand — callers select an item by its
 * EDITOR_TOOLS_*_INDEX constant instead.
 *
 * Caller must already have the Tools radial open (EDITOR_SUB_RADIAL, e.g.
 * via the real ACTION_EDITOR_OPEN_TOOLS binding) before calling, and must
 * drive one more no-input frame afterward for BROWSE to dispatch the
 * pending confirmation (see the existing radial tests in
 * integration_test.c for the two-frame pattern). */
void test_radial_select_item(TestGame *game, int item_index);

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

/* Point editor_state's stable attr-selection identity (selected_attr_kind /
 * selected_attr_section / selected_attr_name) at whatever attr_row_at
 * resolves `display_index` to for `entity`. Mirrors editor_set_selected_attr
 * in editor/core.c, which is file-local there. Tests that jump straight
 * into EDITOR_SUB_ATTR_EDIT without walking the selection UI (see
 * test_integration_editor_attr_edit_* in integration_test.c) need this to
 * populate the identity the same way handle_browse_select would. No-op
 * (selection cleared) if display_index does not resolve to an attribute row. */
void test_set_selected_attr(GameState *state, EditorState *editor_state, Entity *entity, int display_index);

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
