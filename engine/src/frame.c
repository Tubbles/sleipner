#include "frame.h"

#include "alloc.h"
#include "arena.h"
#include "attribute.h"
#include "audio.h"
#include "blueprint.h"
#include "collision.h"
#include "debug.h"
#include "diag.h"
#include "discovery_screen.h"
#include "editor/editor.h"
#include "effect.h"
#include "entity.h"
#include "error.h"
#include "game.h"
#include "input.h"
#include "input_func.h"
#include "inventory_screen.h"
#include "level.h"
#include "map.h"
#include "menu.h"
#include "net_discovery.h"
#include "net_session.h"
#include "network.h"
#include "platform_paths.h"
#include "preferences.h"
#include "progression.h"
#include "rule.h"
#include "save.h"
#include "save_screen.h"
#include "settings.h"
#include "str.h"
#include "strv.h"
#include "undo.h"

#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

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

/* Resolve the platform saves directory and open ctx.save_screen in
 * `mode`, scanning which slots/autosave exist so the renderer can mark
 * them. A resolution failure (no writable saves dir -- headless test
 * environments with no HOME/XDG_DATA_HOME, matching save_autosave's own
 * headless-safe contract) logs and leaves the screen closed rather than
 * opening it against a garbage path; the pause menu simply stays open in
 * that case. */
static void open_save_screen(MenuDispatchCtx ctx, SaveScreenMode mode)
{
    if (!ctx.save_screen) {
        return;
    }
    SCRATCH_SCOPE(&ctx.state->scratch_arena);
    Allocator scratch_alloc = allocator_arena(&ctx.state->scratch_arena);
    Str saves_dir = {0};
    if (!platform_saves_dir(&saves_dir, scratch_alloc, ctx.diag->error)) {
        debug_log(ctx.diag->debug, "open save screen: %s", error_get(ctx.diag->error));
        error_clear(ctx.diag->error);
        return;
    }
    save_screen_open(ctx.save_screen, mode, str_to_strv(saves_dir), scratch_alloc);
}

/* Bind a real listen socket and open ctx.discovery_screen in
 * NET_DISCOVERING (network_start_discovering, network.h). A socket
 * creation failure (no permission, port already bound, sandboxed CI --
 * see network_start_hosting's doc comment for the full best-effort
 * contract network_start_discovering shares) logs and leaves the screen
 * closed rather than opening it against a dead transport; the pause
 * menu closes regardless (dispatch_menu_action's JOIN_GAME case below),
 * same "menu closes either way" shape open_save_screen's callers already
 * settle for on their own resolution failure. */
static void open_discovery_screen(MenuDispatchCtx ctx)
{
    if (!ctx.discovery_screen) {
        return;
    }
    Allocator alloc = allocator_arena(&ctx.state->progression_arena);
    if (!network_start_discovering(&ctx.state->network, &alloc, ctx.diag->error)) {
        debug_log(ctx.diag->debug, "join game: %s", error_get(ctx.diag->error));
        error_clear(ctx.diag->error);
        return;
    }
    discovery_screen_open(ctx.discovery_screen);
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
    case MENU_ACTION_OPEN_SAVE_MENU:
        open_save_screen(ctx, SAVE_SCREEN_MODE_SAVE);
        menu_close(ctx.menu);
        break;
    case MENU_ACTION_OPEN_LOAD_MENU:
        open_save_screen(ctx, SAVE_SCREEN_MODE_LOAD);
        menu_close(ctx.menu);
        break;
    case MENU_ACTION_HOST_GAME: {
        Allocator alloc = allocator_arena(&ctx.state->progression_arena);
        if (!network_start_hosting(&ctx.state->network, &alloc, NETWORK_DEFAULT_HOST_NAME, ctx.diag->error)) {
            debug_log(ctx.diag->debug, "host game: %s", error_get(ctx.diag->error));
            error_clear(ctx.diag->error);
        }
        menu_close(ctx.menu);
        break;
    }
    case MENU_ACTION_JOIN_GAME:
        open_discovery_screen(ctx);
        menu_close(ctx.menu);
        break;
    case MENU_ACTION_OPEN_INVENTORY:
        if (ctx.inventory) {
            Allocator progression_alloc = allocator_arena(&ctx.state->progression_arena);
            inventory_screen_open(ctx.inventory, &ctx.state->progression.items, progression_alloc);
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

/* The ACTION_CONFIRM commit of PLACE mode, split out of handle_place_input.
 * OFFLINE/HOSTING spawn locally exactly as always: the id comes from this
 * peer's own next_entity_id, entity_blueprints/rule_table are patched per
 * spawned entity, and one "Place entity" undo entry is pushed; a HOSTING
 * commit additionally broadcasts the PLACE echo carrying the new ROOT
 * entity's id (level_spawn_entity appends the root first, so it sits at the
 * pre-spawn count). A NET_CLIENT commit spawns NOTHING locally: the host
 * assigns entity ids, so the client only sends the PLACE request
 * (network_editor_commit_place, entity_id -1) and lets the host's echo
 * materialize the entity within a few ticks (client_apply_place_echo,
 * net_session.c) -- the echo-driven spawn is what makes cross-peer id
 * divergence impossible by construction. Nothing changes locally on that
 * path, so it pushes no undo entry and patches no maps; PLACE mode itself
 * stays open for repeat placement on every path, exactly as before. */
static void place_confirm_spawn(
    Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history, Vector2 spawn_position)
{
    const Blueprint *blueprint = &state->gamedata.blueprints.entries.data[editor_state->place_blueprint_index];
    const char *blueprint_name = attr_get_string(&blueprint->attrs, "name");
    if (state->network.mode == NET_CLIENT) {
        /* A nameless blueprint cannot be requested over the wire (the host
         * resolves the spawn by name); blueprint_find can't resolve one
         * either, so there is nothing meaningful to send. */
        if (blueprint_name != nullptr) {
            network_editor_commit_place(state, -1, strv_from_cstr(blueprint_name), spawn_position);
        }
        return;
    }
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    int count_before = state->gamedata.current_level.entities.count;
    if (!level_spawn_entity(diag, &state->gamedata.current_level, blueprint, spawn_position,
                            &state->gamedata.blueprints, texture_registry_lookup, state, &alloc)) {
        debug_log(diag->debug, "error: %s", error_get(diag->error));
        error_clear(diag->error);
        return;
    }
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
    if (state->network.mode == NET_HOSTING && blueprint_name != nullptr) {
        network_editor_commit_place(state, state->gamedata.current_level.entities.data[count_before].id,
                                    strv_from_cstr(blueprint_name), spawn_position);
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
        Vector2 spawn_position =
            editor_state->grid_snap ? editor_snap_position_to_grid(camera->target) : camera->target;
        place_confirm_spawn(diag, state, editor_state, undo_history, spawn_position);
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
    /* S8.7d2: drain any host LOCK_DENY denials first, unconditional of submode,
     * so a stale denial can never linger and abort a future gesture -- see
     * editor_drain_lock_denials (editor/core.c). */
    editor_drain_lock_denials(state, editor_state);
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
        handle_browse_input(diag, state, camera, editor_state, watches, undo_history, input, delta_time);
    }
}

/* Sound is the only effect type with a real handler yet (S6.4): look the
 * name up in the SFX registry and play it through the alias concurrency
 * cap. A miss (unknown name, or headless -- the registry is only
 * populated by main.c's asset-loading path, never in tests) is logged and
 * skipped, not treated as an error. */
static void apply_sound_effects(Diag *diag, GameState *state, const vec_sound_effect_request *sounds)
{
    for (int index = 0; index < sounds->count; index++) {
        const SoundEffectRequest *request = &sounds->data[index];
        debug_log(diag->debug, "effect: sound %.*s", (int)request->name.len, request->name.ptr);
        const Sound *sound = map_strv_sound_get(&state->assets.sounds, request->name);
        if (!sound) {
            debug_log(diag->debug, "effect: sound '%.*s' not found in registry", (int)request->name.len,
                      request->name.ptr);
            continue;
        }
        sfx_alias_pool_play(&state->audio.sfx_aliases, *sound, preferences_effective_sfx_volume(&state->preferences));
    }
}

/* camera_pan/camera_shake have real handlers as of S6.5: start (or
 * restart) the corresponding CameraEffect field on GameState.
 * game_update's camera_update_target/camera_update_shake (game.c) advance
 * them every frame after this. Multiple requests of the same kind in one
 * frame: last one wins, same simplicity apply_sound_effects' alias cap
 * takes for sound -- D22/D26 don't ask for queueing or blending several
 * pans/shakes together. */
static void apply_camera_pan_effects(Diag *diag, GameState *state, const vec_camera_pan_request *camera_pans)
{
    for (int index = 0; index < camera_pans->count; index++) {
        const CameraPanRequest *request = &camera_pans->data[index];
        debug_log(diag->debug, "effect: camera_pan target=(%.1f, %.1f) duration=%.2f", (double)request->target.x,
                  (double)request->target.y, (double)request->duration);
        state->camera_effect.pan = (CameraPanEffect){
            .active = true,
            .from = state->gamedata.camera_target,
            .target = request->target,
            .duration = request->duration,
            .elapsed = 0.0F,
        };
    }
}

static void apply_camera_shake_effects(Diag *diag, GameState *state, const vec_camera_shake_request *camera_shakes)
{
    for (int index = 0; index < camera_shakes->count; index++) {
        const CameraShakeRequest *request = &camera_shakes->data[index];
        debug_log(diag->debug, "effect: camera_shake magnitude=%.2f duration=%.2f", (double)request->magnitude,
                  (double)request->duration);
        state->camera_effect.shake = (CameraShakeEffect){
            .magnitude = request->magnitude,
            .duration = request->duration,
            .elapsed = 0.0F,
            .offset = {0},
        };
    }
}

/* spawn has a real handler as of S6.6 (D22/F24): look the blueprint up by
 * name and spawn it via level_spawn_entity, the same primitive
 * handle_place_input (the editor's PLACE path, above) uses. Deliberately
 * runs here -- after game_update returns -- rather than synchronously in
 * the rule VM: a spawn mutates current_level.entities, which would
 * invalidate the EntityView array and view_count a for_each batch is
 * still iterating (see rule.c's execute_spawn_action). An unknown
 * blueprint name is logged and skipped, not treated as a frame error --
 * one rule referencing a typo'd blueprint shouldn't hard-fail the level.
 * Returns the number of entities actually spawned, so the caller can
 * skip the (otherwise unconditional) setup_current_level_runtime rebuild
 * on frames where nothing was spawned. */
static int apply_spawn_effects(Diag *diag, GameState *state, const vec_spawn_request *spawns)
{
    Allocator alloc = allocator_arena(&state->gamedata_arena);
    int spawned_count = 0;
    for (int index = 0; index < spawns->count; index++) {
        const SpawnRequest *request = &spawns->data[index];
        char blueprint_name[MAX_ARG];
        strv_copy_to_cstr(request->blueprint, blueprint_name, MAX_ARG);
        debug_log(diag->debug, "effect: spawn %s at (%.1f, %.1f)", blueprint_name, (double)request->x,
                  (double)request->y);
        const Blueprint *blueprint = blueprint_find(&state->gamedata.blueprints, blueprint_name);
        if (!blueprint) {
            debug_log(diag->debug, "effect: spawn '%s': unknown blueprint", blueprint_name);
            continue;
        }
        Vector2 position = {request->x, request->y};
        int count_before = state->gamedata.current_level.entities.count;
        if (!level_spawn_entity(diag, &state->gamedata.current_level, blueprint, position, &state->gamedata.blueprints,
                                texture_registry_lookup, state, &alloc)) {
            debug_log(diag->debug, "effect: spawn '%s': %s", blueprint_name, error_get(diag->error));
            error_clear(diag->error);
            continue;
        }
        /* Every entity level_spawn_entity just created for THIS request --
         * the root plus any composite children -- inherits the spawner's
         * facing (S6.10d, D26). Re-derived from the count delta rather
         * than assumed to be exactly one entity: a blueprint with
         * [[blueprint.child]] entries spawns a whole subtree in one
         * level_spawn_entity call. */
        int count_after = state->gamedata.current_level.entities.count;
        for (int spawned_index = count_before; spawned_index < count_after; spawned_index++) {
            state->gamedata.current_level.entities.data[spawned_index].facing = request->facing;
        }
        spawned_count++;
    }
    return spawned_count;
}

/* toast has a real handler as of S6.8b (D25): give_item enqueues the raw
 * item name (see rule.c's ACTION_GIVE_ITEM case), and this formats it into
 * editor_state->toast_msg_buf -- a fixed owned buffer, not the pushed Strv
 * itself -- because the toast must keep displaying for TOAST_DURATION
 * seconds after this same-frame drain finishes, well past the point where
 * a gamedata_arena action argument might be rewound by an intervening
 * transition/hot-reload. Multiple toasts queued in one frame: last one
 * wins, same simplicity apply_camera_pan_effects/apply_camera_shake_effects
 * already take (D22/D26 don't ask for toast queueing either). */
static void apply_toast_effects(Diag *diag, EditorState *editor_state, const vec_toast_request *toasts)
{
    for (int index = 0; index < toasts->count; index++) {
        const ToastRequest *request = &toasts->data[index];
        debug_log(diag->debug, "effect: toast '%.*s'", (int)request->text.len, request->text.ptr);
        (void)snprintf(editor_state->toast_msg_buf, sizeof(editor_state->toast_msg_buf), "Got %.*s",
                       (int)request->text.len, request->text.ptr);
        editor_state->toast_text = strv_from_cstr(editor_state->toast_msg_buf);
        editor_state->toast_timer = TOAST_DURATION;
    }
}

/* GameState-aware replacement for the old S6.2 effect_queue_drain scaffold
 * (which lived in effect.c and only logged). effect.c stays a pure push/
 * clear channel; this is the per-frame apply pass with real GameState
 * access (SFX registry, audio device), which is why it lives here instead.
 * Sound (S6.4), camera_pan/camera_shake (S6.5), spawn (S6.6), and toast
 * (S6.8b) are handled here. Dialogue does NOT go through this queue (S6.7c,
 * D24): it must block the triggering rule, which a deferred per-frame drain
 * can't express, so ACTION_DIALOGUE opens GameState.dialogue directly from
 * the rule VM instead (see rule.c's handle_dialogue_action) and
 * frame_update's dialogue branch (below) drives it. Spawning is the only
 * effect here that can change entity count, so it alone needs the
 * follow-up count-parallel rebuild (rule_table, entity_blueprints,
 * player_index, prev_player_overlaps/prev_solid_collisions) -- via
 * game.c's game_respawn_rebuild_tracking, which preserves the overlap edge
 * state across the rebuild so enter/collide triggers don't spuriously
 * refire. Guarded on spawned_count > 0 so a frame with no spawns (the
 * common case) pays nothing beyond the empty-vec loop. */
static void apply_effect_queue(Diag *diag, GameState *state, EditorState *editor_state)
{
    apply_sound_effects(diag, state, &state->effects.sounds);
    apply_camera_pan_effects(diag, state, &state->effects.camera_pans);
    apply_camera_shake_effects(diag, state, &state->effects.camera_shakes);
    apply_toast_effects(diag, editor_state, &state->effects.toasts);
    int spawned_count = apply_spawn_effects(diag, state, &state->effects.spawns);
    if (spawned_count > 0 && !game_respawn_rebuild_tracking(diag, state)) {
        error_wrap(diag->error, "apply_effect_queue");
        debug_log(diag->debug, "error: %s", error_get(diag->error));
        error_clear(diag->error);
    }
    effect_queue_clear(&state->effects);
}

/* S8.7f2: the host's structural-resync trigger, run once per hosting frame
 * BEFORE network_host_tick so a resync that fires this frame starts streaming
 * the same tick. `arm` is true when this frame's editor input committed a
 * structural mutation or performed an undo/redo restore (computed by
 * run_active_frame from undo_history's mutation counters); a true `arm` restarts
 * the debounce. The armed timer counts down by delta_time and fires
 * network_host_begin_structural_resync when it reaches 0 -- OR immediately if the
 * host has left editor mode while still armed, because leaving editor mode
 * resumes the entity delta stream, which cannot carry structural changes, so a
 * pending structural edit would otherwise never reach clients. A failed emit is
 * logged (the established fallible-call pattern) and leaves the timer at 0: no
 * retry loop, the next edit re-arms. No-op unless NET_HOSTING. */
static void host_structural_resync_tick(Diag *diag,
                                        GameState *state,
                                        UndoHistory *undo_history,
                                        TextureLookupFn resync_texture_lookup,
                                        void *resync_texture_user_data,
                                        bool arm,
                                        float delta_time)
{
    if (state->network.mode != NET_HOSTING) {
        return;
    }
    if (arm) {
        network_structural_mark_dirty(&state->network);
    }
    if (state->network.structural_resync_debounce_timer <= 0.0F) {
        return;
    }
    state->network.structural_resync_debounce_timer -= delta_time;
    if (state->network.structural_resync_debounce_timer > 0.0F && state->editor_mode) {
        return;
    }
    state->network.structural_resync_debounce_timer = 0.0F;
    if (!network_host_begin_structural_resync(diag, state, undo_history, resync_texture_lookup,
                                              resync_texture_user_data)) {
        error_wrap(diag->error, "structural resync");
        debug_log(diag->debug, "error: %s", error_get(diag->error));
        error_clear(diag->error);
    }
}

void run_active_frame(Diag *diag,
                      GameState *state,
                      Camera2D *editor_camera,
                      EditorState *editor_state,
                      WatchList *watches,
                      UndoHistory *undo_history,
                      TextureLookupFn resync_texture_lookup,
                      void *resync_texture_user_data,
                      InputState input,
                      float delta_time)
{
    /* S8.7f2: sample the undo mutation counters around handle_editor_input so
     * the host's structural-resync trigger below can tell whether this frame's
     * editor input committed a structural mutation or performed an undo/redo
     * restore (see host_structural_resync_tick). */
    uint32_t entry_counter_before = undo_history->entry_counter;
    uint32_t restore_counter_before = undo_history->restore_counter;
    if (state->editor_mode) {
        handle_editor_input(diag, state, editor_camera, editor_state, watches, undo_history, input, delta_time);
    }
    /* Unconditional as of S6.8b: give_item's pickup toast (apply_toast_effects,
     * below) is the first toast source reachable from pure play mode (no
     * editor, no menu), so ticking it down only under `state->editor_mode`
     * (as this used to) would leave it stuck on screen forever during
     * ordinary gameplay. Mirrors frame_update's menu branch, which already
     * ticks unconditionally of editor_mode -- gated only by the menu being
     * open. */
    if (editor_state->toast_timer > 0.0F) {
        editor_state->toast_timer -= delta_time;
    }
    /* Input suppression during a fade-to-black transition (S6.14, D27):
     * a zeroed InputState reaches game_update instead of the real one,
     * so the player can't move or act while the screen fades out, swaps
     * levels, and fades back in. state->fade is ticked by frame.c's
     * handle_transition, called once per frame right after frame_update
     * (see main.c/test_helpers.c) -- so the phase read here is the one
     * left over from the PREVIOUS frame's tick, which is exactly the
     * frame boundary the fade is meant to gate. Editor input (above) and
     * the toast timer (above) are untouched -- D27 only asks for player
     * input to be suppressed, not the editor or UI feedback. */
    InputState game_input = state->fade.phase == TRANSITION_FADE_NONE ? input : (InputState){0};
    /* S8.4a session ticks, right before game_update so this tick's
     * behavior dispatch (game.c's input_for_entity) sees the freshest
     * data: network_host_tick drains this tick's JOIN/INPUT/ACK packets
     * before the host simulates (and, S8.4b, sends a SNAPSHOT to any
     * client that just joined, and, S8.4c, reliable-sends a
     * NETWORK_EVENT_PLAYER_JOINED notification plus ages/resends every
     * active client's reliable-event send window by delta_time);
     * network_client_tick sends this tick's MSG_JOIN/MSG_INPUT and, S8.4b,
     * drains and applies any pending SNAPSHOT/DELTA onto this GameState's
     * own entities, and, S8.4c, applies any newly-delivered MSG_EVENT then
     * sends the current ack; S8.7a, it also applies any newly-arrived
     * MSG_ACK to its own outgoing reliable-event send window and ages/
     * resends that window by delta_time. Both are self-guarding on
     * state->network.mode (net_session.h) and no-op under every other
     * mode, so calling both unconditionally here leaves single-player
     * (NET_OFFLINE) untouched -- same reasoning as game.c's tick_network
     * running unconditionally of editor_mode. Uses the raw `input` (this
     * peer's own physical controller reading), not `game_input`: a
     * client's local fade-to-black transition suppresses ITS OWN player's
     * movement, but must not also suppress the input it forwards to the
     * host. */
    /* S8.7e: bridge this editor frame's cursor into the net layer for the
     * session ticks below to consume and share. The editor camera target IS
     * the browse cursor; presence only flows while this peer is actually
     * editing a networked session, and the receiver-side timeout
     * (network_presence_age) fades a peer that stops. Writing these bridge
     * fields rather than passing the editor camera/selection into the net
     * layer keeps network.c editor-free (it has no editor dependency). */
    if (state->editor_mode && (state->network.mode == NET_HOSTING || state->network.mode == NET_CLIENT)) {
        state->network.local_cursor = editor_camera->target;
        state->network.local_cursor_selected_entity_id = editor_state->selected_entity_id;
        state->network.local_cursor_valid = true;
    }
    /* S8.7f2: arm/fire the host's structural-resync trigger. A restore (undo/redo
     * replaces the host's whole gamedata, and networked per-player undo does not
     * exist yet -- S8.7h) arms REGARDLESS of editor mode; a plain snapshot
     * (entry_counter bump) arms only in a structural top mode, because scene-mode
     * commits ride the fine-grained editor-op stream instead (scene-op coverage
     * of delete/place/attr-edit is a known gap owned by the scene-op completion
     * slice, so the top_mode gate here is deliberate, not an oversight). */
    bool restore_happened = undo_history->restore_counter != restore_counter_before;
    bool structural_commit_happened =
        undo_history->entry_counter != entry_counter_before && editor_state->top_mode != EDITOR_TOP_SCENE;
    host_structural_resync_tick(diag, state, undo_history, resync_texture_lookup, resync_texture_user_data,
                                restore_happened || structural_commit_happened, delta_time);
    network_host_tick(state, delta_time);
    network_client_tick(state, &input, delta_time);
    /* S8.7f1: a NET_CLIENT that just finished reassembling a full structural
     * resync (network_client_tick drained its last chunk above) reloads its
     * whole gamedata from the host's streamed bytes here -- BEFORE the
     * render-only NET_CLIENT branch below, so this frame already renders the
     * post-reload state. The reload reassigns per-level entity ids, so the
     * editor selection (frame-layer-owned state) is stale and must be cleared
     * at this call site (network.h has no editor dependency). Safe to poll
     * unconditionally: resync_ready is only ever set on a client. */
    if (network_client_resync_ready(state) &&
        network_client_apply_resync(diag, state, undo_history, resync_texture_lookup, resync_texture_user_data)) {
        editor_state->selected_entity_id = -1;
        editor_state->multiselect_count = 0;
    }
    /* S8.4b: a NET_CLIENT frame renders only -- it never runs the
     * authoritative simulation. network_client_tick above already applied
     * this tick's host state onto state's entities, so game_update_client_render
     * (game.c) only needs to advance render-derived state (animation,
     * camera) on top of it; running the full game_update here would
     * duplicate the host's own behavior dispatch/combat/rules against data
     * the host already owns, and could fire side effects (sounds, toasts,
     * dialogue) the host never intended. Menu/UI handling is unaffected --
     * frame_update (above this call in the stack) already branches away
     * from run_active_frame entirely whenever a menu or modal screen is
     * open, for every mode including NET_CLIENT. Offline (NET_OFFLINE) and
     * hosting (NET_HOSTING) frames are unchanged: game_update runs as
     * before, followed by a DELTA broadcast to every connected client once
     * this tick's simulation has produced its freshest state -- except
     * (S8.7c) while the host is in its own editor mode, see the broadcast
     * gate below. */
    if (state->network.mode == NET_CLIENT) {
        game_update_client_render(state, delta_time);
        return;
    }
    game_update(diag, state, game_input, delta_time);
    apply_effect_queue(diag, state, editor_state);
    /* S8.7c: suspend the every-tick full-state DELTA broadcast while the
     * host is in editor mode. game_update still runs (the shipped "live
     * editing" semantic), but a full-state broadcast every tick would stomp
     * clients' in-progress drag previews -- so during a collaborative
     * editing session the editor-op stream (host-stamped MSG_OP echoes) is
     * the only entity-state channel. Full-state deltas resume the moment the
     * host leaves editor mode and instantly heal any preview divergence,
     * since v1 deltas re-send the whole level's state every tick. */
    if (state->network.mode == NET_HOSTING && !state->editor_mode) {
        network_host_broadcast_delta(state);
    }
}

/* THE SWAP: the fully-black-midpoint body of a level transition -- capture
 * the level being LEFT's entity delta (S6.15b, D33: position/attrs/active,
 * see progression.h), load the target level, re-apply that target level's
 * OWN previously-captured delta (if any) onto its freshly-parsed entities,
 * position the player at the requested spawn point, snap the camera,
 * pre-seed overlap tracking so enter triggers don't refire at the spawn,
 * and push a fresh undo baseline. Runs from handle_transition exactly once
 * per transition, on the frame transition_fade_tick reports do_swap.
 *
 * Ordering of the delta apply vs. the player spawn-position write below is
 * a deliberate choice, not an oversight: progression_apply_level_delta runs
 * first and may overwrite the player entity's position with whatever was
 * captured the last time this level was left, but the unconditional
 * `player->position = (Vector2){spawn_x, spawn_y}` a few lines down always
 * runs after it and wins -- the door's declared spawn point must take
 * precedence over a stale delta, so apply deliberately does not
 * special-case the player's entity id. */
static void run_transition_swap(Diag *diag, GameState *state, FrameContext *ctx)
{
    undo_history_clear(ctx->undo_history);
    Allocator progression_alloc = allocator_arena(&state->progression_arena);
    progression_capture_level_delta(diag, &state->progression, &progression_alloc, &state->gamedata.current_level);
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
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    progression_apply_level_delta(diag, &state->progression, &state->gamedata.current_level, &gamedata_alloc);
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

    /* Autosave-on-transition (S6.15d1, D33): non-fatal by design -- a
     * failed autosave (no writable saves dir, disk full, etc.) must never
     * abort or roll back a transition that has already fully swapped. */
    if (ctx->autosave_fn && !ctx->autosave_fn(diag, state)) {
        debug_log(diag->debug, "autosave failed: %s", error_get(diag->error));
        error_clear(diag->error);
    }
}

/* Fade-to-black transition driver (S6.14, D27). Called once per frame
 * (main.c, test_helpers.c), unconditionally of whether a transition is
 * in flight -- state->fade.phase is the source of truth, not
 * state->transition.pending.
 *
 * Two mutually exclusive branches, never both in the same call:
 *  - Idle (phase == NONE): if a `transition:` rule action fired this
 *    frame (pending == true), START the fade -- phase = FADE_OUT,
 *    timer = TRANSITION_FADE_SECONDS -- and consume pending. The swap
 *    itself does NOT run this frame; the screen has not gone black yet.
 *  - Mid-fade (phase != NONE): tick transition_fade_tick by delta_time.
 *    do_swap fires exactly once, on the frame FADE_OUT's timer reaches
 *    zero (the fully-black midpoint) -- that frame, and only that
 *    frame, runs run_transition_swap.
 *
 * A second `transition:` firing while a fade is already in flight is
 * simply left pending (this function never reads `pending` in the
 * mid-fade branch) -- rule.c's execute_transition_action overwrites the
 * same TransitionRequest fields in place, so the LAST such request
 * before the fade returns to idle is the one that gets consumed,
 * coalescing into a single target rather than queuing multiple swaps.
 * Not reachable via ordinary play since player input (and therefore the
 * enter/interact triggers that fire transition:) is suppressed for the
 * whole fade -- see run_active_frame above -- but a scripted/timer-driven
 * transition could still reach it, hence documenting the behavior here. */
void handle_transition(Diag *diag, GameState *state, FrameContext *ctx, float delta_time)
{
    if (state->fade.phase == TRANSITION_FADE_NONE) {
        if (!state->transition.pending) {
            return;
        }
        state->transition.pending = false;
        state->fade = (TransitionFade){.phase = TRANSITION_FADE_OUT, .timer = TRANSITION_FADE_SECONDS};
        return;
    }

    TransitionFadeStep step = transition_fade_tick(state->fade, delta_time);
    state->fade = step.fade;
    if (step.do_swap) {
        run_transition_swap(diag, state, ctx);
    }
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

/* Modal inventory frame (S6.12b, D25/D34): mirrors run_settings_frame above
 * -- the world is frozen (frame_update returns without calling
 * run_active_frame) while the screen is open. No save/preferences side
 * channel to drain: the inventory screen only reads a snapshot taken at
 * open time, it never mutates persisted state. */
static void run_inventory_frame(GameState *state, FrameContext *ctx, InputState input)
{
    bool close_requested = false;
    inventory_screen_handle_input(ctx->inventory, &input, &state->bindings, &close_requested);
    if (close_requested) {
        inventory_screen_close(ctx->inventory);
        if (ctx->menu) {
            menu_open(ctx->menu);
        }
    }
}

/* Show a toast (mirrors main.c's menu_dispatch_save's toast pattern) and
 * leave the save screen open -- used for every non-success outcome below
 * (resolution failure, write/load failure, confirm on an empty LOAD
 * slot) so the player can pick a different slot or cancel out instead of
 * being bounced back to the pause menu. */
static void show_save_screen_toast(EditorState *editor_state, const char *message)
{
    editor_state->toast_text = strv_from_cstr(message);
    editor_state->toast_timer = TOAST_DURATION;
}

/* Resolve the actual save path for a confirmed slot and dispatch to
 * S6.15d1's save_write (SAVE mode) or save_load (LOAD mode). A confirm
 * on an empty LOAD slot is a no-op-with-toast, never reaching save_load.
 * On success the screen closes straight back to active play -- unlike
 * CANCEL, which returns to the pause menu (see run_save_screen_frame) --
 * matching D33's spec that confirming Save/Load resumes gameplay. A
 * failed directory resolution, a failed save_write/save_load, or the
 * empty-slot case all leave the screen open with a toast instead of
 * closing it, since a load failure in particular must never look like a
 * silent no-op. */
static void handle_save_screen_confirm(Diag *diag, GameState *state, FrameContext *ctx, int confirmed_slot)
{
    SaveScreen *screen = ctx->save_screen;
    bool is_save = screen->mode == SAVE_SCREEN_MODE_SAVE;
    if (!is_save && !save_screen_entry_exists(screen, confirmed_slot)) {
        show_save_screen_toast(ctx->editor_state, "Empty");
        return;
    }

    SCRATCH_SCOPE(&state->scratch_arena);
    Allocator scratch_alloc = allocator_arena(&state->scratch_arena);
    Str saves_dir = {0};
    if (!platform_saves_dir(&saves_dir, scratch_alloc, diag->error) ||
        !platform_ensure_saves_dir(saves_dir.ptr, diag->error)) {
        debug_log(diag->debug, "save screen: %s", error_get(diag->error));
        error_clear(diag->error);
        show_save_screen_toast(ctx->editor_state, is_save ? "Save failed" : "Load failed");
        return;
    }

    Str path = save_screen_entry_is_autosave(confirmed_slot)
                   ? save_screen_autosave_path(str_to_strv(saves_dir), scratch_alloc)
                   : save_screen_slot_path(str_to_strv(saves_dir), confirmed_slot, scratch_alloc);

    bool dispatch_ok = is_save ? save_write(diag, state, path.ptr)
                               : save_load(diag, state, path.ptr, ctx->level_loader_fn, ctx->level_loader_user_data,
                                           ctx->undo_history);
    if (!dispatch_ok) {
        debug_log(diag->debug, "save screen: %s", error_get(diag->error));
        error_clear(diag->error);
        show_save_screen_toast(ctx->editor_state, is_save ? "Save failed" : "Load failed");
        return;
    }

    show_save_screen_toast(ctx->editor_state, is_save ? "Saved" : "Loaded");
    save_screen_close(screen);
}

/* Modal save/load slot-picker frame (S6.15d2, D33): mirrors
 * run_inventory_frame's world-freeze shape above -- the world is frozen
 * while the screen is open. CANCEL returns to the pause menu (same as
 * inventory/settings); a successful CONFIRM instead resolves straight to
 * active play via handle_save_screen_confirm's save_screen_close, never
 * reopening the menu. */
static void run_save_screen_frame(Diag *diag, GameState *state, FrameContext *ctx, InputState input)
{
    bool close_requested = false;
    int confirmed_slot = -1;
    save_screen_handle_input(ctx->save_screen, &input, &state->bindings, &close_requested, &confirmed_slot);

    if (confirmed_slot >= 0) {
        handle_save_screen_confirm(diag, state, ctx, confirmed_slot);
    }
    if (close_requested) {
        save_screen_close(ctx->save_screen);
        if (ctx->menu) {
            menu_open(ctx->menu);
        }
    }
}

/* Modal LAN discovery list (S8.3b): mirrors run_save_screen_frame's
 * world-freeze shape above -- the world is frozen (game_update does not
 * run) while the screen is open. Unlike every other modal screen here,
 * the row list backing it (state->network.join_list) is NOT a snapshot
 * taken once at open time: discovery_client_tick (net_discovery.h) must
 * keep draining the transport and aging/evicting the list every frame
 * the screen is open, or it would never populate at all -- game.c's
 * tick_network only runs from inside game_update, which run_active_frame
 * calls and this screen's world-freeze skips entirely (same as every
 * other modal branch in frame_update below). So this is the second of
 * two places state->network.mode == NET_DISCOVERING/NET_JOINING is ever
 * ticked (see game.c's tick_network doc comment for the first, which
 * covers "screen closed but still DISCOVERING/JOINING") -- exactly one
 * of the two runs on any given frame, since frame_update returns
 * immediately after whichever modal branch it takes, so neither
 * double-processes a frame the other already handled.
 *
 * CONFIRM on a populated row resolves straight back to active play (like
 * save_screen's successful CONFIRM): sets join_target to the chosen
 * host's addr, flips mode to NET_JOINING (S8.4 completes the actual
 * connection), and closes the screen without reopening the pause menu.
 * CANCEL calls network_stop -- closing the transport, clearing
 * join_list, returning to NET_OFFLINE -- and reopens the pause menu,
 * same as every other modal screen's CANCEL. */
static void run_discovery_screen_frame(GameState *state, FrameContext *ctx, InputState input, float delta_time)
{
    discovery_client_tick(&state->network.transport, &state->network.join_list, delta_time);

    bool close_requested = false;
    int confirmed_index = -1;
    discovery_screen_handle_input(ctx->discovery_screen, &input, &state->bindings, &state->network.join_list,
                                  &close_requested, &confirmed_index);

    if (confirmed_index >= 0) {
        state->network.join_target = state->network.join_list.hosts[confirmed_index].addr;
        state->network.mode = NET_JOINING;
        discovery_screen_close(ctx->discovery_screen);
        return;
    }
    if (close_requested) {
        network_stop(&state->network);
        discovery_screen_close(ctx->discovery_screen);
        if (ctx->menu) {
            menu_open(ctx->menu);
        }
    }
}

/* Modal dialogue frame (S6.7c, D24): the world is frozen -- game_update is
 * never called -- while a dialogue is open, mirroring frame_update's
 * settings-open branch above. Ticks the typewriter reveal by delta_time
 * every frame regardless of input, then on ACTION_CONFIRM defers to
 * dialogue_confirm (rule.c) for the skip-to-full / advance-page / close
 * decision. */
static void run_dialogue_frame(GameState *state, const InputState *input, float delta_time)
{
    state->dialogue.reveal_elapsed += delta_time;
    if (input_pressed(input, &state->bindings, ACTION_CONFIRM)) {
        dialogue_confirm(&state->dialogue);
    }
}

void frame_update(Diag *diag, GameState *state, FrameContext *ctx, InputState input, float delta_time)
{
    handle_global_toggles(state, &input, ctx->font_preview_enabled);

    if (ctx->settings && settings_is_open(ctx->settings)) {
        run_settings_frame(state, ctx, input, delta_time);
        return;
    }

    if (ctx->inventory && inventory_screen_is_open(ctx->inventory)) {
        run_inventory_frame(state, ctx, input);
        return;
    }

    if (ctx->save_screen && save_screen_is_open(ctx->save_screen)) {
        run_save_screen_frame(diag, state, ctx, input);
        return;
    }

    if (ctx->discovery_screen && discovery_screen_is_open(ctx->discovery_screen)) {
        run_discovery_screen_frame(state, ctx, input, delta_time);
        return;
    }

    /* Dialogue is modal over the world but never opens over the menu (a
     * rule only fires dialogue: from active play, i.e. run_active_frame
     * below, never while the menu branch is running) -- so it's enough to
     * check this before ACTION_MENU_TOGGLE/the menu dispatch, ignoring
     * both while a dialogue is open rather than fighting over the same
     * frame. */
    if (state->dialogue.active) {
        run_dialogue_frame(state, &input, delta_time);
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
            .inventory = ctx->inventory,
            .save_screen = ctx->save_screen,
            .discovery_screen = ctx->discovery_screen,
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
        run_active_frame(diag, state, ctx->editor_camera, ctx->editor_state, ctx->watches, ctx->undo_history,
                         ctx->resync_texture_lookup, ctx->resync_texture_user_data, input, delta_time);
    }
}
