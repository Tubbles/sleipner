#include "engine_context.h"
#include "raylib.h"

#include "assets.h"
#include "blueprint.h"
#include "debug.h"
#include "entity.h"
#include "game.h"
#include "input.h"
#include "level.h"
#include "rect.h"
#include "screen.h"
#include "toml_emitter.h"

#include "error.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
#define DEBUG_LOG_LINES 20
#define DEBUG_LOG_LINE_LEN 128
#define DEBUG_FONT_SIZE 20
#define DEBUG_LINE_HEIGHT 22
#define DEBUG_MARGIN 8
#define DEBUG_BG_ALPHA 180
#define DEBUG_PANEL_WIDTH 420
#define DEBUG_LINES 10

int screen_width = SCREEN_WIDTH_DEFAULT;
int screen_height = SCREEN_HEIGHT_DEFAULT;

/* Texture registry — maps texture filenames to loaded Texture2D handles */
#define MAX_TEXTURES 64
#define MAX_TEXTURE_FILENAME 64

typedef struct {
    char filename[MAX_TEXTURE_FILENAME];
    Texture2D texture;
} TextureEntry;

static TextureEntry texture_registry[MAX_TEXTURES];
static int texture_registry_count = 0;

static void texture_registry_add(const char *filename, Texture2D texture)
{
    if (texture_registry_count >= MAX_TEXTURES) {
        return;
    }
    TextureEntry *entry = &texture_registry[texture_registry_count];
    strncpy(entry->filename, filename, MAX_TEXTURE_FILENAME - 1);
    entry->filename[MAX_TEXTURE_FILENAME - 1] = '\0';
    entry->texture = texture;
    texture_registry_count++;
}

static Texture2D *texture_registry_lookup(const char *filename, void *user_data)
{
    (void)user_data;
    for (int index = 0; index < texture_registry_count; index++) {
        if (strcmp(texture_registry[index].filename, filename) == 0) {
            return &texture_registry[index].texture;
        }
    }
    return NULL;
}

/* Font preview registry */
#define MAX_PREVIEW_FONTS 16
#define FONT_PREVIEW_SIZE 32
#define FONT_NAME_LEN 64

typedef struct {
    char name[FONT_NAME_LEN];
    Font font;
    bool valid;
} FontPreviewEntry;

static FontPreviewEntry font_preview_entries[MAX_PREVIEW_FONTS];
static int font_preview_count = 0;

static void font_preview_init(struct EngineContext *ctx)
{
    /* Reset font preview system to clean state */
    font_preview_count = 0;
    for (int index = 0; index < MAX_PREVIEW_FONTS; index++) {
        font_preview_entries[index].valid = false;
        font_preview_entries[index].name[0] = '\0';
    }
    debug_log(ctx, "font_preview: initialized (%d max entries)", MAX_PREVIEW_FONTS);
}

static Texture2D load_embedded_texture(EmbeddedAsset asset)
{
    Image image = LoadImageFromMemory(".png", asset.data, asset.size);
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}

static void font_preview_add(struct EngineContext *ctx, const char *name, EmbeddedAsset asset)
{
    if (font_preview_count >= MAX_PREVIEW_FONTS) {
        return;
    }
    FontPreviewEntry *entry = &font_preview_entries[font_preview_count];
    strncpy(entry->name, name, FONT_NAME_LEN - 1);
    entry->name[FONT_NAME_LEN - 1] = '\0';
    entry->font = LoadFontFromMemory(".ttf", asset.data, asset.size, FONT_PREVIEW_SIZE, NULL, 0);
    entry->valid = IsFontValid(entry->font);
    if (entry->valid) {
        debug_log(ctx, "font[%d]: '%s' (%d bytes)", font_preview_count, name, asset.size);
    } else {
        debug_log(ctx, "font[%d]: '%s' failed to load", font_preview_count, name);
    }
    font_preview_count++;
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
        base.buttons[index] = (bool)(base.buttons[index] || overlay.buttons[index]);
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

typedef struct {
    int key;
    int gamepad_button;
} ToggleBinding;

static bool toggle_pressed(ToggleBinding binding)
{
    if (IsKeyPressed(binding.key)) {
        return true;
    }
    return IsGamepadButtonPressed(0, binding.gamepad_button);
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

static void draw_entity(const Entity *entity)
{
    DrawTextureRec(*entity->texture, entity->source, entity->position, WHITE);
}

static void log_gamepad_changes(struct EngineContext *ctx, int *prev_gamepads, int frame)
{
    int gamepads = input_count_gamepads();
    if (gamepads != *prev_gamepads) {
        debug_log(ctx, "gamepads %d -> %d (frame %d)", *prev_gamepads, gamepads, frame);
        for (int index = 0; index < 4; index++) {
            if (IsGamepadAvailable(index)) {
                debug_log(ctx, "gp%d: %s", index, GetGamepadName(index));
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
    if (player_index >= 0 && player_index < level->entity_count) {
        const Entity *player = &level->entities[player_index];
        DrawRectangleLinesEx(player->collision, 1, GREEN);

        /* Player sprite bounds (yellow) */
        Rectangle sprite = {player->position.x - (FRAME_SIZE / 2.0F), player->position.y - (FRAME_SIZE / 2.0F),
                            FRAME_SIZE, FRAME_SIZE};
        DrawRectangleLinesEx(sprite, 1, YELLOW);
    }

    /* Entity collision boxes (red) */
    for (int index = 0; index < level->entity_count; index++) {
        if (index == player_index) {
            continue;
        }
        DrawRectangleLinesEx(level->entities[index].collision, 1, RED);
    }
}

static const Color debug_text_color = {200, 220, 240, 255};
static const Color debug_log_color = {180, 210, 180, 255};
static const Color debug_bg_color = {20, 25, 35, DEBUG_BG_ALPHA};

static void
draw_debug_info(struct EngineContext *ctx, const Entity *player, RectU32 game_bounds, int frame, float elapsed)
{
    int line = 0;
    int screen_w = GetScreenWidth();
    int screen_h = GetScreenHeight();
    int render_w = GetRenderWidth();
    int render_h = GetRenderHeight();

    /* Semi-transparent background for info panel */
    int panel_width = DEBUG_PANEL_WIDTH;
    int panel_height = (DEBUG_LINES * DEBUG_LINE_HEIGHT) + (DEBUG_MARGIN * 2);
    DrawRectangle(0, 0, panel_width, panel_height, debug_bg_color);

    DrawText(TextFormat("FPS: %d  frame: %d  t: %.1fs", GetFPS(), frame, elapsed), DEBUG_MARGIN,
             DEBUG_MARGIN + (line++ * DEBUG_LINE_HEIGHT), DEBUG_FONT_SIZE, debug_text_color);
    DrawText(TextFormat("screen: %dx%d", screen_width, screen_height), DEBUG_MARGIN,
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
    int line_count = debug_get_line_count(ctx);
    if (line_count > 0) {
        int log_height = (line_count * DEBUG_LINE_HEIGHT) + (DEBUG_MARGIN * 2);
        int log_y = screen_height - log_height;
        DrawRectangle(0, log_y, screen_width, log_height, debug_bg_color);

        for (int index = 0; index < line_count; index++) {
            DrawText(debug_get_line(ctx, index), DEBUG_MARGIN, log_y + DEBUG_MARGIN + (index * DEBUG_LINE_HEIGHT),
                     DEBUG_FONT_SIZE, debug_log_color);
        }
    }
}

static void font_preview_cleanup(void)
{
    for (int index = 0; index < font_preview_count; index++) {
        if (font_preview_entries[index].valid) {
            UnloadFont(font_preview_entries[index].font);
        }
    }
}

static void draw_font_preview(void)
{
    int panel_x = screen_width / 2;
    int line_spacing = FONT_PREVIEW_SIZE + DEBUG_MARGIN;
    int panel_height =
        (font_preview_count * (DEBUG_LINE_HEIGHT + line_spacing)) + DEBUG_MARGIN + (DEBUG_LINE_HEIGHT * 3);
    DrawRectangle(panel_x, 0, screen_width - panel_x, panel_height, debug_bg_color);

    int y_offset = DEBUG_MARGIN;
    DrawText("Font Preview - All fonts loaded at 32px", panel_x + DEBUG_MARGIN, y_offset, DEBUG_FONT_SIZE,
             debug_text_color);
    y_offset += DEBUG_LINE_HEIGHT;
    DrawText("(Visual size varies by font design)", panel_x + DEBUG_MARGIN, y_offset, DEBUG_FONT_SIZE,
             debug_text_color);
    y_offset += DEBUG_LINE_HEIGHT;
    DrawText(TextFormat("Showing %d fonts total", font_preview_count), panel_x + DEBUG_MARGIN, y_offset,
             DEBUG_FONT_SIZE, debug_text_color);
    y_offset += DEBUG_LINE_HEIGHT;

    for (int index = 0; index < font_preview_count; index++) {
        if (!font_preview_entries[index].valid) {
            continue;
        }
        DrawText(TextFormat("%d. %s", index + 1, font_preview_entries[index].name), panel_x + DEBUG_MARGIN, y_offset,
                 DEBUG_FONT_SIZE, debug_text_color);
        y_offset += DEBUG_LINE_HEIGHT;
        DrawTextEx(font_preview_entries[index].font, "The quick brown fox 0123456789",
                   (Vector2){(float)(panel_x + DEBUG_MARGIN), (float)y_offset}, FONT_PREVIEW_SIZE, 1, WHITE);
        y_offset += line_spacing;
    }
}

static long gamedata_mtime = 0;

#define MAX_GAMEDATA_SIZE (256UL * 1024)
#define MAX_PATH_LEN 512
#define COPY_BUFFER_SIZE 4096

static bool backup_file(struct EngineContext *ctx, const char *path)
{
    char backup_path[MAX_PATH_LEN];
    (void)snprintf(backup_path, MAX_PATH_LEN, "%s.bak", path);

    FILE *source = fopen(path, "re");
    if (!source) {
        error_set(ctx, "backup fopen(%s): %s", path, strerror(errno));
        return false;
    }

    FILE *dest = fopen(backup_path, "we");
    if (!dest) {
        error_set(ctx, "backup fopen(%s): %s", backup_path, strerror(errno));
        (void)fclose(source);
        return false;
    }

    char buffer[COPY_BUFFER_SIZE];
    for (;;) {
        size_t bytes = fread(buffer, 1, sizeof(buffer), source);
        if (bytes > 0 && fwrite(buffer, 1, bytes, dest) != bytes) {
            error_set(ctx, "backup fwrite(%s): %s", backup_path, strerror(errno));
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
        error_set(ctx, "backup fread(%s): %s", path, strerror(errno));
        return false;
    }

    debug_log(ctx, "backup: %s -> %s", path, backup_path);
    return true;
}

static bool save_gamedata(struct EngineContext *ctx, const GameState *state)
{
    if (!backup_file(ctx, GAMEDATA_PATH)) {
        error_wrap(ctx, "save_gamedata");
        return false;
    }

    char buffer[MAX_GAMEDATA_SIZE];
    int written = toml_emit_gamedata(ctx, buffer, (int)sizeof(buffer), &state->blueprints, &state->current_level, 1);
    if (written < 0) {
        error_wrap(ctx, "save_gamedata");
        return false;
    }

    FILE *file = fopen(GAMEDATA_PATH, "we");
    if (!file) {
        error_set(ctx, "fopen(%s): %s", GAMEDATA_PATH, strerror(errno));
        error_wrap(ctx, "save_gamedata");
        return false;
    }

    size_t to_write = (size_t)written;
    if (fwrite(buffer, 1, to_write, file) != to_write) {
        error_set(ctx, "fwrite(%s): %s", GAMEDATA_PATH, strerror(errno));
        (void)fclose(file);
        error_wrap(ctx, "save_gamedata");
        return false;
    }
    (void)fclose(file);

    debug_log(ctx, "saved gamedata: %d bytes to %s", written, GAMEDATA_PATH);
    return true;
}

static char *read_file_text(struct EngineContext *ctx, const char *path)
{
    /* Stat the file first for diagnostics */
    struct stat file_stat;
    if (stat(path, &file_stat) != 0) {
        error_set(ctx, "stat(%s): %s", path, strerror(errno));
        return NULL;
    }
    debug_log(ctx, "gamedata: stat(%s): size=%ld mode=%o uid=%d gid=%d", path, (long)file_stat.st_size,
              (unsigned)file_stat.st_mode, (int)file_stat.st_uid, (int)file_stat.st_gid);

    FILE *file = fopen(path, "re");
    if (!file) {
        error_set(ctx, "fopen(%s): %s", path, strerror(errno));
        return NULL;
    }

    char *buffer = malloc(MAX_GAMEDATA_SIZE + 1);
    if (!buffer) {
        error_set(ctx, "malloc(%lu): %s", MAX_GAMEDATA_SIZE + 1, strerror(errno));
        (void)fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, MAX_GAMEDATA_SIZE, file);
    if (ferror(file)) {
        error_set(ctx, "fread(%s): %s", path, strerror(errno));
        free(buffer);
        (void)fclose(file);
        return NULL;
    }
    (void)fclose(file);

    if (bytes_read > MAX_GAMEDATA_SIZE) {
        error_set(ctx, "file too large: %zu bytes (max %lu)", bytes_read, MAX_GAMEDATA_SIZE);
        free(buffer);
        return NULL;
    }
    buffer[bytes_read] = '\0';
    debug_log(ctx, "gamedata: read %zu bytes from %s", bytes_read, path);
    return buffer;
}

static void load_gamedata(struct EngineContext *ctx, GameState *state)
{
    char *content = read_file_text(ctx, GAMEDATA_PATH);
    if (!content) {
        error_wrap(ctx, "load_gamedata");
        debug_log(ctx, "error: %s", error_get(ctx));
        error_clear(ctx);
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
    debug_log(ctx, "gamedata: hex[0..%d]: %s", hex_count - 1, hexbuf);

    bool loaded = game_load_gamedata(
        ctx, state, (GamedataParams){.toml_string = content, .texture_lookup = texture_registry_lookup});
    free(content);

    if (loaded) {
        debug_log(ctx, "gamedata: %d blueprints", state->blueprints.count);
        for (int index = 0; index < state->blueprints.count; index++) {
            const Blueprint *blueprint = &state->blueprints.entries[index];
            debug_log(ctx, "  bp[%d]: '%s' tex='%s' attrs=%d", index, blueprint->name, blueprint->texture_name,
                      blueprint->attrs.count);
        }
        debug_log(ctx, "gamedata: level '%s' (%dx%d, %d entities)", state->current_level.name,
                  state->current_level.width, state->current_level.height, state->current_level.entity_count);
        for (int index = 0; index < state->current_level.entity_count; index++) {
            const Entity *entity = &state->current_level.entities[index];
            debug_log(ctx, "  ent[%d]: bp='%s' pos=(%.0f,%.0f) tex=%s", index, entity->blueprint_name,
                      entity->position.x, entity->position.y, entity->texture ? "ok" : "NULL");
        }
        if (state->player_index >= 0) {
            debug_log(ctx, "gamedata: player at entity[%d]", state->player_index);
        } else {
            debug_log(ctx, "gamedata: WARNING player not found!");
        }
    } else {
        debug_log(ctx, "error: %s", error_get(ctx));
        error_clear(ctx);
    }

    gamedata_mtime = GetFileModTime(GAMEDATA_PATH);
}

static void poll_hot_reload(struct EngineContext *ctx, GameState *state)
{
    if (!state->gamedata_loaded) {
        load_gamedata(ctx, state);
        return;
    }

    long current_mtime = GetFileModTime(GAMEDATA_PATH);
    if (current_mtime > 0 && current_mtime != gamedata_mtime) {
        debug_log(ctx, "gamedata: hot-reload triggered");
        load_gamedata(ctx, state);
    }
}

static void draw_entities_depth_sorted(const GameState *state)
{
    const Entity *player = game_get_player_const(state);
    float player_sort_y = player ? player->position.y + 16 : 0;

    for (int index = 0; index < state->current_level.entity_count; index++) {
        if (index == state->player_index) {
            continue;
        }
        float entity_sort_y =
            state->current_level.entities[index].collision.y + state->current_level.entities[index].collision.height;
        if (entity_sort_y <= player_sort_y) {
            draw_entity(&state->current_level.entities[index]);
        }
    }

    if (player) {
        draw_player_entity(player);
    }

    for (int index = 0; index < state->current_level.entity_count; index++) {
        if (index == state->player_index) {
            continue;
        }
        float entity_sort_y =
            state->current_level.entities[index].collision.y + state->current_level.entities[index].collision.height;
        if (entity_sort_y > player_sort_y) {
            draw_entity(&state->current_level.entities[index]);
        }
    }

    if (state->debug_enabled) {
        draw_debug_collision_boxes(&state->current_level, state->player_index);
    }
}

int main(void)
{
    struct EngineContext ctx_val = {0};
    struct EngineContext *ctx = &ctx_val;

    debug_init(ctx, TRACE_LOG_PATH);

#ifdef __ANDROID__
    SetConfigFlags(FLAG_FULLSCREEN_MODE);
    InitWindow(1920, 1080, "Sleipner");
    screen_width = 1920;
    screen_height = 1080;
#else
    InitWindow(SCREEN_WIDTH_DEFAULT, SCREEN_HEIGHT_DEFAULT, "Sleipner");
#endif
    HideCursor();

#ifndef __ANDROID__
    int monitor = GetCurrentMonitor();
    int mon_width = GetMonitorWidth(monitor);
    int mon_height = GetMonitorHeight(monitor);
    debug_log(ctx, "monitor=%d resolution=%dx%d", monitor, mon_width, mon_height);
    if (mon_width > 0 && mon_height > 0) {
        screen_width = mon_width;
        screen_height = mon_height;
        SetWindowSize(screen_width, screen_height);
    }
#endif
#ifndef __ANDROID__
    ToggleBorderlessWindowed();
#endif
    debug_log(ctx, "screen_width=%d screen_height=%d", screen_width, screen_height);

#ifndef __ANDROID__
    EmbeddedAsset gamepad_asset = ASSET(gamecontrollerdb_txt);
    input_load_mappings(ctx, (const char *)gamepad_asset.data, gamepad_asset.size);
#endif
    SetTargetFPS(TARGET_FPS);
    InitAudioDevice();

    /* Load textures from embedded assets */
    texture_registry_add("player.png", load_embedded_texture(ASSET(player_png)));
    texture_registry_add("grass.png", load_embedded_texture(ASSET(grass_png)));
    texture_registry_add("tree.png", load_embedded_texture(ASSET(tree_png)));
    texture_registry_add("chest.png", load_embedded_texture(ASSET(chest_png)));
    texture_registry_add("house.png", load_embedded_texture(ASSET(house_png)));
    texture_registry_add("fence.png", load_embedded_texture(ASSET(fence_png)));
    for (int index = 0; index < texture_registry_count; index++) {
        debug_log(ctx, "texture[%d]: '%s' id=%u %dx%d", index, texture_registry[index].filename,
                  texture_registry[index].texture.id, texture_registry[index].texture.width,
                  texture_registry[index].texture.height);
    }
    /* Initialize and load fonts */
    font_preview_init(ctx);
    font_preview_add(ctx, "Earth Illusion", ASSET(earth_illusion_ttf));
    font_preview_add(ctx, "Golden Apple", ASSET(golden_apple_ttf));
    font_preview_add(ctx, "MenuCard", ASSET(menucard_ttf));
    font_preview_add(ctx, "Nudge Orb", ASSET(nudge_orb_ttf));
    font_preview_add(ctx, "CardboardCrown", ASSET(cardboardcrown_ttf));
    font_preview_add(ctx, "RoyalFibre", ASSET(royalfibre_ttf));

    EmbeddedAsset bgm_asset = ASSET(bgm_mp3);
    Music bgm = LoadMusicStreamFromMemory(".mp3", bgm_asset.data, bgm_asset.size);
    bgm.looping = true;
    PlayMusicStream(bgm);

    /* Render target at game resolution for pixel-perfect scaling */
    RectU32 game_bounds = {(uint32_t)screen_width / PIXEL_SCALE, (uint32_t)screen_height / PIXEL_SCALE};
    RenderTexture2D target = LoadRenderTexture((int)game_bounds.width, (int)game_bounds.height);

    GameState state;
    if (!game_init(ctx, &state, game_bounds)) {
        debug_log(ctx, "error: %s", error_get(ctx));
        error_clear(ctx);
        return 1;
    }

    int prev_gamepads = -1;
    bool font_preview_enabled = false;

    debug_log(ctx, "gamedata path: %s", GAMEDATA_PATH);
    debug_log(ctx, "screen %dx%d  game %ux%u  scale %d", screen_width, screen_height, game_bounds.width,
              game_bounds.height, PIXEL_SCALE);
    debug_log(ctx, "GetScreen %dx%d  GetRender %dx%d", GetScreenWidth(), GetScreenHeight(), GetRenderWidth(),
              GetRenderHeight());
    load_gamedata(ctx, &state);

    while (!WindowShouldClose()) {
        float delta_time = GetFrameTime();

        UpdateMusicStream(bgm);

        /* Hot-reload: poll mtime and reload if gamedata changed */
        if (state.frame % HOT_RELOAD_POLL_FRAMES == 0) {
            poll_hot_reload(ctx, &state);
        }

        /* Toggle debug overlay: F3 or gamepad Select */
        if (toggle_pressed((ToggleBinding){KEY_F3, GAMEPAD_BUTTON_MIDDLE_LEFT})) {
            state.debug_enabled = (bool)!state.debug_enabled;
            debug_log(ctx, "debug %s (frame %d)", (int)state.debug_enabled ? "ON" : "OFF", state.frame);
        }

        /* Toggle font preview: F4 or gamepad Right Thumb */
        if (toggle_pressed((ToggleBinding){KEY_F4, GAMEPAD_BUTTON_RIGHT_THUMB})) {
            font_preview_enabled = (bool)!font_preview_enabled;
        }

        log_gamepad_changes(ctx, &prev_gamepads, state.frame);

        if (any_gamepad_exit_requested()) {
            goto quit;
        }

        InputState input = read_all_input();

        /* Update (pure logic — no rendering) */
        game_update(ctx, &state, input, delta_time);

        /* Heartbeat every ~5 seconds */
        if (state.frame % HEARTBEAT_INTERVAL == 0) {
            debug_log(ctx, "frame=%d t=%.1fs dt=%.4f fps=%d", state.frame, state.elapsed, delta_time, GetFPS());
        }

        /* Render at game resolution with depth sorting */
        BeginTextureMode(target);
        ClearBackground(BLACK);

        draw_grass(*texture_registry_lookup("grass.png", NULL), game_bounds);
        draw_entities_depth_sorted(&state);

        EndTextureMode();

        /* Scale game render to screen */
        BeginDrawing();
        DrawTexturePro(target.texture, (Rectangle){0, 0, (float)game_bounds.width, -(float)game_bounds.height},
                       (Rectangle){0, 0, (float)screen_width, (float)screen_height}, (Vector2){0, 0}, 0.0F, WHITE);

        if (state.debug_enabled) {
            draw_debug_info(ctx, game_get_player_const(&state), game_bounds, state.frame, state.elapsed);
        }
        if (font_preview_enabled) {
            draw_font_preview();
        }
        EndDrawing();
    }

quit:
    debug_log(ctx, "exiting game loop (frame=%d t=%.1fs)", state.frame, state.elapsed);

    UnloadMusicStream(bgm);
    UnloadRenderTexture(target);
    for (int index = 0; index < texture_registry_count; index++) {
        UnloadTexture(texture_registry[index].texture);
    }
    font_preview_cleanup();
    game_free(&state);
    debug_shutdown(ctx);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
