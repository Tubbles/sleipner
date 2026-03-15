#include "raylib.h"

#include "audio.h"
#include "input.h"
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

// NOLINTBEGIN(bugprone-easily-swappable-parameters)
static void update_player(Player *player, InputState input, float delta_time, int game_width, int game_height)
// NOLINTEND(bugprone-easily-swappable-parameters)
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
    if (player->position.x > (float)game_width - half) {
        player->position.x = (float)game_width - half;
    }
    if (player->position.y > (float)game_height - half) {
        player->position.y = (float)game_height - half;
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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void draw_grass(Texture2D texture, int game_width, int game_height)
{
    for (int tile_y = 0; tile_y < game_height; tile_y += TILE_SIZE) {
        for (int tile_x = 0; tile_x < game_width; tile_x += TILE_SIZE) {
            DrawTexture(texture, tile_x, tile_y, WHITE);
        }
    }
}

int main(void)
{
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
    ToggleBorderlessWindowed();
    dzlog_info("screen_width=%d screen_height=%d", screen_width, screen_height);

    input_load_mappings((const char *)asset_gamecontrollerdb, sizeof(asset_gamecontrollerdb));
    SetTargetFPS(TARGET_FPS);
    audio_init();

    srand((unsigned)time(NULL));

    /* Load textures from embedded data */
    Texture2D tex_player = load_embedded_texture(asset_player_png, sizeof(asset_player_png));
    Texture2D tex_grass = load_embedded_texture(asset_grass_png, sizeof(asset_grass_png));

    /* Render target at game resolution for pixel-perfect scaling */
    int game_width = screen_width / PIXEL_SCALE;
    int game_height = screen_height / PIXEL_SCALE;
    RenderTexture2D target = LoadRenderTexture(game_width, game_height);

    Player player = {
        .position = {(float)game_width / 2.0F, (float)game_height / 2.0F},
        .anim_row = ANIM_IDLE_DOWN,
        .flip = false,
        .frame_timer = 0.0F,
        .frame_index = 0,
        .moving = false,
    };

    int frame = 0;
    float elapsed = 0.0F;
    int prev_gamepads = -1;

    dzlog_info("entering game loop (game_res=%dx%d scale=%d)", game_width, game_height, PIXEL_SCALE);

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

        update_player(&player, input, delta_time, game_width, game_height);

        /* Render at game resolution */
        BeginTextureMode(target);
        ClearBackground(BLACK);

        draw_grass(tex_grass, game_width, game_height);

        draw_player(&player, tex_player);

        EndTextureMode();

        /* Scale game render to screen */
        BeginDrawing();
        DrawTexturePro(target.texture, (Rectangle){0, 0, (float)game_width, -(float)game_height},
                       (Rectangle){0, 0, (float)screen_width, (float)screen_height}, (Vector2){0, 0}, 0.0F, WHITE);
        EndDrawing();
    }

quit:
    dzlog_info("exiting game loop (frame=%d t=%.1fs)", frame, elapsed);

    UnloadRenderTexture(target);
    UnloadTexture(tex_player);
    UnloadTexture(tex_grass);
    audio_shutdown();
    CloseWindow();
    zlog_fini();
    return 0;
}
