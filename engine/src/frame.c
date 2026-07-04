#include "frame.h"

#include "alloc.h"
#include "arena.h"
#include "attribute.h"
#include "blueprint.h"
#include "collision.h"
#include "debug.h"
#include "diag.h"
#include "editor/editor.h"
#include "entity.h"
#include "error.h"
#include "game.h"
#include "input.h"
#include "input_func.h"
#include "level.h"
#include "map.h"
#include "menu.h"
#include "rule.h"
#include "settings.h"
#include "str.h"
#include "strv.h"
#include "undo.h"

#include "raylib.h"

#include <stdbool.h>

void handle_global_toggles(GameState *state, const InputState *input, bool *font_preview_enabled)
{
    if (input_pressed(input, &state->bindings, ACTION_FONT_PREVIEW_TOGGLE)) {
        *font_preview_enabled = !*font_preview_enabled;
    }
    if (input_pressed(input, &state->bindings, ACTION_EDITOR_TOGGLE)) {
        state->editor_mode = !state->editor_mode;
        debug_log(&state->debug, "editor %s (frame %d)", (int)state->editor_mode ? "ON" : "OFF", state->frame);
    }
}

void toggle_menu_open(MenuState *menu)
{
    if (menu->open) {
        menu_close(menu);
    } else {
        menu_open(menu);
    }
}

void dispatch_menu_action(MenuDispatchCtx ctx, MenuAction action)
{
    switch (action) {
    case MENU_ACTION_RESUME:
        menu_close(ctx.menu);
        break;
    case MENU_ACTION_SAVE:
        if (ctx.save_fn) {
            ctx.save_fn(ctx.diag, ctx.state, ctx.editor_state, ctx.undo_history);
        }
        menu_close(ctx.menu);
        break;
    case MENU_ACTION_RESTORE:
        if (ctx.restore_fn) {
            ctx.restore_fn(ctx.diag, ctx.state, ctx.editor_state, ctx.watches, ctx.undo_history);
        }
        menu_close(ctx.menu);
        break;
    case MENU_ACTION_OPEN_SETTINGS:
        if (ctx.settings) {
            settings_open(ctx.settings);
        }
        menu_close(ctx.menu);
        break;
    case MENU_ACTION_TOGGLE_DEBUG_OVERLAY:
        ctx.state->debug_enabled = !ctx.state->debug_enabled;
        debug_log(&ctx.state->debug, "debug %s (frame %d)", ctx.state->debug_enabled ? "ON" : "OFF", ctx.state->frame);
        menu_close(ctx.menu);
        break;
    case MENU_ACTION_QUIT:
        *ctx.quit_requested = true;
        break;
    case MENU_ACTION_NONE:
        break;
    }
}

static void handle_place_input(Diag *diag,
                               GameState *state,
                               Camera2D *camera,
                               EditorState *editor_state,
                               UndoHistory *undo_history,
                               InputState input,
                               float delta_time)
{
    if (state->gamedata.blueprints.entries.count == 0) {
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (input_pressed(&input, &state->bindings, ACTION_CONFIRM)) {
        int bp_index = editor_state->place_blueprint_index;
        const Blueprint *blueprint = &state->gamedata.blueprints.entries.data[bp_index];
        Allocator alloc = allocator_arena(&state->gamedata_arena);
        int count_before = state->gamedata.current_level.entities.count;
        if (!level_spawn_entity(diag, &state->gamedata.current_level, blueprint, camera->target,
                                &state->gamedata.blueprints, texture_registry_lookup, state, &alloc)) {
            debug_log(diag->debug, "error: %s", error_get(diag->error));
            error_clear(diag->error);
        } else {
            for (int index = count_before; index < state->gamedata.current_level.entities.count; index++) {
                Entity *spawned = &state->gamedata.current_level.entities.data[index];
                Str bp_name = str_new(alloc);
                (void)str_from_strv(&bp_name, str_to_strv(spawned->blueprint_name));
                (void)map_int_str_set(&state->gamedata.entity_blueprints, spawned->id, bp_name);
                const Blueprint *spawned_bp = blueprint_find(&state->gamedata.blueprints, spawned->blueprint_name.ptr);
                if (spawned_bp && spawned_bp->rules.count > 0) {
                    (void)map_entity_ruleset_set(&state->gamedata.rule_table, spawned->id, spawned_bp->rules);
                }
            }
            undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                                   strv_from_cstr("Place entity"));
        }
    }
    if (input_pressed(&input, &state->bindings, ACTION_CANCEL)) {
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
    }
    if (input_pressed(&input, &state->bindings, ACTION_NAV_UP)) {
        int count = state->gamedata.blueprints.entries.count;
        editor_state->place_blueprint_index = (editor_state->place_blueprint_index - 1 + count) % count;
    }
    if (input_pressed(&input, &state->bindings, ACTION_NAV_DOWN)) {
        int count = state->gamedata.blueprints.entries.count;
        editor_state->place_blueprint_index = (editor_state->place_blueprint_index + 1) % count;
    }
    if (input_pressed(&input, &state->bindings, ACTION_PAGE_UP)) {
        int new_index = editor_state->place_blueprint_index - EDITOR_PLACE_PAGE_SIZE;
        editor_state->place_blueprint_index = (new_index < 0) ? 0 : new_index;
    }
    if (input_pressed(&input, &state->bindings, ACTION_PAGE_DOWN)) {
        int count = state->gamedata.blueprints.entries.count;
        int new_index = editor_state->place_blueprint_index + EDITOR_PLACE_PAGE_SIZE;
        editor_state->place_blueprint_index = (new_index >= count) ? count - 1 : new_index;
    }
    update_editor_camera(camera, &input, &state->bindings, delta_time);
}

void handle_editor_input(Diag *diag,
                         GameState *state,
                         Camera2D *camera,
                         EditorState *editor_state,
                         WatchList *watches,
                         UndoHistory *undo_history,
                         InputState input,
                         float delta_time)
{
    handle_mode_transitions(state, editor_state, &input);
    if (editor_state->sub_mode == EDITOR_SUB_DRAG) {
        handle_drag_input(state, editor_state, undo_history, input, delta_time);
    } else if (editor_state->sub_mode == EDITOR_SUB_HANDLES) {
        handle_handle_input(state, editor_state, undo_history, input, delta_time);
    } else if (editor_state->sub_mode == EDITOR_SUB_PLACE) {
        handle_place_input(diag, state, camera, editor_state, undo_history, input, delta_time);
    } else if (editor_state->sub_mode == EDITOR_SUB_ATTR_EDIT) {
        handle_attr_edit_input(state, editor_state, undo_history, &input, delta_time);
    } else if (editor_state->sub_mode == EDITOR_SUB_RADIAL) {
        handle_radial_input(editor_state, &input, &state->bindings);
    } else if (editor_state->sub_mode == EDITOR_SUB_WORD_BUILDER) {
        handle_word_builder_input(diag, state, editor_state, undo_history, &input);
    } else if (editor_state->sub_mode == EDITOR_SUB_FUZZY_FINDER) {
        handle_fuzzy_finder_input(diag, state, editor_state, undo_history, texture_registry_lookup, state, &input);
    } else if (editor_state->sub_mode == EDITOR_SUB_GAMEPAD_KB) {
        handle_gamepad_kb_input(editor_state, &input, &state->bindings);
    } else if (editor_state->sub_mode == EDITOR_SUB_WATCH_LIST) {
        handle_watch_list_input(editor_state, watches, &input, &state->bindings);
    } else if (editor_state->sub_mode == EDITOR_SUB_TILE_PAINT) {
        handle_tile_paint_input(state, editor_state, undo_history, &input);
    } else if (editor_state->sub_mode == EDITOR_SUB_TILE_PALETTE) {
        handle_tile_palette_input(editor_state, &state->gamedata.tileset, &input, &state->bindings);
    } else if (editor_state->sub_mode == EDITOR_SUB_ATLAS_BROWSE) {
        handle_atlas_browse_input(state, camera, editor_state, &input);
    } else if (editor_state->sub_mode == EDITOR_SUB_ATLAS_REGION_EDIT) {
        handle_atlas_region_edit_input(state, editor_state, undo_history, input, delta_time);
    } else if (editor_state->sub_mode == EDITOR_SUB_ANIM_EDIT) {
        handle_anim_edit_input(state, editor_state, undo_history, &input);
    } else if (editor_state->sub_mode == EDITOR_SUB_ANIM_FRAMES) {
        handle_anim_frames_input(state, editor_state, &input);
    } else if (editor_state->sub_mode == EDITOR_SUB_RULE_LIST) {
        handle_rule_list_input(state, editor_state, undo_history, &input);
    } else if (editor_state->sub_mode == EDITOR_SUB_RULE_TREE) {
        handle_rule_tree_input(state, editor_state, undo_history, &input);
    } else if (editor_state->top_mode == EDITOR_TOP_BLUEPRINT) {
        handle_blueprint_browse_input(state, editor_state, undo_history, &input);
    } else if (editor_state->top_mode == EDITOR_TOP_LEVEL) {
        handle_level_browse_input(diag, state, editor_state, undo_history, &input);
    } else {
        handle_browse_input(state, camera, editor_state, watches, undo_history, input, delta_time);
    }
}

void run_active_frame(Diag *diag,
                      GameState *state,
                      Camera2D *editor_camera,
                      EditorState *editor_state,
                      WatchList *watches,
                      UndoHistory *undo_history,
                      InputState input,
                      float delta_time)
{
    if (state->editor_mode) {
        handle_editor_input(diag, state, editor_camera, editor_state, watches, undo_history, input, delta_time);
        if (editor_state->toast_timer > 0.0F) {
            editor_state->toast_timer -= delta_time;
        }
    }
    game_update(diag, state, input, delta_time);
}

void handle_transition(Diag *diag, GameState *state, FrameContext *ctx)
{
    if (!state->transition.pending) {
        return;
    }
    undo_history_clear(ctx->undo_history);
    state->transition.pending = false;
    float spawn_x = state->transition.x;
    float spawn_y = state->transition.y;
    SCRATCH_SCOPE(&state->scratch_arena);
    Allocator scratch_alloc = allocator_arena(&state->scratch_arena);
    Str level_name = str_new(scratch_alloc);
    (void)str_from_strv(&level_name, str_to_strv(state->transition.level));
    debug_log(diag->debug, "transition to '%s' at (%.0f, %.0f)", level_name.ptr, spawn_x, spawn_y);
    if (ctx->level_loader_fn) {
        (void)ctx->level_loader_fn(diag, state, level_name.ptr, ctx->level_loader_user_data);
    }
    Entity *player = game_get_player(state);
    if (player) {
        player->position = (Vector2){spawn_x, spawn_y};
        game_snap_camera(state);

        /* Pre-seed overlap tracking so enter triggers don't fire for entities
         * the player already overlaps at the spawn position. */
        const AttrSet *player_defaults = entity_resolve_defaults(state, player->id);
        for (int index = 0;
             index < state->gamedata.current_level.entities.count && index < state->gamedata.prev_player_overlaps.count;
             index++) {
            Entity *target = &state->gamedata.current_level.entities.data[index];
            const AttrSet *defaults = entity_resolve_defaults(state, target->id);
            CollisionPrimitive player_prim_storage;
            CollisionPrimitive trigger_prim_storage;
            CollisionShape player_shape = entity_collision_region(player, player_defaults, &player_prim_storage);
            CollisionShape trigger_shape = entity_trigger_region(target, defaults, &trigger_prim_storage);
            state->gamedata.prev_player_overlaps.data[index] =
                composite_overlap(&player_shape, player->position, 0.0F, &trigger_shape, target->position, 0.0F);
        }
    }
    undo_history_new_entry(ctx->undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Level loaded"));
}

static void run_settings_frame(GameState *state, FrameContext *ctx, InputState input, float delta_time)
{
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    bool close_requested = false;
    settings_handle_input(ctx->settings, &input, &state->bindings, &state->preferences, gamedata_alloc,
                          &close_requested);
    settings_tick(ctx->settings, delta_time);
    if (ctx->settings->save_requested) {
        ctx->settings->save_requested = false;
        if (ctx->keybindings_save_fn) {
            (void)ctx->keybindings_save_fn(state);
        }
    }
    if (ctx->settings->save_preferences_requested) {
        ctx->settings->save_preferences_requested = false;
        if (ctx->preferences_save_fn) {
            (void)ctx->preferences_save_fn(state);
        }
    }
    if (close_requested) {
        settings_close(ctx->settings);
        if (ctx->menu) {
            menu_open(ctx->menu);
        }
    }
}

void frame_update(Diag *diag, GameState *state, FrameContext *ctx, InputState input, float delta_time)
{
    handle_global_toggles(state, &input, ctx->font_preview_enabled);

    if (ctx->settings && settings_is_open(ctx->settings)) {
        run_settings_frame(state, ctx, input, delta_time);
        return;
    }

    if (input_pressed(&input, &state->bindings, ACTION_MENU_TOGGLE)) {
        toggle_menu_open(ctx->menu);
    }

    if (input_pressed(&input, &state->bindings, ACTION_QUIT)) {
        *ctx->quit_requested = true;
    }

    if (ctx->menu->open) {
        MenuDispatchCtx dispatch_ctx = {
            .diag = diag,
            .state = state,
            .editor_state = ctx->editor_state,
            .watches = ctx->watches,
            .undo_history = ctx->undo_history,
            .menu = ctx->menu,
            .settings = ctx->settings,
            .quit_requested = ctx->quit_requested,
            .save_fn = ctx->save_fn,
            .restore_fn = ctx->restore_fn,
        };
        MenuAction action = menu_handle_input(ctx->menu, &input, &state->bindings);
        dispatch_menu_action(dispatch_ctx, action);
        if (ctx->editor_state->toast_timer > 0.0F) {
            ctx->editor_state->toast_timer -= delta_time;
        }
    } else {
        run_active_frame(diag, state, ctx->editor_camera, ctx->editor_state, ctx->watches, ctx->undo_history, input,
                         delta_time);
    }
}
