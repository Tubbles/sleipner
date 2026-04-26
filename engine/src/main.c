#include "raylib.h"

#if defined(__linux__) && !defined(__ANDROID__)
/* We only need glfwInitHint and two GLFW_WAYLAND_* macros from this
 * header. GLFW_INCLUDE_NONE skips its <GL/gl.h> include so the file
 * does not need the system OpenGL headers on the include path. */
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
/* LSan picks up this weak symbol at runtime and uses its return value as
 * the suppressions list (same mechanism as __asan_default_options). Each
 * "leak:<substring>" entry matches when any frame in the leak's stack
 * trace contains <substring>. Suppressing the residual Wayland leaks
 * here means the binary is leak-clean out of the box, with no
 * LSAN_OPTIONS env var or external file required.
 *
 * Suppressed: _glfwInitEGL pthread_once allocations from Mesa EGL driver
 * dlopen and _glfwCreateWindowWayland wl_proxy_marshal protocol object
 * allocations. Both are vendor process-lifetime state that GLFW's
 * Wayland teardown does not reclaim. */
/* Name, signature, and external linkage are dictated by the LSan ABI:
 * the runtime calls the weak symbol literally named
 * __lsan_default_suppressions, so it cannot be renamed and cannot be
 * static. The reserved-identifier, naming, and internal-linkage checks
 * have to be silenced for this one declaration. */
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming,misc-use-internal-linkage)
const char *__lsan_default_suppressions(void)
{
    return "leak:_glfwInitEGL\n"
           "leak:_glfwCreateWindowWayland\n";
}
#endif

#include "alloc.h"
#include "arena.h"
#include "assets.h"
#include "attribute.h"
#include "audio.h"
#include "blueprint.h"
#include "blur.h"
#include "debug.h"
#include "depth_sort.h"
#include "editor/editor.h"
#include "entity.h"
#include "diag.h"
#include "frame.h"
#include "game.h"
#include "input.h"
#include "input_func.h"
#include "level.h"
#include "menu.h"
#include "rect.h"
#include "rule.h"
#include "str.h"
#include "strv.h"
#include "undo.h"

#include "toml_emitter.h"

#include "error.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define FOPEN_READ "r"
#define FOPEN_WRITE "w"
#define FOPEN_APPEND "a"
#else
#define FOPEN_READ "re"
#define FOPEN_WRITE "we"
#define FOPEN_APPEND "ae"
#endif

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
#define DEBUG_FONT_SIZE 32
#define DEBUG_LINE_HEIGHT 26
#define DEBUG_PANEL_WIDTH 420
#define DEBUG_LINES 14
#define FONT_PREVIEW_SIZE 32

/* UI text helpers — wrap DrawTextEx with the same ergonomics as DrawText */
static void draw_ui_text(Font font, const char *text, int pos_x, int pos_y, int font_size, Color color)
{
    DrawTextEx(font, text, (Vector2){(float)pos_x, (float)pos_y}, (float)font_size, 1, color);
}

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

static Texture2D load_embedded_texture(EmbeddedAsset asset)
{
    Image image = LoadImageFromMemory(".png", asset.data, asset.size);
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}

static int count_connected_gamepads(void)
{
    int count = 0;
    for (int index = 0; index < 4; index++) {
        if (IsGamepadAvailable(index)) {
            count++;
        }
    }
    return count;
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

static void draw_player_entity(const GameState *state, const Entity *player)
{
    float source_width = (float)FRAME_SIZE;
    if (player->flip) {
        source_width = -source_width;
    }
    Rectangle source = {(float)(player->frame_index * FRAME_SIZE), (float)(player->anim_row * FRAME_SIZE), source_width,
                        FRAME_SIZE};
    const AttrSet *defaults = entity_resolve_defaults(state, player->id);
    Vector2 draw_pos = entity_draw_position(player, defaults);
    Rectangle dest = {draw_pos.x, draw_pos.y, FRAME_SIZE, FRAME_SIZE};
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
    if (!entity->texture) {
        return;
    }
    const AttrSet *defaults = entity_resolve_defaults(state, entity->id);
    Vector2 draw_pos = entity_draw_position(entity, defaults);
    DrawTextureRec(*entity->texture, get_source_rect(&entity->attrs, defaults), draw_pos, WHITE);
}

static void log_gamepad_changes(GameState *state, int *prev_gamepads, int frame)
{
    int gamepads = count_connected_gamepads();
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

static void draw_background_tiles(Texture2D texture, RectU32 bounds, Color tint)
{
    for (uint32_t tile_y = 0; tile_y < bounds.height; tile_y += TILE_SIZE) {
        for (uint32_t tile_x = 0; tile_x < bounds.width; tile_x += TILE_SIZE) {
            DrawTexture(texture, (int)tile_x, (int)tile_y, tint);
        }
    }
}

static void draw_debug_collision_boxes(const GameState *state, const Level *level, int player_index)
{
    /* Player collision box (green) + sprite bounds (yellow) */
    if (player_index >= 0 && player_index < level->entities.count) {
        const Entity *player = &level->entities.data[player_index];
        const AttrSet *defaults = entity_resolve_defaults(state, player->id);
        DrawRectangleLinesEx(entity_collision_rect(player, defaults), 1, GREEN);
        Vector2 draw_pos = entity_draw_position(player, defaults);
        Rectangle sprite = {draw_pos.x, draw_pos.y, FRAME_SIZE, FRAME_SIZE};
        DrawRectangleLinesEx(sprite, 1, YELLOW);
    }

    /* Entity collision boxes (red) */
    for (int index = 0; index < level->entities.count; index++) {
        if (index == player_index) {
            continue;
        }
        const AttrSet *defaults = entity_resolve_defaults(state, level->entities.data[index].id);
        DrawRectangleLinesEx(entity_collision_rect(&level->entities.data[index], defaults), 1, RED);
    }

    /* Position dots (white) for all entities */
    for (int index = 0; index < level->entities.count; index++) {
        Vector2 pos = level->entities.data[index].position;
        DrawRectangle((int)pos.x - 1, (int)pos.y - 1, 3, 3, WHITE);
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

    Font font = state->assets.ui_font;
    draw_ui_text(font, "v" SLEIPNER_VERSION "  " __DATE__, DEBUG_MARGIN, DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT),
                 DEBUG_FONT_SIZE, debug_text_color);
    draw_ui_text(font, TextFormat("FPS: %d  frame: %d  t: %.1fs", GetFPS(), state->frame, state->elapsed), DEBUG_MARGIN,
                 DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);
    draw_ui_text(font, TextFormat("arena: %zu bytes", arena_used(&state->gamedata_arena)), DEBUG_MARGIN,
                 DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);
    draw_ui_text(font, TextFormat("screen: %dx%d", state->screen_width, state->screen_height), DEBUG_MARGIN,
                 DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);
    draw_ui_text(font, TextFormat("GetScreen: %dx%d  GetRender: %dx%d", screen_w, screen_h, render_w, render_h),
                 DEBUG_MARGIN, DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);
    draw_ui_text(font, TextFormat("game: %ux%u  scale: %d", game_bounds.width, game_bounds.height, PIXEL_SCALE),
                 DEBUG_MARGIN, DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);

    if (player) {
        draw_ui_text(
            font, TextFormat("player: %.1f, %.1f  row: %d", player->position.x, player->position.y, player->anim_row),
            DEBUG_MARGIN, DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);
        const AttrSet *player_defaults = entity_resolve_defaults(state, player->id);
        Rectangle player_col = entity_collision_rect(player, player_defaults);
        draw_ui_text(font,
                     TextFormat("collision: %.0f,%.0f %.0fx%.0f", player_col.x, player_col.y, player_col.width,
                                player_col.height),
                     DEBUG_MARGIN, DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);
    }

    draw_ui_text(font, TextFormat("gamepads: %d", count_connected_gamepads()), DEBUG_MARGIN,
                 DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);

    for (int index = 0; index < 4; index++) {
        if (IsGamepadAvailable(index)) {
            draw_ui_text(font, TextFormat("  gp%d: %s", index, GetGamepadName(index)), DEBUG_MARGIN,
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
            draw_ui_text(font, debug_get_line(&state->debug, index), DEBUG_MARGIN,
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
    texture_registry_add(state, "floor.png", load_embedded_texture(ASSET(floor_png)), &gamedata_alloc);
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

    EmbeddedAsset ui_font_asset = ASSET(golden_apple_ttf);
    state->assets.ui_font =
        LoadFontFromMemory(".ttf", ui_font_asset.data, ui_font_asset.size, DEBUG_FONT_SIZE, nullptr, 0);
    debug_log(&state->debug, "ui_font: Golden Apple %dpx valid=%d", DEBUG_FONT_SIZE,
              IsFontValid(state->assets.ui_font));

    /* Defaults are loaded in game_init. Overlay the TOML file (currently
     * a stub) on top so user customizations win once that lands. Bindings
     * live alongside textures and fonts: persistent, below gamedata_base,
     * freed only at game exit. */
    if (!input_func_load_bindings_toml(&state->bindings, gamedata_alloc, &state->error, nullptr)) {
        debug_log(&state->debug, "input bindings: %s", error_get(&state->error));
        error_clear(&state->error);
    }
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
    if (IsFontValid(state->assets.ui_font)) {
        UnloadFont(state->assets.ui_font);
    }
}

static void draw_font_preview(GameState *state)
{
    int panel_x = state->screen_width / 2;
    int line_spacing = FONT_PREVIEW_SIZE + DEBUG_MARGIN;
    int panel_height = (state->assets.font_previews.count * (DEBUG_LINE_HEIGHT + line_spacing)) + DEBUG_MARGIN +
                       (DEBUG_LINE_HEIGHT * 3);
    DrawRectangle(panel_x, 0, state->screen_width - panel_x, panel_height, debug_bg_color);

    Font font = state->assets.ui_font;
    int y_offset = DEBUG_MARGIN;
    draw_ui_text(font, "Font Preview - All fonts loaded at 32px", panel_x + DEBUG_MARGIN, y_offset, DEBUG_FONT_SIZE,
                 debug_text_color);
    y_offset += DEBUG_LINE_HEIGHT;
    draw_ui_text(font, "(Visual size varies by font design)", panel_x + DEBUG_MARGIN, y_offset, DEBUG_FONT_SIZE,
                 debug_text_color);
    y_offset += DEBUG_LINE_HEIGHT;
    draw_ui_text(font, TextFormat("Showing %d fonts total", state->assets.font_previews.count), panel_x + DEBUG_MARGIN,
                 y_offset, DEBUG_FONT_SIZE, debug_text_color);
    y_offset += DEBUG_LINE_HEIGHT;

    for (int index = 0; index < state->assets.font_previews.count; index++) {
        if (!state->assets.font_previews.data[index].valid) {
            continue;
        }
        draw_ui_text(font, TextFormat("%d. %s", index + 1, state->assets.font_previews.data[index].name),
                     panel_x + DEBUG_MARGIN, y_offset, DEBUG_FONT_SIZE, debug_text_color);
        y_offset += DEBUG_LINE_HEIGHT;
        DrawTextEx(state->assets.font_previews.data[index].font, "The quick brown fox 0123456789",
                   (Vector2){(float)(panel_x + DEBUG_MARGIN), (float)y_offset}, FONT_PREVIEW_SIZE, 1, WHITE);
        y_offset += line_spacing;
    }
}

#define MAX_GAMEDATA_SIZE (256UL * 1024)
#define MAX_PATH_LEN 512
#define COPY_BUFFER_SIZE 4096

static bool backup_file(GameState *state, const char *path)
{
    char backup_path[MAX_PATH_LEN];
    (void)snprintf(backup_path, MAX_PATH_LEN, "%s.bak", path);

    FILE *source = fopen(path, FOPEN_READ);
    if (!source) {
        error_set(&state->error, "backup fopen(%s): %s", path, strerror(errno));
        return false;
    }

    FILE *dest = fopen(backup_path, FOPEN_WRITE);
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

    /* Build a combined level array: current level first, then other levels.
     * Allocate on scratch arena to avoid permanent allocation. */
    SCRATCH_SCOPE(&state->scratch_arena);
    int total_levels = 1 + state->gamedata.other_levels.count;
    Level *all_levels = (Level *)arena_alloc(&state->scratch_arena, sizeof(Level) * (size_t)total_levels);
    all_levels[0] = state->gamedata.current_level;
    for (int index = 0; index < state->gamedata.other_levels.count; index++) {
        all_levels[1 + index] = state->gamedata.other_levels.data[index];
    }

    char buffer[MAX_GAMEDATA_SIZE];
    int written = toml_emit_gamedata(&state->error, buffer, (int)sizeof(buffer), &state->gamedata.blueprints,
                                     all_levels, total_levels);
    if (written < 0) {
        error_wrap(&state->error, "save_gamedata");
        return false;
    }

    FILE *file = fopen(GAMEDATA_PATH, FOPEN_WRITE);
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

    FILE *file = fopen(path, FOPEN_READ);
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

static void load_gamedata(Diag *diag, GameState *state, const char *level_name)
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
                                                      .level_name = level_name,
                                                      .texture_lookup = texture_registry_lookup,
                                                      .texture_user_data = state});

    if (loaded) {
        debug_log(diag->debug, "gamedata: %d blueprints", state->gamedata.blueprints.entries.count);
        for (int index = 0; index < state->gamedata.blueprints.entries.count; index++) {
            const Blueprint *blueprint = &state->gamedata.blueprints.entries.data[index];
            debug_log(diag->debug, "  bp[%d]: '%s' tex='%s' attrs=%d", index,
                      attr_get_string(&blueprint->attrs, "name"), attr_get_string(&blueprint->attrs, "texture"),
                      blueprint->attrs.entries.count);
        }
        debug_log(diag->debug, "gamedata: level '%s' (%dx%d, %d entities)", state->gamedata.current_level.name.ptr,
                  state->gamedata.current_level.width, state->gamedata.current_level.height,
                  state->gamedata.current_level.entities.count);
        for (int index = 0; index < state->gamedata.current_level.entities.count; index++) {
            const Entity *entity = &state->gamedata.current_level.entities.data[index];
            debug_log(diag->debug, "  ent[%d]: bp='%s' pos=(%.0f,%.0f) tex=%s", index, entity->blueprint_name.ptr,
                      entity->position.x, entity->position.y, entity->texture ? "ok" : "nullptr");
        }
        if (state->gamedata.player_index >= 0) {
            debug_log(diag->debug, "gamedata: player at entity[%d]", state->gamedata.player_index);
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
        load_gamedata(diag, state, nullptr);
        return true;
    }

    long current_mtime = GetFileModTime(GAMEDATA_PATH);
    if (current_mtime > 0 && current_mtime != state->gamedata_mtime) {
        debug_log(diag->debug, "gamedata: hot-reload triggered");
        load_gamedata(diag, state, nullptr);
        return true;
    }
    return false;
}

static void handle_transition(Diag *diag, GameState *state, UndoHistory *undo_history)
{
    if (!state->transition.pending) {
        return;
    }
    undo_history_clear(undo_history);
    state->transition.pending = false;
    float spawn_x = state->transition.x;
    float spawn_y = state->transition.y;
    SCRATCH_SCOPE(&state->scratch_arena);
    Allocator scratch_alloc = allocator_arena(&state->scratch_arena);
    Str level_name = str_new(scratch_alloc);
    (void)str_from_strv(&level_name, str_to_strv(state->transition.level));
    debug_log(diag->debug, "transition to '%s' at (%.0f, %.0f)", level_name.ptr, spawn_x, spawn_y);
    load_gamedata(diag, state, level_name.ptr);
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
            const AttrSet *defaults =
                entity_resolve_defaults(state, state->gamedata.current_level.entities.data[index].id);
            state->gamedata.prev_player_overlaps.data[index] = CheckCollisionRecs(
                entity_collision_rect(player, player_defaults),
                entity_collision_rect(&state->gamedata.current_level.entities.data[index], defaults));
        }
    }
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Level loaded"));
}

/* Reset editor-side state after a gamedata load: drop the old undo chain,
 * push a fresh baseline snapshot, and clear transient editor UI state.
 * The sentinel -1 init is load-bearing — a zero-init EditorState has
 * radial_confirmed = 0 which is a valid radial sector and silently breaks
 * handle_browse_input. Any reload path must go through here. */
static void reset_editor_after_reload(GameState *state,
                                      EditorState *editor_state,
                                      WatchList *watches,
                                      UndoHistory *undo_history,
                                      Strv baseline_description)
{
    undo_history_clear(undo_history);
    undo_history_new_entry(undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           baseline_description);
    *editor_state = (EditorState){.top_mode = EDITOR_TOP_SCENE,
                                  .selected_entity_index = -1,
                                  .sub_mode = EDITOR_SUB_BROWSE,
                                  .selected_attr_index = -1,
                                  .radial_confirmed = -1,
                                  .radial_selected = -1,
                                  .selected_blueprint_index = -1,
                                  .blueprint_attr_index = -1,
                                  .blueprint_tree_index = -1};
    *watches = (WatchList){0};
}

static void handle_hot_reload(
    Diag *diag, GameState *state, EditorState *editor_state, WatchList *watches, UndoHistory *undo_history)
{
    if (poll_hot_reload(diag, state)) {
        reset_editor_after_reload(state, editor_state, watches, undo_history, strv_from_cstr("Reload"));
    }
}

static void draw_entities_depth_sorted(GameState *state)
{
    SCRATCH_SCOPE(&state->scratch_arena);
    Allocator alloc = allocator_arena(&state->scratch_arena);

    const Level *level = &state->gamedata.current_level;
    int *sorted = sort_entities_by_depth(level->entities.data, level->entities.count, &alloc);

    for (int draw_index = 0; draw_index < level->entities.count; draw_index++) {
        int entity_index = sorted[draw_index];
        if (entity_index == state->gamedata.player_index) {
            draw_player_entity(state, &level->entities.data[entity_index]);
        } else {
            draw_entity(state, &level->entities.data[entity_index]);
        }
    }

    if (state->debug_enabled) {
        draw_debug_collision_boxes(state, level, state->gamedata.player_index);
    }
}

/* Save and reload are dispatched from the pause menu (engine/src/menu.c).
 * Their direct keybindings (formerly F9 / F10 + Select) were removed
 * when the menu took ownership; see menu_dispatch_save and
 * menu_dispatch_restore below for the live call sites. */
static void menu_dispatch_save(Diag *diag, GameState *state, EditorState *editor_state, UndoHistory *undo_history)
{
    if (!save_gamedata(state)) {
        debug_log(diag->debug, "save error: %s", error_get(diag->error));
        error_clear(diag->error);
        editor_state->toast_text = strv_from_cstr("Save failed");
    } else {
        state->gamedata_mtime = GetFileModTime(GAMEDATA_PATH);
        undo_history_mark_saved(undo_history);
        debug_log(diag->debug, "gamedata saved");
        editor_state->toast_text = strv_from_cstr("Saved");
    }
    editor_state->toast_timer = 2.0F;
}

static void menu_dispatch_restore(
    Diag *diag, GameState *state, EditorState *editor_state, WatchList *watches, UndoHistory *undo_history)
{
    bool was_dirty = undo_history_is_dirty(undo_history);
    debug_log(diag->debug, "gamedata: menu reload requested (dirty=%s)", was_dirty ? "yes" : "no");
    load_gamedata(diag, state, nullptr);
    reset_editor_after_reload(state, editor_state, watches, undo_history, strv_from_cstr("Menu reload"));
    editor_state->toast_text = strv_from_cstr("Reloaded from disk");
    editor_state->toast_timer = 2.0F;
}

typedef struct {
    RenderTexture2D target;
    RectU32 game_bounds;
    Camera2D editor_camera;
    bool font_preview_enabled;
    bool is_dirty;
    EditorState editor_state;
    const WatchList *watches;
    /* menu and blur are mutable: render_frame lazily calls blur_capture
     * the first frame the menu is open and flips menu->blur_captured. */
    MenuState *menu;
    BlurPipeline *blur;
} RenderParams;

static void render_frame(GameState *state, RenderParams params)
{
    BeginTextureMode(params.target);
    ClearBackground(BLACK);

    /* Gameplay camera: split into integer target (pixel-perfect in low-res buffer)
     * and fractional remainder (applied as sub-pixel offset during upscale blit).
     * Without this, camera jumps in PIXEL_SCALE-sized steps on screen. */
    Vector2 cam = state->gamedata.camera_target;
    Vector2 int_cam = {floorf(cam.x), floorf(cam.y)};
    Vector2 frac_cam = {cam.x - int_cam.x, cam.y - int_cam.y};

    Camera2D gameplay_camera = {
        .offset = {(float)params.game_bounds.width / 2.0F, (float)params.game_bounds.height / 2.0F},
        .target = int_cam,
        .zoom = 1.0F,
    };

    if (state->editor_mode) {
        BeginMode2D(params.editor_camera);
        frac_cam = (Vector2){0};
    } else {
        BeginMode2D(gameplay_camera);
    }
    const char *bg_tile = state->gamedata.current_level.background_tile.ptr
                              ? state->gamedata.current_level.background_tile.ptr
                              : "grass.png";
    RectU32 floor_bounds = {(uint32_t)state->gamedata.current_level.floor_width,
                            (uint32_t)state->gamedata.current_level.floor_height};
    draw_background_tiles(*texture_registry_lookup(bg_tile, state), floor_bounds,
                          state->gamedata.current_level.background_tint);
    draw_entities_depth_sorted(state);
    if (state->editor_mode) {
        int hover_index = find_nearest_entity(&state->gamedata.current_level, params.editor_camera.target);
        draw_editor_highlights(state, &params.editor_state, hover_index);
        draw_collision_handles(state, &params.editor_state);
        draw_place_preview(state, &params.editor_state, params.editor_camera);
    }
    EndMode2D();
    if (state->editor_mode) {
        draw_editor_crosshair(params.game_bounds);
    }
    EndTextureMode();

    BeginDrawing();
    DrawTexturePro(params.target.texture,
                   (Rectangle){0, 0, (float)params.game_bounds.width, -(float)params.game_bounds.height},
                   (Rectangle){-frac_cam.x * PIXEL_SCALE, -frac_cam.y * PIXEL_SCALE,
                               (float)state->screen_width + PIXEL_SCALE, (float)state->screen_height + PIXEL_SCALE},
                   (Vector2){0, 0}, 0.0F, WHITE);
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
        } else if (params.editor_state.sub_mode == EDITOR_SUB_WORD_BUILDER ||
                   params.editor_state.sub_mode == EDITOR_SUB_GAMEPAD_KB) {
            draw_word_builder_panel(screen, state, &params.editor_state);
        } else if (params.editor_state.sub_mode == EDITOR_SUB_FUZZY_FINDER) {
            draw_fuzzy_finder_panel(screen, state, &params.editor_state);
        } else if (params.editor_state.top_mode == EDITOR_TOP_BLUEPRINT) {
            if (params.editor_state.selected_blueprint_index >= 0) {
                draw_blueprint_detail_panel(screen, state, &params.editor_state);
            } else {
                draw_blueprint_list_panel(screen, state, &params.editor_state);
            }
        } else {
            draw_editor_panel(screen, state, &params.editor_state);
        }
    }
    draw_watch_overlay(screen, state, params.watches);
    draw_radial_picker(screen, &params.editor_state, state->assets.ui_font);
    draw_gamepad_kb(screen, &params.editor_state, state->assets.ui_font);
    if (state->editor_mode) {
        draw_toast(&params.editor_state, screen, state->assets.ui_font);
    }
    draw_hints_bar(state->editor_mode, &params.editor_state, params.is_dirty, screen, state->assets.ui_font);
    if (params.menu->open && !params.menu->blur_captured) {
        blur_capture(params.blur, params.target.texture);
        params.menu->blur_captured = true;
    }
    menu_render(params.menu, params.blur, state->screen_width, state->screen_height);
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

#ifdef _WIN32
    /* GUI subsystem has no console — stdout is dead. Reopen it to the trace
     * file so raylib's built-in TraceLog output is captured. */
    (void)freopen(TRACE_LOG_PATH, FOPEN_APPEND, stdout);
#endif

#if defined(__linux__) && !defined(__ANDROID__)
    /* Skip libdecor on Wayland. We run borderless-fullscreen so the
     * decoration title bar is never drawn, and libdecor-gtk pulls in
     * GTK + Pango + Fontconfig (~535 KB of process-lifetime allocations
     * those libraries do not free on shutdown). With this hint, GLFW
     * uses xdg-decoration-unstable-v1 server-side decorations when the
     * compositor offers them, otherwise its own bare fallback. No-op
     * on X11. */
    glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_WAYLAND_DISABLE_LIBDECOR);
#endif
    debug_log(&state->debug, "calling InitWindow");
#ifdef __ANDROID__
    SetConfigFlags(FLAG_FULLSCREEN_MODE);
    InitWindow(1920, 1080, "Sleipner");
    state->screen_width = 1920;
    state->screen_height = 1080;
#else
    InitWindow(SCREEN_WIDTH_DEFAULT, SCREEN_HEIGHT_DEFAULT, "Sleipner");
#endif
    debug_log(&state->debug, "InitWindow done");
    SetExitKey(KEY_NULL);
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
    debug_log(&state->debug, "calling ToggleBorderlessWindowed");
    ToggleBorderlessWindowed();
    debug_log(&state->debug, "ToggleBorderlessWindowed done");
#endif
    debug_log(&state->debug, "state->screen_width=%d state->screen_height=%d", state->screen_width,
              state->screen_height);

    SetTargetFPS(TARGET_FPS);
    debug_log(&state->debug, "calling audio_init");
    audio_init(&state->audio);
    debug_log(&state->debug, "audio_init done");

    debug_log(&state->debug, "loading bgm");
    EmbeddedAsset bgm_asset = ASSET(bgm_mp3);
    Music bgm = LoadMusicStreamFromMemory(".mp3", bgm_asset.data, bgm_asset.size);
    bgm.looping = true;
    PlayMusicStream(bgm);
    debug_log(&state->debug, "bgm loaded");

    /* Render target at game resolution for pixel-perfect scaling */
    RectU32 game_bounds = {(uint32_t)state->screen_width / PIXEL_SCALE, (uint32_t)state->screen_height / PIXEL_SCALE};
    debug_log(&state->debug, "loading render texture %ux%u", game_bounds.width, game_bounds.height);
    RenderTexture2D target = LoadRenderTexture((int)game_bounds.width, (int)game_bounds.height);
    debug_log(&state->debug, "render texture loaded");

    debug_log(&state->debug, "calling game_init");
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
    Camera2D editor_camera = {
        .offset = {(float)game_bounds.width / 2.0F, (float)game_bounds.height / 2.0F},
        .target = {(float)game_bounds.width / 2.0F, (float)game_bounds.height / 2.0F},
        .zoom = 1.0F,
    };
    EditorState editor_state = {.top_mode = EDITOR_TOP_SCENE,
                                .selected_entity_index = -1,
                                .sub_mode = EDITOR_SUB_BROWSE,
                                .selected_attr_index = -1,
                                .radial_confirmed = -1,
                                .radial_selected = -1,
                                .selected_blueprint_index = -1,
                                .blueprint_attr_index = -1,
                                .blueprint_tree_index = -1};
    WatchList watches = {0};
    UndoHistory undo_history = {0};
    if (!undo_history_init(&state->error, &undo_history)) {
        debug_log(&state->debug, "error: %s", error_get(&state->error));
        error_clear(&state->error);
        return 1;
    }

    debug_log(&state->debug, "gamedata path: %s", GAMEDATA_PATH);
    debug_log(&state->debug, "screen %dx%d  game %ux%u  scale %d", state->screen_width, state->screen_height,
              game_bounds.width, game_bounds.height, PIXEL_SCALE);
    debug_log(&state->debug, "GetScreen %dx%d  GetRender %dx%d", GetScreenWidth(), GetScreenHeight(), GetRenderWidth(),
              GetRenderHeight());
    load_gamedata(diag, state, nullptr);
    undo_history_new_entry(&undo_history, &state->gamedata, &state->gamedata_arena, state->gamedata_base,
                           strv_from_cstr("Initial"));

    /* Pause-overlay menu and its blur backdrop. Blur is sized to the
     * low-res scene render-texture so blur_capture can sample the
     * already-rendered scene directly. */
    MenuState menu = {0};
    menu_init(&menu);
    EmbeddedAsset menu_font_asset = ASSET(cardboardcrown_ttf);
    menu_set_font(&menu,
                  LoadFontFromMemory(".ttf", menu_font_asset.data, menu_font_asset.size, MENU_FONT_SIZE, nullptr, 0));
    BlurPipeline blur = {0};
    blur_init(&blur, (int)game_bounds.width, (int)game_bounds.height);
    bool quit_requested = false;

    FrameContext frame_ctx = {
        .editor_state = &editor_state,
        .editor_camera = &editor_camera,
        .watches = &watches,
        .undo_history = &undo_history,
        .menu = &menu,
        .font_preview_enabled = &font_preview_enabled,
        .quit_requested = &quit_requested,
        .save_fn = menu_dispatch_save,
        .restore_fn = menu_dispatch_restore,
    };

    while (!WindowShouldClose() && !quit_requested) {
        float delta_time = GetFrameTime();

        UpdateMusicStream(bgm);

        /* Hot-reload: poll mtime and reload if gamedata changed */
        if (state->frame % HOT_RELOAD_POLL_FRAMES == 0) {
            handle_hot_reload(diag, state, &editor_state, &watches, &undo_history);
        }

        InputState input = {0};
        input_capture(&input);

        log_gamepad_changes(state, &prev_gamepads, state->frame);

        frame_update(diag, state, &frame_ctx, input, delta_time);

        handle_transition(diag, state, &undo_history);

        render_frame(state, (RenderParams){
                                .target = target,
                                .game_bounds = game_bounds,
                                .editor_camera = editor_camera,
                                .font_preview_enabled = font_preview_enabled,
                                .is_dirty = undo_history_is_dirty(&undo_history),
                                .editor_state = editor_state,
                                .watches = &watches,
                                .menu = &menu,
                                .blur = &blur,
                            });
    }

    debug_log(&state->debug, "exiting game loop (frame=%d t=%.1fs)", state->frame, state->elapsed);

    blur_cleanup(&blur);
    menu_cleanup(&menu);
    undo_history_free(&undo_history);
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
