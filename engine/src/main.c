#include "raylib.h"

#include "blueprint.h"
#include "debug.h"
#include "entity.h"
#include "game.h"
#include "input.h"
#include "level.h"
#include "rect.h"
#include "screen.h"

#include <stdint.h>
#include <string.h>

#ifdef __ANDROID__
#define ASSET_PREFIX ""
#define SYNCTHING_PATH "/storage/emulated/0/Sync"
#define GAMEDATA_PATH SYNCTHING_PATH "/sleipner/gamedata.toml"
#define TRACE_LOG_PATH SYNCTHING_PATH "/sleipner/trace.log"
#else
#define ASSET_PREFIX "assets/"
#define GAMEDATA_PATH "data/gamedata.toml"
#define TRACE_LOG_PATH "trace.log"
#endif

#define HEARTBEAT_INTERVAL 300
#define TARGET_FPS 60
#define HOT_RELOAD_POLL_FRAMES 30

#define PIXEL_SCALE 3
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

static void log_gamepad_changes(int *prev_gamepads, int frame)
{
    int gamepads = input_count_gamepads();
    if (gamepads != *prev_gamepads) {
        debug_log("gamepads %d -> %d (frame %d)", *prev_gamepads, gamepads, frame);
        for (int index = 0; index < 4; index++) {
            if (IsGamepadAvailable(index)) {
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

static void draw_debug_info(const Entity *player, RectU32 game_bounds, int frame, float elapsed)
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
    int line_count = debug_get_line_count();
    if (line_count > 0) {
        int log_height = (line_count * DEBUG_LINE_HEIGHT) + (DEBUG_MARGIN * 2);
        int log_y = screen_height - log_height;
        DrawRectangle(0, log_y, screen_width, log_height, debug_bg_color);

        for (int index = 0; index < line_count; index++) {
            DrawText(debug_get_line(index), DEBUG_MARGIN, log_y + DEBUG_MARGIN + (index * DEBUG_LINE_HEIGHT),
                     DEBUG_FONT_SIZE, debug_log_color);
        }
    }
}

static long gamedata_mtime = 0;

static void load_gamedata(GameState *state)
{
    char *content = LoadFileText(GAMEDATA_PATH);
    if (!content) {
        debug_log("gamedata: FAILED to open %s", GAMEDATA_PATH);
        return;
    }

    debug_log("gamedata: loaded %s (%d bytes, first=0x%02x)", GAMEDATA_PATH, (int)strlen(content),
              (unsigned char)content[0]);

    bool loaded =
        game_load_gamedata(state, (GamedataParams){.toml_string = content, .texture_lookup = texture_registry_lookup});
    UnloadFileText(content);

    if (loaded) {
        debug_log("gamedata: %d blueprints", state->blueprints.count);
        for (int index = 0; index < state->blueprints.count; index++) {
            const Blueprint *blueprint = &state->blueprints.entries[index];
            debug_log("  bp[%d]: '%s' tex='%s' attrs=%d", index, blueprint->name, blueprint->texture_name,
                      blueprint->attrs.count);
        }
        debug_log("gamedata: level '%s' (%dx%d, %d entities)", state->current_level.name, state->current_level.width,
                  state->current_level.height, state->current_level.entity_count);
        for (int index = 0; index < state->current_level.entity_count; index++) {
            const Entity *entity = &state->current_level.entities[index];
            debug_log("  ent[%d]: bp='%s' pos=(%.0f,%.0f) tex=%s", index, entity->blueprint_name, entity->position.x,
                      entity->position.y, entity->texture ? "ok" : "NULL");
        }
        if (state->player_index >= 0) {
            debug_log("gamedata: player at entity[%d]", state->player_index);
        } else {
            debug_log("gamedata: WARNING player not found!");
        }
    } else {
        debug_log("gamedata: no level found");
    }

    gamedata_mtime = GetFileModTime(GAMEDATA_PATH);
}

static void poll_hot_reload(GameState *state)
{
    if (!state->gamedata_loaded) {
        load_gamedata(state);
        return;
    }

    long current_mtime = GetFileModTime(GAMEDATA_PATH);
    if (current_mtime > 0 && current_mtime != gamedata_mtime) {
        debug_log("gamedata: hot-reload triggered");
        load_gamedata(state);
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
    debug_init(TRACE_LOG_PATH);

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
    debug_log("monitor=%d resolution=%dx%d", monitor, mon_width, mon_height);
    if (mon_width > 0 && mon_height > 0) {
        screen_width = mon_width;
        screen_height = mon_height;
        SetWindowSize(screen_width, screen_height);
    }
#endif
#ifndef __ANDROID__
    ToggleBorderlessWindowed();
#endif
    debug_log("screen_width=%d screen_height=%d", screen_width, screen_height);

#ifndef __ANDROID__
    input_load_mappings(ASSET_PREFIX "gamecontrollerdb.txt");
#endif
    SetTargetFPS(TARGET_FPS);
    InitAudioDevice();

    /* Load textures and register them by filename */
    texture_registry_add("player.png", LoadTexture(ASSET_PREFIX "sprites/player.png"));
    texture_registry_add("grass.png", LoadTexture(ASSET_PREFIX "sprites/grass.png"));
    texture_registry_add("tree.png", LoadTexture(ASSET_PREFIX "sprites/tree.png"));
    texture_registry_add("chest.png", LoadTexture(ASSET_PREFIX "sprites/chest.png"));
    texture_registry_add("house.png", LoadTexture(ASSET_PREFIX "sprites/house.png"));
    texture_registry_add("fence.png", LoadTexture(ASSET_PREFIX "sprites/fence.png"));
    for (int index = 0; index < texture_registry_count; index++) {
        debug_log("texture[%d]: '%s' id=%u %dx%d", index, texture_registry[index].filename,
                  texture_registry[index].texture.id, texture_registry[index].texture.width,
                  texture_registry[index].texture.height);
    }
    Music bgm = LoadMusicStream(ASSET_PREFIX "music/bgm.mp3");
    bgm.looping = true;
    PlayMusicStream(bgm);

    /* Render target at game resolution for pixel-perfect scaling */
    RectU32 game_bounds = {(uint32_t)screen_width / PIXEL_SCALE, (uint32_t)screen_height / PIXEL_SCALE};
    RenderTexture2D target = LoadRenderTexture((int)game_bounds.width, (int)game_bounds.height);

    GameState state;
    game_init(&state, game_bounds);

    int prev_gamepads = -1;

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
            poll_hot_reload(&state);
        }

        /* Toggle debug overlay: F3 or gamepad Select */
        if (IsKeyPressed(KEY_F3) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_LEFT)) {
            state.debug_enabled = (bool)!state.debug_enabled;
            debug_log("debug %s (frame %d)", (int)state.debug_enabled ? "ON" : "OFF", state.frame);
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
            debug_log("frame=%d t=%.1fs dt=%.4f fps=%d", state.frame, state.elapsed, delta_time, GetFPS());
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
            draw_debug_info(game_get_player_const(&state), game_bounds, state.frame, state.elapsed);
        }
        EndDrawing();
    }

quit:
    debug_log("exiting game loop (frame=%d t=%.1fs)", state.frame, state.elapsed);

    UnloadMusicStream(bgm);
    UnloadRenderTexture(target);
    for (int index = 0; index < texture_registry_count; index++) {
        UnloadTexture(texture_registry[index].texture);
    }
    game_free(&state);
    debug_shutdown();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
