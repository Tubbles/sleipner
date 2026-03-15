#include "raylib.h"

#include "audio.h"
#include "input.h"
#include "rect.h"
#include "screen.h"

#include "assets.h"

#include "zlog.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

#define HEARTBEAT_INTERVAL 300
#define TARGET_FPS 60

#define PIXEL_SCALE 3
#define TILE_SIZE 16
#define FRAME_SIZE 32
#define PLAYER_SPEED 80.0F
#define WALK_FRAMES 6
#define ANIM_SPEED 10.0F
#define MAX_OBSTACLES 32

enum {
    ANIM_IDLE_DOWN = 0,
    ANIM_IDLE_UP = 2,
    ANIM_WALK_DOWN = 3,
    ANIM_WALK_SIDE = 4,
    ANIM_WALK_UP = 5,
};

typedef struct {
    Vector2 position;
    int anim_row;
    bool flip;
    float frame_timer;
    int frame_index;
    bool moving;
} Player;

typedef struct {
    Vector2 position;
    Texture2D *texture;
    Rectangle source;
    Rectangle collision;
} Obstacle;

int screen_width = SCREEN_WIDTH_DEFAULT;
int screen_height = SCREEN_HEIGHT_DEFAULT;

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

static void update_player(Player *player, InputState input, float delta_time, RectU32 bounds)
{
    player->moving = false;

    if (input.left_stick.x != 0.0F || input.left_stick.y != 0.0F) {
        player->position.x += input.left_stick.x * PLAYER_SPEED * delta_time;
        player->position.y += input.left_stick.y * PLAYER_SPEED * delta_time;
        player->moving = true;

        if (fabsf(input.left_stick.x) > fabsf(input.left_stick.y)) {
            player->anim_row = ANIM_WALK_SIDE;
            player->flip = input.left_stick.x < 0.0F;
        } else if (input.left_stick.y > 0.0F) {
            player->anim_row = ANIM_WALK_DOWN;
        } else {
            player->anim_row = ANIM_WALK_UP;
        }
    }

    /* Clamp to game bounds */
    float half = FRAME_SIZE / 2.0F;
    if (player->position.x < half) {
        player->position.x = half;
    }
    if (player->position.y < half) {
        player->position.y = half;
    }
    if (player->position.x > (float)bounds.width - half) {
        player->position.x = (float)bounds.width - half;
    }
    if (player->position.y > (float)bounds.height - half) {
        player->position.y = (float)bounds.height - half;
    }

    /* Animate walk cycle */
    if (player->moving) {
        player->frame_timer += delta_time * ANIM_SPEED;
        if (player->frame_timer >= 1.0F) {
            player->frame_timer -= 1.0F;
            player->frame_index = (player->frame_index + 1) % WALK_FRAMES;
        }
    } else {
        player->frame_index = 0;
        player->frame_timer = 0.0F;
    }
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

static Rectangle player_hitbox(const Player *player)
{
    return (Rectangle){player->position.x - 5, player->position.y + 6, 10, 10};
}

static void resolve_player_obstacles(Player *player, const Obstacle *obstacles, int count)
{
    for (int index = 0; index < count; index++) {
        Rectangle hitbox = player_hitbox(player);
        Rectangle obstacle = obstacles[index].collision;
        if (!CheckCollisionRecs(hitbox, obstacle)) {
            continue;
        }
        float push_left = (hitbox.x + hitbox.width) - obstacle.x;
        float push_right = (obstacle.x + obstacle.width) - hitbox.x;
        float push_up = (hitbox.y + hitbox.height) - obstacle.y;
        float push_down = (obstacle.y + obstacle.height) - hitbox.y;

        float min_push = push_left;
        int direction = 0;
        if (push_right < min_push) {
            min_push = push_right;
            direction = 1;
        }
        if (push_up < min_push) {
            min_push = push_up;
            direction = 2;
        }
        if (push_down < min_push) {
            min_push = push_down;
            direction = 3;
        }

        if (direction == 0) {
            player->position.x -= push_left;
        } else if (direction == 1) {
            player->position.x += push_right;
        } else if (direction == 2) {
            player->position.y -= push_up;
        } else {
            player->position.y += push_down;
        }
    }
}

static void draw_obstacle(const Obstacle *obstacle)
{
    DrawTextureRec(*obstacle->texture, obstacle->source, obstacle->position, WHITE);
}

static void log_gamepad_changes(int *prev_gamepads, int frame)
{
    int gamepads = input_count_gamepads();
    if (gamepads != *prev_gamepads) {
        dzlog_info("gamepads %d -> %d (frame=%d)", *prev_gamepads, gamepads, frame);
        for (int index = 0; index < 4; index++) {
            if (IsGamepadAvailable(index)) {
                dzlog_info("gamepad %d: %s", index, GetGamepadName(index));
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

    InitWindow(SCREEN_WIDTH_DEFAULT, SCREEN_HEIGHT_DEFAULT, "Sleipner");
    HideCursor();

    int monitor = GetCurrentMonitor();
    int mon_width = GetMonitorWidth(monitor);
    int mon_height = GetMonitorHeight(monitor);
    dzlog_info("monitor=%d resolution=%dx%d", monitor, mon_width, mon_height);
    if (mon_width > 0 && mon_height > 0) {
        screen_width = mon_width;
        screen_height = mon_height;
        SetWindowSize(screen_width, screen_height);
    }
#ifndef __ANDROID__
    ToggleBorderlessWindowed();
#endif
    dzlog_info("screen_width=%d screen_height=%d", screen_width, screen_height);

#ifndef __ANDROID__
    input_load_mappings((const char *)asset_gamecontrollerdb, sizeof(asset_gamecontrollerdb));
#endif
    SetTargetFPS(TARGET_FPS);
    audio_init();

    srand((unsigned)time(NULL));

    /* Load textures */
#ifdef __ANDROID__
    Texture2D tex_player = LoadTexture("sprites/player.png");
    Texture2D tex_grass = LoadTexture("sprites/grass.png");
    Texture2D tex_tree = LoadTexture("sprites/tree.png");
    Texture2D tex_chest = LoadTexture("sprites/chest.png");
    Texture2D tex_house = LoadTexture("sprites/house.png");
    Texture2D tex_fence = LoadTexture("sprites/fence.png");
#else
    Texture2D tex_player = load_embedded_texture(asset_player_png, sizeof(asset_player_png));
    Texture2D tex_grass = load_embedded_texture(asset_grass_png, sizeof(asset_grass_png));
    Texture2D tex_tree = load_embedded_texture(asset_tree_png, sizeof(asset_tree_png));
    Texture2D tex_chest = load_embedded_texture(asset_chest_png, sizeof(asset_chest_png));
    Texture2D tex_house = load_embedded_texture(asset_house_png, sizeof(asset_house_png));
    Texture2D tex_fence = load_embedded_texture(asset_fence_png, sizeof(asset_fence_png));
#endif

    /* Render target at game resolution for pixel-perfect scaling */
    RectU32 game_bounds = {(uint32_t)screen_width / PIXEL_SCALE, (uint32_t)screen_height / PIXEL_SCALE};
    RenderTexture2D target = LoadRenderTexture((int)game_bounds.width, (int)game_bounds.height);

    Player player = {
        .position = {(float)game_bounds.width / 2.0F, (float)game_bounds.height / 2.0F},
        .anim_row = ANIM_IDLE_DOWN,
        .flip = false,
        .frame_timer = 0.0F,
        .frame_index = 0,
        .moving = false,
    };

    /* Place obstacles — collision rects are in world space */
    /* Fence source rects: top-left 16x48 vertical segment from 64x64 sheet */
    Obstacle obstacles[] = {
        /* House */
        {.position = {40, 20}, .texture = &tex_house, .source = {0, 0, 96, 128}, .collision = {40, 84, 96, 64}},
        /* Trees */
        {.position = {200, 60}, .texture = &tex_tree, .source = {0, 0, 64, 80}, .collision = {220, 120, 24, 16}},
        {.position = {350, 150}, .texture = &tex_tree, .source = {0, 0, 64, 80}, .collision = {370, 210, 24, 16}},
        {.position = {80, 200}, .texture = &tex_tree, .source = {0, 0, 64, 80}, .collision = {100, 260, 24, 16}},
        {.position = {450, 40}, .texture = &tex_tree, .source = {0, 0, 64, 80}, .collision = {470, 100, 24, 16}},
        {.position = {500, 220}, .texture = &tex_tree, .source = {0, 0, 64, 80}, .collision = {520, 280, 24, 16}},
        /* Chests */
        {.position = {300, 100}, .texture = &tex_chest, .source = {0, 0, 16, 16}, .collision = {300, 100, 16, 16}},
        {.position = {160, 280}, .texture = &tex_chest, .source = {0, 0, 16, 16}, .collision = {160, 280, 16, 16}},
        /* Fences (vertical segment, 16x48 from top-left of sheet) */
        {.position = {260, 50}, .texture = &tex_fence, .source = {0, 0, 16, 48}, .collision = {260, 50, 16, 48}},
        {.position = {260, 98}, .texture = &tex_fence, .source = {0, 0, 16, 48}, .collision = {260, 98, 16, 48}},
    };
    int obstacle_count = sizeof(obstacles) / sizeof(obstacles[0]);

    int frame = 0;
    float elapsed = 0.0F;
    int prev_gamepads = -1;

    dzlog_info("entering game loop (game_res=%ux%u scale=%d)", game_bounds.width, game_bounds.height, PIXEL_SCALE);

    while (!WindowShouldClose()) {
        float delta_time = GetFrameTime();
        frame++;
        elapsed += delta_time;

        /* Heartbeat every ~5 seconds */
        if (frame % HEARTBEAT_INTERVAL == 0) {
            dzlog_debug("frame=%d t=%.1fs dt=%.4f fps=%d", frame, elapsed, delta_time, GetFPS());
        }

        log_gamepad_changes(&prev_gamepads, frame);

        if (any_gamepad_exit_requested()) {
            goto quit;
        }

        /* Read input (gamepad 0 + keyboard merged) */
        InputState input = input_read_keyboard();
        if (IsGamepadAvailable(0)) {
            input = merge_input(input, input_read(0));
        }

        update_player(&player, input, delta_time, game_bounds);
        resolve_player_obstacles(&player, obstacles, obstacle_count);

        /* Render at game resolution with depth sorting */
        BeginTextureMode(target);
        ClearBackground(BLACK);

        draw_grass(tex_grass, game_bounds);

        float player_sort_y = player.position.y + 16;
        /* Draw obstacles behind player */
        for (int index = 0; index < obstacle_count; index++) {
            float obstacle_sort_y = obstacles[index].collision.y + obstacles[index].collision.height;
            if (obstacle_sort_y <= player_sort_y) {
                draw_obstacle(&obstacles[index]);
            }
        }
        draw_player(&player, tex_player);
        /* Draw obstacles in front of player */
        for (int index = 0; index < obstacle_count; index++) {
            float obstacle_sort_y = obstacles[index].collision.y + obstacles[index].collision.height;
            if (obstacle_sort_y > player_sort_y) {
                draw_obstacle(&obstacles[index]);
            }
        }

        EndTextureMode();

        /* Scale game render to screen */
        BeginDrawing();
        DrawTexturePro(target.texture, (Rectangle){0, 0, (float)game_bounds.width, -(float)game_bounds.height},
                       (Rectangle){0, 0, (float)screen_width, (float)screen_height}, (Vector2){0, 0}, 0.0F, WHITE);
        EndDrawing();
    }

quit:
    dzlog_info("exiting game loop (frame=%d t=%.1fs)", frame, elapsed);

    UnloadRenderTexture(target);
    UnloadTexture(tex_player);
    UnloadTexture(tex_grass);
    UnloadTexture(tex_tree);
    UnloadTexture(tex_chest);
    UnloadTexture(tex_house);
    UnloadTexture(tex_fence);
    audio_shutdown();
    CloseWindow();
#ifndef __ANDROID__
    zlog_fini();
#endif
    return 0;
}
