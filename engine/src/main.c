#include "raylib.h"

#include "alloc.h"
#include "arena.h"
#include "assets.h"
#include "attribute.h"
#include "audio.h"
#include "blueprint.h"
#include "debug.h"
#include "editor.h"
#include "entity.h"
#include "diag.h"
#include "game.h"
#include "input.h"
#include "level.h"
#include "rect.h"

#include "touch.h"
#include "toml_emitter.h"

#include "error.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

VEC_IMPL(font_preview, FontPreviewEntry)
VEC_IMPL(texture_entry, TextureEntry)

#ifdef __ANDROID__
#define SYNCTHING_PATH "/storage/emulated/0/Sync"
#define GAMEDATA_PATH SYNCTHING_PATH "/sleipner/gamedata.toml"
#define TRACE_LOG_PATH SYNCTHING_PATH "/sleipner/trace.log"
#else
#define GAMEDATA_PATH "data/gamedata.toml"
#define TRACE_LOG_PATH "trace.log"
#endif

#define HEARTBEAT_INTERVAL 300
#define TARGET_FPS 60
#define HOT_RELOAD_POLL_FRAMES 30

#define PIXEL_SCALE 4
#define TILE_SIZE 16
#define DEBUG_FONT_SIZE 22
#define DEBUG_LINE_HEIGHT 26
#define DEBUG_PANEL_WIDTH 420
#define DEBUG_LINES 14
#define FONT_PREVIEW_SIZE 32

/* Texture registry — maps texture filenames to loaded Texture2D handles */
static void texture_registry_add(GameState *state, const char *filename, Texture2D texture, Allocator *alloc)
{
    state->assets.textures.alloc = *alloc;
    TextureEntry entry = {0};
    strncpy(entry.filename, filename, MAX_TEXTURE_FILENAME - 1);
    entry.filename[MAX_TEXTURE_FILENAME - 1] = '\0';
    entry.texture = texture;
    (void)vec_texture_entry_push(&state->assets.textures, entry);
}

static Texture2D *texture_registry_lookup(const char *filename, void *user_data)
{
    GameState *state = (GameState *)user_data;
    if (!state) {
        return nullptr;
    }
    for (int index = 0; index < state->assets.textures.count; index++) {
        if (strcmp(state->assets.textures.data[index].filename, filename) == 0) {
            return &state->assets.textures.data[index].texture;
        }
    }
    return nullptr;
}

static Texture2D load_embedded_texture(EmbeddedAsset asset)
{
    Image image = LoadImageFromMemory(".png", asset.data, asset.size);
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}

static void font_preview_add(GameState *state, const char *name, EmbeddedAsset asset, Allocator *alloc)
{
    FontPreviewEntry entry = {0};
    strncpy(entry.name, name, FONT_NAME_LEN - 1);
    entry.font = LoadFontFromMemory(".ttf", asset.data, asset.size, FONT_PREVIEW_SIZE, nullptr, 0);
    entry.valid = IsFontValid(entry.font);
    if (entry.valid) {
        debug_log(&state->debug, "font[%d]: '%s' (%d bytes)", state->assets.font_previews.count, name, asset.size);
    } else {
        debug_log(&state->debug, "font[%d]: '%s' failed to load", state->assets.font_previews.count, name);
    }
    state->assets.font_previews.alloc = *alloc;
    (void)vec_font_preview_push(&state->assets.font_previews, entry);
}

static InputState merge_input(InputState base, InputState overlay)
{
    if (overlay.left_stick.x != 0.0F) {
        base.left_stick.x = overlay.left_stick.x;
    }
    if (overlay.left_stick.y != 0.0F) {
        base.left_stick.y = overlay.left_stick.y;
    }
    if (overlay.right_stick.x != 0.0F) {
        base.right_stick.x = overlay.right_stick.x;
    }
    if (overlay.right_stick.y != 0.0F) {
        base.right_stick.y = overlay.right_stick.y;
    }
    for (int index = 0; index < 4; index++) {
        if (overlay.buttons[index]) {
            base.buttons[index] = true;
        }
    }
    if (overlay.left_trigger > base.left_trigger) {
        base.left_trigger = overlay.left_trigger;
    }
    if (overlay.right_trigger > base.right_trigger) {
        base.right_trigger = overlay.right_trigger;
    }
    return base;
}

static InputState read_all_input(void)
{
    InputState input = input_read_keyboard();
    if (IsGamepadAvailable(0)) {
        input = merge_input(input, input_read(0));
    }
    return input;
}

static void draw_player_entity(const Entity *player)
{
    float source_width = (float)FRAME_SIZE;
    if (player->flip) {
        source_width = -source_width;
    }
    Rectangle source = {(float)(player->frame_index * FRAME_SIZE), (float)(player->anim_row * FRAME_SIZE), source_width,
                        FRAME_SIZE};
    Rectangle dest = {player->position.x - (FRAME_SIZE / 2.0F), player->position.y - (FRAME_SIZE / 2.0F), FRAME_SIZE,
                      FRAME_SIZE};
    DrawTexturePro(*player->texture, source, dest, (Vector2){0, 0}, 0.0F, WHITE);
}

static Rectangle get_source_rect(const AttrSet *instance, const AttrSet *defaults)
{
    return (Rectangle){
        attr_get_scoped_float(instance, defaults, "src_x", 0.0F),
        attr_get_scoped_float(instance, defaults, "src_y", 0.0F),
        attr_get_scoped_float(instance, defaults, "src_w", 0.0F),
        attr_get_scoped_float(instance, defaults, "src_h", 0.0F),
    };
}

static void draw_entity(const GameState *state, const Entity *entity)
{
    const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
    DrawTextureRec(*entity->texture, get_source_rect(&entity->attrs, defaults), entity->position, WHITE);
}

static void log_gamepad_changes(GameState *state, int *prev_gamepads, int frame)
{
    int gamepads = input_count_gamepads();
    if (gamepads != *prev_gamepads) {
        debug_log(&state->debug, "gamepads %d -> %d (frame %d)", *prev_gamepads, gamepads, frame);
        for (int index = 0; index < 4; index++) {
            if (IsGamepadAvailable(index)) {
                debug_log(&state->debug, "gp%d: %s", index, GetGamepadName(index));
            }
        }
        *prev_gamepads = gamepads;
    }
}

static bool any_gamepad_exit_requested(void)
{
    for (int index = 0; index < 4; index++) {
        if (input_exit_requested(index)) {
            return true;
        }
    }
    return false;
}

static void draw_grass(Texture2D texture, RectU32 bounds)
{
    for (uint32_t tile_y = 0; tile_y < bounds.height; tile_y += TILE_SIZE) {
        for (uint32_t tile_x = 0; tile_x < bounds.width; tile_x += TILE_SIZE) {
            DrawTexture(texture, (int)tile_x, (int)tile_y, WHITE);
        }
    }
}

static void draw_debug_collision_boxes(const Level *level, int player_index)
{
    /* Player collision box (green) */
    if (player_index >= 0 && player_index < level->entities.count) {
        const Entity *player = &level->entities.data[player_index];
        DrawRectangleLinesEx(player->collision, 1, GREEN);

        /* Player sprite bounds (yellow) */
        Rectangle sprite = {player->position.x - (FRAME_SIZE / 2.0F), player->position.y - (FRAME_SIZE / 2.0F),
                            FRAME_SIZE, FRAME_SIZE};
        DrawRectangleLinesEx(sprite, 1, YELLOW);
    }

    /* Entity collision boxes (red) */
    for (int index = 0; index < level->entities.count; index++) {
        if (index == player_index) {
            continue;
        }
        DrawRectangleLinesEx(level->entities.data[index].collision, 1, RED);
    }
}

static void draw_debug_info(GameState *state, RectU32 game_bounds)
{
    const Entity *player = game_get_player_const(state);
    int line = 0;
    int screen_w = GetScreenWidth();
    int screen_h = GetScreenHeight();
    int render_w = GetRenderWidth();
    int render_h = GetRenderHeight();

    /* Semi-transparent background for info panel */
    int panel_width = DEBUG_PANEL_WIDTH;
    int panel_height = (DEBUG_LINES * DEBUG_LINE_HEIGHT) + (DEBUG_MARGIN * 2);
    DrawRectangle(0, 0, panel_width, panel_height, debug_bg_color);

    DrawText("v" SLEIPNER_VERSION "  " __DATE__, DEBUG_MARGIN, DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT),
             DEBUG_FONT_SIZE, debug_text_color);
    DrawText(TextFormat("FPS: %d  frame: %d  t: %.1fs", GetFPS(), state->frame, state->elapsed), DEBUG_MARGIN,
             DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);
    DrawText(TextFormat("arena: %zu bytes", arena_used(&state->gamedata_arena)), DEBUG_MARGIN,
             DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);
    DrawText(TextFormat("screen: %dx%d", state->screen_width, state->screen_height), DEBUG_MARGIN,
             DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);
    DrawText(TextFormat("GetScreen: %dx%d  GetRender: %dx%d", screen_w, screen_h, render_w, render_h), DEBUG_MARGIN,
             DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);
    DrawText(TextFormat("game: %ux%u  scale: %d", game_bounds.width, game_bounds.height, PIXEL_SCALE), DEBUG_MARGIN,
             DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);

    if (player) {
        DrawText(TextFormat("player: %.1f, %.1f  row: %d", player->position.x, player->position.y, player->anim_row),
                 DEBUG_MARGIN, DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);
        DrawText(TextFormat("collision: %.0f,%.0f %.0fx%.0f", player->collision.x, player->collision.y,
                            player->collision.width, player->collision.height),
                 DEBUG_MARGIN, DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);
    }

    DrawText(TextFormat("gamepads: %d", input_count_gamepads()), DEBUG_MARGIN,
             DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);

    for (int index = 0; index < 4; index++) {
        if (IsGamepadAvailable(index)) {
            DrawText(TextFormat("  gp%d: %s", index, GetGamepadName(index)), DEBUG_MARGIN,
                     DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);
        }
    }

    /* Log panel at bottom */
    int line_count = debug_get_line_count(&state->debug);
    if (line_count > 0) {
        int log_height = (line_count * DEBUG_LINE_HEIGHT) + (DEBUG_MARGIN * 2);
        int log_y = state->screen_height - log_height;
        DrawRectangle(0, log_y, state->screen_width, log_height, debug_bg_color);

        for (int index = 0; index < line_count; index++) {
            DrawText(debug_get_line(&state->debug, index), DEBUG_MARGIN,
                     log_y + DEBUG_MARGIN + (index * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_log_color);
        }
    }
}

static void load_persistent_assets(GameState *state)
{
    Allocator gamedata_alloc = allocator_arena(&state->gamedata_arena);
    texture_registry_add(state, "player.png", load_embedded_texture(ASSET(player_png)), &gamedata_alloc);
    texture_registry_add(state, "grass.png", load_embedded_texture(ASSET(grass_png)), &gamedata_alloc);
    texture_registry_add(state, "tree.png", load_embedded_texture(ASSET(tree_png)), &gamedata_alloc);
    texture_registry_add(state, "chest.png", load_embedded_texture(ASSET(chest_png)), &gamedata_alloc);
    texture_registry_add(state, "house.png", load_embedded_texture(ASSET(house_png)), &gamedata_alloc);
    texture_registry_add(state, "fence.png", load_embedded_texture(ASSET(fence_png)), &gamedata_alloc);
    for (int index = 0; index < state->assets.textures.count; index++) {
        debug_log(&state->debug, "texture[%d]: '%s' id=%u %dx%d", index, state->assets.textures.data[index].filename,
                  state->assets.textures.data[index].texture.id, state->assets.textures.data[index].texture.width,
                  state->assets.textures.data[index].texture.height);
    }
    font_preview_add(state, "Earth Illusion", ASSET(earth_illusion_ttf), &gamedata_alloc);
    font_preview_add(state, "Golden Apple", ASSET(golden_apple_ttf), &gamedata_alloc);
    font_preview_add(state, "MenuCard", ASSET(menucard_ttf), &gamedata_alloc);
    font_preview_add(state, "Nudge Orb", ASSET(nudge_orb_ttf), &gamedata_alloc);
    font_preview_add(state, "CardboardCrown", ASSET(cardboardcrown_ttf), &gamedata_alloc);
    font_preview_add(state, "RoyalFibre", ASSET(royalfibre_ttf), &gamedata_alloc);
}

static void unload_textures(GameState *state)
{
    for (int index = 0; index < state->assets.textures.count; index++) {
        UnloadTexture(state->assets.textures.data[index].texture);
    }
}

static void font_preview_cleanup(GameState *state)
{
    for (int index = 0; index < state->assets.font_previews.count; index++) {
        if (state->assets.font_previews.data[index].valid) {
            UnloadFont(state->assets.font_previews.data[index].font);
        }
    }
}

static void draw_font_preview(GameState *state)
{
    int panel_x = state->screen_width / 2;
    int line_spacing = FONT_PREVIEW_SIZE + DEBUG_MARGIN;
    int panel_height = (state->assets.font_previews.count * (DEBUG_LINE_HEIGHT + line_spacing)) + DEBUG_MARGIN +
                       (DEBUG_LINE_HEIGHT * 3);
    DrawRectangle(panel_x, 0, state->screen_width - panel_x, panel_height, debug_bg_color);

    int y_offset = DEBUG_MARGIN;
    DrawText("Font Preview - All fonts loaded at 32px", panel_x + DEBUG_MARGIN, y_offset, DEBUG_FONT_SIZE,
             debug_text_color);
    y_offset += DEBUG_LINE_HEIGHT;
    DrawText("(Visual size varies by font design)", panel_x + DEBUG_MARGIN, y_offset, DEBUG_FONT_SIZE,
             debug_text_color);
    y_offset += DEBUG_LINE_HEIGHT;
    DrawText(TextFormat("Showing %d fonts total", state->assets.font_previews.count), panel_x + DEBUG_MARGIN, y_offset,
             DEBUG_FONT_SIZE, debug_text_color);
    y_offset += DEBUG_LINE_HEIGHT;

    for (int index = 0; index < state->assets.font_previews.count; index++) {
        if (!state->assets.font_previews.data[index].valid) {
            continue;
        }
        DrawText(TextFormat("%d. %s", index + 1, state->assets.font_previews.data[index].name), panel_x + DEBUG_MARGIN,
                 y_offset, DEBUG_FONT_SIZE, debug_text_color);
        y_offset += DEBUG_LINE_HEIGHT;
        DrawTextEx(state->assets.font_previews.data[index].font, "The quick brown fox 0123456789",
                   (Vector2){(float)(panel_x + DEBUG_MARGIN), (float)y_offset}, FONT_PREVIEW_SIZE, 1, WHITE);
        y_offset += line_spacing;
    }
}

#define TOUCH_BUTTON_X_FRAC 0.90F
#define TOUCH_BUTTON_SIZE_FRAC 0.10F
#define TOUCH_STICK_RADIUS_DIV 16

#define MAX_GAMEDATA_SIZE (256UL * 1024)
#define MAX_PATH_LEN 512
#define COPY_BUFFER_SIZE 4096

static bool backup_file(GameState *state, const char *path)
{
    char backup_path[MAX_PATH_LEN];
    (void)snprintf(backup_path, MAX_PATH_LEN, "%s.bak", path);

    FILE *source = fopen(path, "re");
    if (!source) {
        error_set(&state->error, "backup fopen(%s): %s", path, strerror(errno));
        return false;
    }

    FILE *dest = fopen(backup_path, "we");
    if (!dest) {
        error_set(&state->error, "backup fopen(%s): %s", backup_path, strerror(errno));
        (void)fclose(source);
        return false;
    }

    char buffer[COPY_BUFFER_SIZE];
    for (;;) {
        size_t bytes = fread(buffer, 1, sizeof(buffer), source);
        if (bytes > 0 && fwrite(buffer, 1, bytes, dest) != bytes) {
            error_set(&state->error, "backup fwrite(%s): %s", backup_path, strerror(errno));
            (void)fclose(source);
            (void)fclose(dest);
            return false;
        }
        if (bytes < sizeof(buffer)) {
            break;
        }
    }

    bool read_ok = (ferror(source) == 0);
    (void)fclose(source);
    (void)fclose(dest);

    if (!read_ok) {
        error_set(&state->error, "backup fread(%s): %s", path, strerror(errno));
        return false;
    }

    debug_log(&state->debug, "backup: %s -> %s", path, backup_path);
    return true;
}

static bool save_gamedata(GameState *state)
{
    if (!backup_file(state, GAMEDATA_PATH)) {
        error_wrap(&state->error, "save_gamedata");
        return false;
    }

    char buffer[MAX_GAMEDATA_SIZE];
    int written =
        toml_emit_gamedata(&state->error, buffer, (int)sizeof(buffer), &state->blueprints, &state->current_level, 1);
    if (written < 0) {
        error_wrap(&state->error, "save_gamedata");
        return false;
    }

    FILE *file = fopen(GAMEDATA_PATH, "we");
    if (!file) {
        error_set(&state->error, "fopen(%s): %s", GAMEDATA_PATH, strerror(errno));
        error_wrap(&state->error, "save_gamedata");
        return false;
    }

    size_t to_write = (size_t)written;
    if (fwrite(buffer, 1, to_write, file) != to_write) {
        error_set(&state->error, "fwrite(%s): %s", GAMEDATA_PATH, strerror(errno));
        (void)fclose(file);
        error_wrap(&state->error, "save_gamedata");
        return false;
    }
    (void)fclose(file);

    debug_log(&state->debug, "saved gamedata: %d bytes to %s", written, GAMEDATA_PATH);
    return true;
}

static char *read_file_text(GameState *state, const char *path, Arena *arena)
{
    /* Stat the file first for diagnostics */
    struct stat file_stat;
    if (stat(path, &file_stat) != 0) {
        error_set(&state->error, "stat(%s): %s", path, strerror(errno));
        return nullptr;
    }
    debug_log(&state->debug, "gamedata: stat(%s): size=%ld mode=%o uid=%d gid=%d", path, (long)file_stat.st_size,
              (unsigned)file_stat.st_mode, (int)file_stat.st_uid, (int)file_stat.st_gid);

    FILE *file = fopen(path, "re");
    if (!file) {
        error_set(&state->error, "fopen(%s): %s", path, strerror(errno));
        return nullptr;
    }

    ArenaCheckpoint read_cp = arena_save(arena);
    char *buffer = arena_alloc(arena, MAX_GAMEDATA_SIZE + 1);
    if (!buffer) {
        (void)fclose(file);
        return nullptr;
    }
    /* Pre-zero so null termination is automatic; avoids tainted-index write after fread */
    (void)memset(buffer, 0, MAX_GAMEDATA_SIZE + 1);

    size_t bytes_read = fread(buffer, 1, MAX_GAMEDATA_SIZE, file);
    if (ferror(file)) {
        error_set(&state->error, "fread(%s): %s", path, strerror(errno));
        arena_restore(arena, read_cp);
        (void)fclose(file);
        return nullptr;
    }
    (void)fclose(file);
    debug_log(&state->debug, "gamedata: read %zu bytes from %s", bytes_read, path);
    return buffer;
}

static float clamp_unit(float value)
{
    if (value < -1.0F) {
        return -1.0F;
    }
    if (value > 1.0F) {
        return 1.0F;
    }
    return value;
}

static InputState apply_touch_input(InputState input, TouchState *touch_state, GameState *state)
{
    Rectangle button_rect = {(float)state->screen_width * TOUCH_BUTTON_X_FRAC, 0.0F,
                             (float)state->screen_width * TOUCH_BUTTON_SIZE_FRAC,
                             (float)state->screen_width * TOUCH_BUTTON_SIZE_FRAC};
    touch_update(touch_state, button_rect);
    if (touch_state->debug_button_triggered) {
        state->debug_enabled = !state->debug_enabled;
        debug_log(&state->debug, "debug %s (touch, frame %d)", (int)state->debug_enabled ? "ON" : "OFF", state->frame);
    }
    Vector2 stick = touch_get_stick(touch_state, (float)state->screen_width / (float)TOUCH_STICK_RADIUS_DIV);
    if (stick.x != 0.0F) {
        input.left_stick.x = clamp_unit(input.left_stick.x + stick.x);
    }
    if (stick.y != 0.0F) {
        input.left_stick.y = clamp_unit(input.left_stick.y + stick.y);
    }
    return input;
}

static void load_gamedata(Diag *diag, GameState *state)
{
    char *content = read_file_text(state, GAMEDATA_PATH, &state->gamedata_arena);
    if (!content) {
        error_wrap(diag->error, "load_gamedata");
        debug_log(diag->debug, "error: %s", error_get(diag->error));
        error_clear(diag->error);
        return;
    }

    int content_length = (int)strlen(content);

    /* Log first 16 bytes as hex for BOM/encoding diagnosis */
    char hexbuf[(16 * 3) + 1] = {0};
    int hex_count = content_length < 16 ? content_length : 16;
    for (int index = 0; index < hex_count; index++) {
        int offset = index * 3;
        (void)snprintf(&hexbuf[offset], 4, "%02x ", (unsigned char)content[index]);
    }
    debug_log(diag->debug, "gamedata: hex[0..%d]: %s", hex_count - 1, hexbuf);

    bool loaded = game_load_gamedata(diag, state,
                                     (GamedataParams){.toml_string = content,
                                                      .texture_lookup = texture_registry_lookup,
                                                      .texture_user_data = state});

    if (loaded) {
        debug_log(diag->debug, "gamedata: %d blueprints", state->blueprints.entries.count);
        for (int index = 0; index < state->blueprints.entries.count; index++) {
            const Blueprint *blueprint = &state->blueprints.entries.data[index];
            debug_log(diag->debug, "  bp[%d]: '%s' tex='%s' attrs=%d", index,
                      attr_get_string(&blueprint->attrs, "name"), attr_get_string(&blueprint->attrs, "texture"),
                      blueprint->attrs.entries.count);
        }
        debug_log(diag->debug, "gamedata: level '%s' (%dx%d, %d entities)", state->current_level.name.ptr,
                  state->current_level.width, state->current_level.height, state->current_level.entities.count);
        for (int index = 0; index < state->current_level.entities.count; index++) {
            const Entity *entity = &state->current_level.entities.data[index];
            debug_log(diag->debug, "  ent[%d]: bp='%s' pos=(%.0f,%.0f) tex=%s", index, entity->blueprint_name.ptr,
                      entity->position.x, entity->position.y, entity->texture ? "ok" : "nullptr");
        }
        if (state->player_index >= 0) {
            debug_log(diag->debug, "gamedata: player at entity[%d]", state->player_index);
        } else {
            debug_log(diag->debug, "gamedata: WARNING player not found!");
        }
    } else {
        debug_log(diag->debug, "error: %s", error_get(diag->error));
        error_clear(diag->error);
    }

    state->gamedata_mtime = GetFileModTime(GAMEDATA_PATH);
}

static bool poll_hot_reload(Diag *diag, GameState *state)
{
    if (!state->gamedata_loaded) {
        load_gamedata(diag, state);
        return true;
    }

    long current_mtime = GetFileModTime(GAMEDATA_PATH);
    if (current_mtime > 0 && current_mtime != state->gamedata_mtime) {
        debug_log(diag->debug, "gamedata: hot-reload triggered");
        load_gamedata(diag, state);
        return true;
    }
    return false;
}

static void handle_hot_reload(Diag *diag, GameState *state, EditorState *editor_state, WatchList *watches)
{
    if (poll_hot_reload(diag, state)) {
        *editor_state = (EditorState){.selected_entity_index = -1,
                                      .sub_mode = EDITOR_SUB_BROWSE,
                                      .selected_attr_index = -1,
                                      .radial_confirmed = -1,
                                      .radial_selected = -1};
        *watches = (WatchList){0};
    }
}

static void draw_entities_depth_sorted(const GameState *state)
{
    const Entity *player = game_get_player_const(state);
    float player_sort_y = player ? player->position.y + 16 : 0;

    for (int index = 0; index < state->current_level.entities.count; index++) {
        if (index == state->player_index) {
            continue;
        }
        float entity_sort_y = state->current_level.entities.data[index].collision.y +
                              state->current_level.entities.data[index].collision.height;
        if (entity_sort_y <= player_sort_y) {
            draw_entity(state, &state->current_level.entities.data[index]);
        }
    }

    if (player) {
        draw_player_entity(player);
    }

    for (int index = 0; index < state->current_level.entities.count; index++) {
        if (index == state->player_index) {
            continue;
        }
        float entity_sort_y = state->current_level.entities.data[index].collision.y +
                              state->current_level.entities.data[index].collision.height;
        if (entity_sort_y > player_sort_y) {
            draw_entity(state, &state->current_level.entities.data[index]);
        }
    }

    if (state->debug_enabled) {
        draw_debug_collision_boxes(&state->current_level, state->player_index);
    }
}

static void handle_save_input(Diag *diag, GameState *state, EditorState *editor_state, WatchList *watches)
{
    if (toggle_pressed((ToggleBinding){KEY_F9, GAMEPAD_BUTTON_RIGHT_FACE_UP})) {
        if (!save_gamedata(state)) {
            debug_log(diag->debug, "save error: %s", error_get(diag->error));
            error_clear(diag->error);
        } else {
            load_gamedata(diag, state);
            *editor_state = (EditorState){.selected_entity_index = -1,
                                          .sub_mode = EDITOR_SUB_BROWSE,
                                          .selected_attr_index = -1,
                                          .radial_confirmed = -1,
                                          .radial_selected = -1};
            *watches = (WatchList){0};
        }
    }
}

static void handle_place_input(
    Diag *diag, GameState *state, Camera2D *camera, EditorState *editor_state, InputState input, float delta_time)
{
    if (state->blueprints.entries.count == 0) {
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
        return;
    }
    if (toggle_pressed((ToggleBinding){KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_DOWN})) {
        int bp_index = editor_state->place_blueprint_index;
        const Blueprint *blueprint = &state->blueprints.entries.data[bp_index];
        Allocator alloc = allocator_arena(&state->gamedata_arena);
        if (!level_spawn_entity(diag, &state->current_level, blueprint, camera->target, &state->blueprints,
                                texture_registry_lookup, state, &alloc)) {
            debug_log(diag->debug, "error: %s", error_get(diag->error));
            error_clear(diag->error);
        }
    }
    if (toggle_pressed((ToggleBinding){KEY_ESCAPE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT})) {
        editor_state->sub_mode = EDITOR_SUB_BROWSE;
    }
    if (toggle_pressed((ToggleBinding){KEY_UP, GAMEPAD_BUTTON_LEFT_FACE_UP})) {
        int count = state->blueprints.entries.count;
        editor_state->place_blueprint_index = (editor_state->place_blueprint_index - 1 + count) % count;
    }
    if (toggle_pressed((ToggleBinding){KEY_DOWN, GAMEPAD_BUTTON_LEFT_FACE_DOWN})) {
        int count = state->blueprints.entries.count;
        editor_state->place_blueprint_index = (editor_state->place_blueprint_index + 1) % count;
    }
    if (toggle_pressed((ToggleBinding){KEY_Q, GAMEPAD_BUTTON_LEFT_TRIGGER_1})) {
        int new_index = editor_state->place_blueprint_index - EDITOR_PLACE_PAGE_SIZE;
        editor_state->place_blueprint_index = (new_index < 0) ? 0 : new_index;
    }
    if (toggle_pressed((ToggleBinding){KEY_E, GAMEPAD_BUTTON_RIGHT_TRIGGER_1})) {
        int count = state->blueprints.entries.count;
        int new_index = editor_state->place_blueprint_index + EDITOR_PLACE_PAGE_SIZE;
        editor_state->place_blueprint_index = (new_index >= count) ? count - 1 : new_index;
    }
    update_editor_camera(camera, input, delta_time);
}

static void handle_editor_input(Diag *diag,
                                GameState *state,
                                Camera2D *camera,
                                EditorState *editor_state,
                                WatchList *watches,
                                InputState input,
                                float delta_time)
{
    handle_save_input(diag, state, editor_state, watches);
    handle_mode_transitions(state, editor_state);
    if (editor_state->sub_mode == EDITOR_SUB_DRAG) {
        handle_drag_input(state, editor_state, input, delta_time);
    } else if (editor_state->sub_mode == EDITOR_SUB_HANDLES) {
        handle_handle_input(state, editor_state, input, delta_time);
    } else if (editor_state->sub_mode == EDITOR_SUB_PLACE) {
        handle_place_input(diag, state, camera, editor_state, input, delta_time);
    } else if (editor_state->sub_mode == EDITOR_SUB_ATTR_EDIT) {
        handle_attr_edit_input(state, editor_state, delta_time);
    } else if (editor_state->sub_mode == EDITOR_SUB_RADIAL) {
        handle_radial_input(editor_state, input);
    } else if (editor_state->sub_mode == EDITOR_SUB_WORD_BUILDER) {
        handle_word_builder_input(diag, state, editor_state);
    } else {
        handle_browse_input(state, camera, editor_state, watches, input, delta_time);
    }
}

typedef struct {
    RenderTexture2D target;
    RectU32 game_bounds;
    Camera2D editor_camera;
    bool font_preview_enabled;
    EditorState editor_state;
    const WatchList *watches;
} RenderParams;

static void render_frame(GameState *state, RenderParams params)
{
    BeginTextureMode(params.target);
    ClearBackground(BLACK);
    if (state->editor_mode) {
        BeginMode2D(params.editor_camera);
    }
    draw_grass(*texture_registry_lookup("grass.png", state), params.game_bounds);
    draw_entities_depth_sorted(state);
    if (state->editor_mode) {
        int hover_index = find_nearest_entity(&state->current_level, params.editor_camera.target);
        draw_editor_highlights(state, &params.editor_state, hover_index);
        draw_collision_handles(state, &params.editor_state);
        draw_place_preview(state, &params.editor_state, params.editor_camera);
        EndMode2D();
        draw_editor_crosshair(params.game_bounds);
    }
    EndTextureMode();

    BeginDrawing();
    DrawTexturePro(
        params.target.texture, (Rectangle){0, 0, (float)params.game_bounds.width, -(float)params.game_bounds.height},
        (Rectangle){0, 0, (float)state->screen_width, (float)state->screen_height}, (Vector2){0, 0}, 0.0F, WHITE);
    if (state->debug_enabled) {
        draw_debug_info(state, params.game_bounds);
    }
    if (params.font_preview_enabled) {
        draw_font_preview(state);
    }
    ScreenSize screen = {state->screen_width, state->screen_height};
    if (state->editor_mode) {
        if (params.editor_state.sub_mode == EDITOR_SUB_PLACE) {
            draw_place_panel(screen, state, &params.editor_state);
        } else if (params.editor_state.sub_mode == EDITOR_SUB_WORD_BUILDER) {
            draw_word_builder_panel(screen, state, &params.editor_state);
        } else {
            draw_editor_panel(screen, state, &params.editor_state);
        }
    }
    draw_watch_overlay(screen, state, params.watches);
    draw_radial_picker(screen, &params.editor_state);
    draw_hints_bar(state->editor_mode, &params.editor_state, screen);
    EndDrawing();
}

int main(void)
{
    GameState state_val = {0};
    GameState *state = &state_val;
    Diag diag_val = {&state->error, &state->debug};
    Diag *diag = &diag_val;
    state->screen_width = SCREEN_WIDTH_DEFAULT;
    state->screen_height = SCREEN_HEIGHT_DEFAULT;

    debug_init(&state->debug, TRACE_LOG_PATH);

#ifdef __ANDROID__
    SetConfigFlags(FLAG_FULLSCREEN_MODE);
    InitWindow(1920, 1080, "Sleipner");
    state->screen_width = 1920;
    state->screen_height = 1080;
#else
    InitWindow(SCREEN_WIDTH_DEFAULT, SCREEN_HEIGHT_DEFAULT, "Sleipner");
#endif
    HideCursor();

#ifndef __ANDROID__
    int monitor = GetCurrentMonitor();
    int mon_width = GetMonitorWidth(monitor);
    int mon_height = GetMonitorHeight(monitor);
    debug_log(&state->debug, "monitor=%d resolution=%dx%d", monitor, mon_width, mon_height);
    if (mon_width > 0 && mon_height > 0) {
        state->screen_width = mon_width;
        state->screen_height = mon_height;
        SetWindowSize(state->screen_width, state->screen_height);
    }
#endif
#ifndef __ANDROID__
    ToggleBorderlessWindowed();
#endif
    debug_log(&state->debug, "state->screen_width=%d state->screen_height=%d", state->screen_width,
              state->screen_height);

    SetTargetFPS(TARGET_FPS);
    audio_init(&state->audio);

    EmbeddedAsset bgm_asset = ASSET(bgm_mp3);
    Music bgm = LoadMusicStreamFromMemory(".mp3", bgm_asset.data, bgm_asset.size);
    bgm.looping = true;
    PlayMusicStream(bgm);

    /* Render target at game resolution for pixel-perfect scaling */
    RectU32 game_bounds = {(uint32_t)state->screen_width / PIXEL_SCALE, (uint32_t)state->screen_height / PIXEL_SCALE};
    RenderTexture2D target = LoadRenderTexture((int)game_bounds.width, (int)game_bounds.height);

    if (!game_init(diag, state, game_bounds)) {
        debug_log(&state->debug, "error: %s", error_get(&state->error));
        error_clear(&state->error);
        return 1;
    }

    /* Load textures and fonts into gamedata_arena — these sit at the bottom of the arena
     * below gamedata_base and survive every gamedata reload (only freed at game exit). */
    load_persistent_assets(state);
    /* Mark the high-water point: everything below here survives gamedata reloads */
    state->gamedata_base = arena_save(&state->gamedata_arena);

#ifndef __ANDROID__
    {
        SCRATCH_SCOPE(&state->scratch_arena);
        EmbeddedAsset gamepad_asset = ASSET(gamecontrollerdb_txt);
        Allocator gamepad_alloc = allocator_arena(&state->scratch_arena);
        input_load_mappings(&state->debug, &gamepad_alloc, (const char *)gamepad_asset.data, gamepad_asset.size);
    }
#endif

    int prev_gamepads = -1;
    bool font_preview_enabled = false;
    TouchState touch_state = {0};
    Camera2D editor_camera = {
        .offset = {(float)game_bounds.width / 2.0F, (float)game_bounds.height / 2.0F},
        .target = {(float)game_bounds.width / 2.0F, (float)game_bounds.height / 2.0F},
        .zoom = 1.0F,
    };
    EditorState editor_state = {.selected_entity_index = -1,
                                .sub_mode = EDITOR_SUB_BROWSE,
                                .selected_attr_index = -1,
                                .radial_confirmed = -1,
                                .radial_selected = -1};
    WatchList watches = {0};

    debug_log(&state->debug, "gamedata path: %s", GAMEDATA_PATH);
    debug_log(&state->debug, "screen %dx%d  game %ux%u  scale %d", state->screen_width, state->screen_height,
              game_bounds.width, game_bounds.height, PIXEL_SCALE);
    debug_log(&state->debug, "GetScreen %dx%d  GetRender %dx%d", GetScreenWidth(), GetScreenHeight(), GetRenderWidth(),
              GetRenderHeight());
    load_gamedata(diag, state);

    while (!WindowShouldClose()) {
        float delta_time = GetFrameTime();

        UpdateMusicStream(bgm);

        /* Hot-reload: poll mtime and reload if gamedata changed */
        if (state->frame % HOT_RELOAD_POLL_FRAMES == 0) {
            handle_hot_reload(diag, state, &editor_state, &watches);
        }

        /* Toggle debug overlay: F3 only (Select/MIDDLE_LEFT is now used by radial picker) */
        if (IsKeyPressed(KEY_F3)) {
            state->debug_enabled = !state->debug_enabled;
            debug_log(&state->debug, "debug %s (frame %d)", (int)state->debug_enabled ? "ON" : "OFF", state->frame);
        }

        /* Toggle font preview: F4 or gamepad Right Thumb */
        if (toggle_pressed((ToggleBinding){KEY_F4, GAMEPAD_BUTTON_RIGHT_THUMB})) {
            font_preview_enabled = !font_preview_enabled;
        }

        /* Toggle editor mode: F5 or gamepad Start */
        if (toggle_pressed((ToggleBinding){KEY_F5, GAMEPAD_BUTTON_MIDDLE_RIGHT})) {
            state->editor_mode = !state->editor_mode;
            debug_log(&state->debug, "editor %s (frame %d)", (int)state->editor_mode ? "ON" : "OFF", state->frame);
        }

        log_gamepad_changes(state, &prev_gamepads, state->frame);

        if (any_gamepad_exit_requested()) {
            goto quit;
        }

        InputState input = read_all_input();
        input = apply_touch_input(input, &touch_state, state);

        /* Handle editor-only actions: save, entity browse, and camera pan */
        if (state->editor_mode) {
            handle_editor_input(diag, state, &editor_camera, &editor_state, &watches, input, delta_time);
        }

        /* Update (pure logic — no rendering) */
        game_update(diag, state, input, delta_time);

        render_frame(state, (RenderParams){
                                .target = target,
                                .game_bounds = game_bounds,
                                .editor_camera = editor_camera,
                                .font_preview_enabled = font_preview_enabled,
                                .editor_state = editor_state,
                                .watches = &watches,
                            });
    }

quit:
    debug_log(&state->debug, "exiting game loop (frame=%d t=%.1fs)", state->frame, state->elapsed);

    UnloadMusicStream(bgm);
    UnloadRenderTexture(target);
    unload_textures(state);
    font_preview_cleanup(state);
    game_free(diag, state);
    audio_shutdown(&state->audio);
    debug_shutdown(&state->debug);
    CloseWindow();
    return 0;
}
