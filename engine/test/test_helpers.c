#include "test_helpers.h"
#include "alloc.h"
#include "attribute.h"
#include "diag.h"
#include "editor/editor.h"
#include "editor/internal.h"
#include "entity.h"
#include "frame.h"
#include "game.h"
#include "input.h"
#include "menu.h"
#include "raylib.h"
#include "rect.h"
#include "str.h"
#include "strv.h"
#include "undo.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* --- Heap allocator wrappers (test-only) --- */

static void *heap_malloc_fn(void *ctx, size_t size)
{
    (void)ctx;
    return malloc(size);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — signature dictated by Allocator typedef
static void *heap_realloc_fn(void *ctx, void *ptr, size_t new_size)
{
    (void)ctx;
    return realloc(ptr, new_size);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — signature dictated by Allocator typedef
static void heap_free_fn(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

Allocator allocator_heap(void)
{
    return (Allocator){
        .ctx = nullptr,
        .malloc_fn = heap_malloc_fn,
        .realloc_fn = heap_realloc_fn,
        .free_fn = heap_free_fn,
    };
}

Allocator test_heap_alloc;

void test_helpers_init(void)
{
    test_heap_alloc = allocator_heap();
}

void test_blueprint_table_free(BlueprintTable *table)
{
    for (int index = 0; index < table->entries.count; index++) {
        test_blueprint_free(&table->entries.data[index]);
    }
    vec_blueprint_free(&table->entries);
}

void test_blueprint_free(Blueprint *blueprint)
{
    for (int index = 0; index < blueprint->children.count; index++) {
        str_free(&blueprint->children.data[index].blueprint_name);
        str_free(&blueprint->children.data[index].tag);
    }
    vec_blueprint_child_free(&blueprint->children);
    test_attr_set_free(&blueprint->attrs);
}

void test_level_free(Level *level)
{
    level_free(&test_heap_alloc, level);
}

void test_entity_free(Entity *entity)
{
    str_free(&entity->blueprint_name);
    str_free(&entity->tag);
    test_attr_set_free(&entity->attrs);
}

void test_flag_set_free(FlagSet *flags)
{
    flag_set_free(&test_heap_alloc, flags);
}

void test_attr_set_free(AttrSet *set)
{
    attr_set_free(&test_heap_alloc, set);
}

/* --- Black-box integration test fixture --- */

/* All headless tests share one Texture2D placeholder. Real GL
 * resources are never created; entity rendering doesn't run in tests
 * so the texture handle is never sampled, only address-compared. */
static Texture2D dummy_texture;

static Texture2D *test_dummy_texture_lookup(const char *texture_name, void *user_data)
{
    (void)texture_name;
    (void)user_data;
    return &dummy_texture;
}

bool test_game_setup(TestGame *out, const char *toml_string)
{
    *out = (TestGame){0};
    out->diag = (Diag){&out->state.error, &out->state.debug};

    if (!game_init(&out->diag, &out->state, (RectU32){320, 240})) {
        return false;
    }
    if (!game_load_gamedata(
            &out->diag, &out->state,
            (GamedataParams){.toml_string = toml_string, .texture_lookup = test_dummy_texture_lookup})) {
        return false;
    }
    if (!undo_history_init(&out->state.error, &out->undo_history)) {
        return false;
    }
    undo_history_new_entry(&out->undo_history, &out->state.gamedata, &out->state.gamedata_arena,
                           out->state.gamedata_base, strv_from_cstr("Initial"));

    menu_init(&out->menu);
    out->editor_state = (EditorState){.top_mode = EDITOR_TOP_SCENE,
                                      .selected_entity_index = -1,
                                      .sub_mode = EDITOR_SUB_BROWSE,
                                      .selected_attr_index = -1,
                                      .radial_confirmed = -1,
                                      .radial_selected = -1,
                                      .selected_blueprint_index = -1,
                                      .blueprint_attr_index = -1,
                                      .blueprint_tree_index = -1};
    out->editor_camera = (Camera2D){.zoom = 1.0F};
    out->frame_ctx = (FrameContext){
        .editor_state = &out->editor_state,
        .editor_camera = &out->editor_camera,
        .watches = &out->watches,
        .undo_history = &out->undo_history,
        .menu = &out->menu,
        .font_preview_enabled = &out->font_preview_enabled,
        .quit_requested = &out->quit_requested,
        .save_fn = nullptr,
        .restore_fn = nullptr,
    };
    return true;
}

void test_game_teardown(TestGame *game)
{
    undo_history_free(&game->undo_history);
    menu_cleanup(&game->menu);
    game_free(&game->diag, &game->state);
}

void test_advance_frame(TestGame *game, InputState input)
{
    frame_update(&game->diag, &game->state, &game->frame_ctx, input, 1.0F / 60.0F);
}

void test_advance_frames(TestGame *game, InputState input, int frames)
{
    for (int iteration = 0; iteration < frames; iteration++) {
        frame_update(&game->diag, &game->state, &game->frame_ctx, input, 1.0F / 60.0F);
    }
}

int test_find_int_attr_display_index(GameState *state, Entity *entity, const char *name)
{
    int probe_limit = 64;
    size_t name_len = strlen(name);
    for (int idx = 0; idx < probe_limit; idx++) {
        Attribute *attr = attr_at_display_index(state, entity, idx);
        if (!attr) {
            continue;
        }
        if (attr->type == ATTR_INT && attr->name.len == name_len && strncmp(attr->name.ptr, name, name_len) == 0) {
            return idx;
        }
    }
    return -1;
}

int test_player_int_attr(GameState *state, const char *name)
{
    Entity *player_entity = &state->gamedata.current_level.entities.data[state->gamedata.player_index];
    int idx = test_find_int_attr_display_index(state, player_entity, name);
    if (idx < 0) {
        return 0;
    }
    Attribute *attr = attr_at_display_index(state, player_entity, idx);
    if (!attr) {
        return 0;
    }
    return attr->value.i;
}
