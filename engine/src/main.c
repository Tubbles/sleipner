#include "raylib.h"

#include "game.h"
#include "input.h"
#include "rect.h"
#include "screen.h"

#include "assets.h"

#include "toml.h"
#include "zlog.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef __ANDROID__
#define SYNCTHING_PATH "/storage/emulated/0/Sync"
#define GAMEDATA_PATH SYNCTHING_PATH "/sleipner/gamedata.toml"
#else
#define GAMEDATA_PATH "data/gamedata.toml"
#endif

#define HEARTBEAT_INTERVAL 300
#define TARGET_FPS 60
#define HOT_RELOAD_POLL_FRAMES 30

#define PIXEL_SCALE 3
#define TILE_SIZE 16
#define DEBUG_LOG_LINES 20
#define DEBUG_LOG_LINE_LEN 128
#define DEBUG_FONT_SIZE 16
#define DEBUG_LINE_HEIGHT 18
#define DEBUG_MARGIN 6
#define DEBUG_BG_ALPHA 160

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

static void debug_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

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

static Texture2D load_embedded_texture(const unsigned char *data, int size)
{
    Image image = LoadImageFromMemory(".png", data, size);
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}

static void draw_player(const Player *player, Texture2D texture)
{
    float source_width = (float)FRAME_SIZE;
    if (player->flip) {
        source_width = -source_width;
    }
    Rectangle source = {(float)(player->frame_index * FRAME_SIZE), (float)(player->anim_row * FRAME_SIZE), source_width,
                        FRAME_SIZE};
    Rectangle dest = {player->position.x - (FRAME_SIZE / 2.0F), player->position.y - (FRAME_SIZE / 2.0F), FRAME_SIZE,
                      FRAME_SIZE};
    DrawTexturePro(texture, source, dest, (Vector2){0, 0}, 0.0F, WHITE);
}

static void draw_obstacle(const LevelEntity *obstacle)
{
    DrawTextureRec(*obstacle->texture, obstacle->source, obstacle->position, WHITE);
}

static void log_gamepad_changes(int *prev_gamepads, int frame)
{
    int gamepads = input_count_gamepads();
    if (gamepads != *prev_gamepads) {
        dzlog_info("gamepads %d -> %d (frame=%d)", *prev_gamepads, gamepads, frame);
        debug_log("gamepads %d -> %d (frame %d)", *prev_gamepads, gamepads, frame);
        for (int index = 0; index < 4; index++) {
            if (IsGamepadAvailable(index)) {
                dzlog_info("gamepad %d: %s", index, GetGamepadName(index));
                debug_log("gp%d: %s", index, GetGamepadName(index));
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

/* --- Debug overlay --- */

static char debug_log_lines[DEBUG_LOG_LINES][DEBUG_LOG_LINE_LEN];
static int debug_log_head = 0;
static int debug_log_count = 0;

static void debug_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(debug_log_lines[debug_log_head], DEBUG_LOG_LINE_LEN, format, args);
    va_end(args);
    debug_log_head = (debug_log_head + 1) % DEBUG_LOG_LINES;
    if (debug_log_count < DEBUG_LOG_LINES) {
        debug_log_count++;
    }
}

static void draw_debug_collision_boxes(const Player *player, const LevelEntity *obstacles, int obstacle_count)
{
    /* Player hitbox (green) */
    Rectangle hitbox = game_player_hitbox(player);
    DrawRectangleLinesEx(hitbox, 1, GREEN);

    /* Player sprite bounds (yellow) */
    Rectangle sprite = {player->position.x - FRAME_SIZE / 2.0F, player->position.y - FRAME_SIZE / 2.0F, FRAME_SIZE,
                        FRAME_SIZE};
    DrawRectangleLinesEx(sprite, 1, YELLOW);

    /* Obstacle collision boxes (red) */
    for (int index = 0; index < obstacle_count; index++) {
        DrawRectangleLinesEx(obstacles[index].collision, 1, RED);
    }
}

static void draw_debug_info(const Player *player, RectU32 game_bounds, int frame, float elapsed)
{
    int line = 0;
    int screen_w = GetScreenWidth();
    int screen_h = GetScreenHeight();
    int render_w = GetRenderWidth();
    int render_h = GetRenderHeight();

    /* Semi-transparent background for info panel */
    int panel_width = 360;
    int panel_height = 10 * DEBUG_LINE_HEIGHT + DEBUG_MARGIN * 2;
    DrawRectangle(0, 0, panel_width, panel_height, (Color){0, 0, 0, DEBUG_BG_ALPHA});

    DrawText(TextFormat("FPS: %d  frame: %d  t: %.1fs", GetFPS(), frame, elapsed), DEBUG_MARGIN,
             DEBUG_MARGIN + line++ * DEBUG_LINE_HEIGHT, DEBUG_FONT_SIZE, GREEN);
    DrawText(TextFormat("screen: %dx%d", screen_width, screen_height), DEBUG_MARGIN,
             DEBUG_MARGIN + line++ * DEBUG_LINE_HEIGHT, DEBUG_FONT_SIZE, GREEN);
    DrawText(TextFormat("GetScreen: %dx%d  GetRender: %dx%d", screen_w, screen_h, render_w, render_h), DEBUG_MARGIN,
             DEBUG_MARGIN + line++ * DEBUG_LINE_HEIGHT, DEBUG_FONT_SIZE, GREEN);
    DrawText(TextFormat("game: %ux%u  scale: %d", game_bounds.width, game_bounds.height, PIXEL_SCALE), DEBUG_MARGIN,
             DEBUG_MARGIN + line++ * DEBUG_LINE_HEIGHT, DEBUG_FONT_SIZE, GREEN);
    DrawText(TextFormat("player: %.1f, %.1f  row: %d", player->position.x, player->position.y, player->anim_row),
             DEBUG_MARGIN, DEBUG_MARGIN + line++ * DEBUG_LINE_HEIGHT, DEBUG_FONT_SIZE, GREEN);

    Rectangle hitbox = game_player_hitbox(player);
    DrawText(TextFormat("hitbox: %.0f,%.0f %.0fx%.0f", hitbox.x, hitbox.y, hitbox.width, hitbox.height), DEBUG_MARGIN,
             DEBUG_MARGIN + line++ * DEBUG_LINE_HEIGHT, DEBUG_FONT_SIZE, GREEN);
    DrawText(TextFormat("gamepads: %d", input_count_gamepads()), DEBUG_MARGIN,
             DEBUG_MARGIN + line++ * DEBUG_LINE_HEIGHT, DEBUG_FONT_SIZE, GREEN);

    for (int index = 0; index < 4; index++) {
        if (IsGamepadAvailable(index)) {
            DrawText(TextFormat("  gp%d: %s", index, GetGamepadName(index)), DEBUG_MARGIN,
                     DEBUG_MARGIN + line++ * DEBUG_LINE_HEIGHT, DEBUG_FONT_SIZE, GREEN);
        }
    }

    /* Log panel at bottom */
    if (debug_log_count > 0) {
        int log_height = debug_log_count * DEBUG_LINE_HEIGHT + DEBUG_MARGIN * 2;
        int log_y = screen_height - log_height;
        DrawRectangle(0, log_y, screen_width, log_height, (Color){0, 0, 0, DEBUG_BG_ALPHA});

        for (int index = 0; index < debug_log_count; index++) {
            int log_index = (debug_log_head - debug_log_count + index + DEBUG_LOG_LINES) % DEBUG_LOG_LINES;
            DrawText(debug_log_lines[log_index], DEBUG_MARGIN, log_y + DEBUG_MARGIN + index * DEBUG_LINE_HEIGHT,
                     DEBUG_FONT_SIZE, LIME);
        }
    }
}

static time_t gamedata_mtime = 0;

static time_t get_file_mtime(const char *path)
{
    struct stat file_stat;
    if (stat(path, &file_stat) != 0) {
        return 0;
    }
    return file_stat.st_mtime;
}

static void load_gamedata(GameState *state)
{
    FILE *file = fopen(GAMEDATA_PATH, "r");
    if (!file) {
        dzlog_warn("gamedata: could not open %s", GAMEDATA_PATH);
        debug_log("gamedata: FAILED to open %s", GAMEDATA_PATH);
        return;
    }

    char errbuf[200];
    toml_table_t *root = toml_parse_file(file, errbuf, sizeof(errbuf));
    fclose(file);

    if (!root) {
        dzlog_warn("gamedata: parse error: %s", errbuf);
        debug_log("gamedata: parse error: %s", errbuf);
        return;
    }

    arena_reset(&state->gamedata_arena);

    int blueprint_count = blueprints_load(&state->blueprints, root, &state->gamedata_arena);
    debug_log("gamedata: %d blueprints", blueprint_count);
    for (int index = 0; index < state->blueprints.count; index++) {
        debug_log("  blueprint: %s", state->blueprints.entries[index].name);
    }

    bool level_ok = level_load(&state->current_level, root, NULL, &state->blueprints, texture_registry_lookup, NULL);
    if (level_ok) {
        debug_log("gamedata: level '%s' (%dx%d, %d entities)", state->current_level.name, state->current_level.width,
                  state->current_level.height, state->current_level.entity_count);
    } else {
        debug_log("gamedata: no level found");
    }

    toml_free(root);
    state->gamedata_loaded = true;
    gamedata_mtime = get_file_mtime(GAMEDATA_PATH);
}

int main(void)
{
#ifndef __ANDROID__
    /* Init zlog from embedded config string */
    {
        char zlog_conf[sizeof(asset_zlog_conf) + 1];
        for (unsigned long index = 0; index < sizeof(asset_zlog_conf); index++) {
            zlog_conf[index] = (char)asset_zlog_conf[index];
        }
        zlog_conf[sizeof(asset_zlog_conf)] = '\0';
        zlog_init_from_string(zlog_conf);
        dzlog_set_category("sleipner");
    }
#else
    dzlog_set_category("sleipner");
#endif

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
    dzlog_info("monitor=%d resolution=%dx%d", monitor, mon_width, mon_height);
    if (mon_width > 0 && mon_height > 0) {
        screen_width = mon_width;
        screen_height = mon_height;
        SetWindowSize(screen_width, screen_height);
    }
#endif
#ifndef __ANDROID__
    ToggleBorderlessWindowed();
#endif
    dzlog_info("screen_width=%d screen_height=%d", screen_width, screen_height);

#ifndef __ANDROID__
    input_load_mappings((const char *)asset_gamecontrollerdb, sizeof(asset_gamecontrollerdb));
#endif
    SetTargetFPS(TARGET_FPS);
    InitAudioDevice();

    /* Load textures and register them by filename */
#ifdef __ANDROID__
    texture_registry_add("player.png", LoadTexture("sprites/player.png"));
    texture_registry_add("grass.png", LoadTexture("sprites/grass.png"));
    texture_registry_add("tree.png", LoadTexture("sprites/tree.png"));
    texture_registry_add("chest.png", LoadTexture("sprites/chest.png"));
    texture_registry_add("house.png", LoadTexture("sprites/house.png"));
    texture_registry_add("fence.png", LoadTexture("sprites/fence.png"));
    Music bgm = LoadMusicStream("music/bgm.mp3");
#else
    texture_registry_add("player.png", load_embedded_texture(asset_player_png, sizeof(asset_player_png)));
    texture_registry_add("grass.png", load_embedded_texture(asset_grass_png, sizeof(asset_grass_png)));
    texture_registry_add("tree.png", load_embedded_texture(asset_tree_png, sizeof(asset_tree_png)));
    texture_registry_add("chest.png", load_embedded_texture(asset_chest_png, sizeof(asset_chest_png)));
    texture_registry_add("house.png", load_embedded_texture(asset_house_png, sizeof(asset_house_png)));
    texture_registry_add("fence.png", load_embedded_texture(asset_fence_png, sizeof(asset_fence_png)));
    Music bgm = LoadMusicStreamFromMemory(".mp3", asset_bgm_mp3, sizeof(asset_bgm_mp3));
#endif
    bgm.looping = true;
    PlayMusicStream(bgm);

    /* Render target at game resolution for pixel-perfect scaling */
    RectU32 game_bounds = {(uint32_t)screen_width / PIXEL_SCALE, (uint32_t)screen_height / PIXEL_SCALE};
    RenderTexture2D target = LoadRenderTexture((int)game_bounds.width, (int)game_bounds.height);

    GameState state;
    game_init(&state, game_bounds);

    int prev_gamepads = -1;

    dzlog_info("entering game loop (game_res=%ux%u scale=%d)", game_bounds.width, game_bounds.height, PIXEL_SCALE);
    debug_log("screen %dx%d  game %ux%u  scale %d", screen_width, screen_height, game_bounds.width, game_bounds.height,
              PIXEL_SCALE);
    debug_log("GetScreen %dx%d  GetRender %dx%d", GetScreenWidth(), GetScreenHeight(), GetRenderWidth(),
              GetRenderHeight());
    load_gamedata(&state);

    while (!WindowShouldClose()) {
        float delta_time = GetFrameTime();

        UpdateMusicStream(bgm);

        /* Hot-reload: poll mtime and reload if gamedata changed */
        if (state.frame % HOT_RELOAD_POLL_FRAMES == 0) {
            if (!state.gamedata_loaded) {
                load_gamedata(&state);
            } else {
                time_t current_mtime = get_file_mtime(GAMEDATA_PATH);
                if (current_mtime > 0 && current_mtime != gamedata_mtime) {
                    dzlog_info("gamedata: file changed, reloading");
                    debug_log("gamedata: hot-reload triggered");
                    load_gamedata(&state);
                }
            }
        }

        /* Toggle debug overlay: F3 or gamepad Select */
        if (IsKeyPressed(KEY_F3) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_LEFT)) {
            state.debug_enabled = !state.debug_enabled;
            debug_log("debug %s (frame %d)", state.debug_enabled ? "ON" : "OFF", state.frame);
        }

        log_gamepad_changes(&prev_gamepads, state.frame);

        if (any_gamepad_exit_requested()) {
            goto quit;
        }

        /* Read input (gamepad 0 + keyboard merged) */
        InputState input = input_read_keyboard();
        if (IsGamepadAvailable(0)) {
            input = merge_input(input, input_read(0));
        }

        /* Update (pure logic — no rendering) */
        game_update(&state, input, delta_time);

        /* Heartbeat every ~5 seconds */
        if (state.frame % HEARTBEAT_INTERVAL == 0) {
            dzlog_debug("frame=%d t=%.1fs dt=%.4f fps=%d", state.frame, state.elapsed, delta_time, GetFPS());
        }

        /* Render at game resolution with depth sorting */
        BeginTextureMode(target);
        ClearBackground(BLACK);

        draw_grass(*texture_registry_lookup("grass.png", NULL), game_bounds);

        float player_sort_y = state.player.position.y + 16;
        /* Draw entities behind player */
        for (int index = 0; index < state.current_level.entity_count; index++) {
            float entity_sort_y =
                state.current_level.entities[index].collision.y + state.current_level.entities[index].collision.height;
            if (entity_sort_y <= player_sort_y) {
                draw_obstacle(&state.current_level.entities[index]);
            }
        }
        draw_player(&state.player, *texture_registry_lookup("player.png", NULL));
        /* Draw entities in front of player */
        for (int index = 0; index < state.current_level.entity_count; index++) {
            float entity_sort_y =
                state.current_level.entities[index].collision.y + state.current_level.entities[index].collision.height;
            if (entity_sort_y > player_sort_y) {
                draw_obstacle(&state.current_level.entities[index]);
            }
        }

        if (state.debug_enabled) {
            draw_debug_collision_boxes(&state.player, state.current_level.entities, state.current_level.entity_count);
        }

        EndTextureMode();

        /* Scale game render to screen */
        BeginDrawing();
        DrawTexturePro(target.texture, (Rectangle){0, 0, (float)game_bounds.width, -(float)game_bounds.height},
                       (Rectangle){0, 0, (float)screen_width, (float)screen_height}, (Vector2){0, 0}, 0.0F, WHITE);

        if (state.debug_enabled) {
            draw_debug_info(&state.player, game_bounds, state.frame, state.elapsed);
        }
        EndDrawing();
    }

quit:
    dzlog_info("exiting game loop (frame=%d t=%.1fs)", state.frame, state.elapsed);

    UnloadMusicStream(bgm);
    UnloadRenderTexture(target);
    for (int index = 0; index < texture_registry_count; index++) {
        UnloadTexture(texture_registry[index].texture);
    }
    game_free(&state);
    CloseAudioDevice();
    CloseWindow();
#ifndef __ANDROID__
    zlog_fini();
#endif
    return 0;
}
