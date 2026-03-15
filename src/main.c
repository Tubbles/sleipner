#include "raylib.h"
#include "audio.h"
#include "input.h"
#include "particle.h"
#include "render.h"
#include "screen.h"
#include "assets.h"

#include "zlog.h"

#include <stdlib.h>
#include <time.h>

#define HEARTBEAT_INTERVAL 300
#define TARGET_FPS 60

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

    ParticlePool particles;
    particles_init(&particles);

    srand((unsigned)time(NULL));

    int frame = 0;
    float elapsed = 0.0F;
    int prev_gamepads = -1;

    dzlog_info("entering game loop");

    while (!WindowShouldClose()) {
        float delta_time = GetFrameTime();
        frame++;
        elapsed += delta_time;

        /* Heartbeat every ~5 seconds */
        if (frame % HEARTBEAT_INTERVAL == 0) {
            dzlog_debug("frame=%d t=%.1fs dt=%.4f fps=%d particles=%d", frame, elapsed, delta_time, GetFPS(),
                        particles.count);
        }

        /* Log gamepad connect/disconnect */
        int gamepads = input_count_gamepads();
        if (gamepads != prev_gamepads) {
            dzlog_info("gamepads %d -> %d (frame=%d)", prev_gamepads, gamepads, frame);
            for (int index = 0; index < 4; index++) {
                if (IsGamepadAvailable(index)) {
                    dzlog_info("gamepad %d: %s", index, GetGamepadName(index));
                }
            }
            prev_gamepads = gamepads;
        }

        /* Exit on Start+Select from any gamepad */
        for (int index = 0; index < 4; index++) {
            if (input_exit_requested(index)) {
                goto quit;
            }
        }

        /* Read input (gamepad 0 + keyboard merged) */
        InputState input = input_read_keyboard();
        if (IsGamepadAvailable(0)) {
            input = merge_input(input, input_read(0));
        }

        /* TODO: game update logic here */
        (void)input;

        particles_update(&particles, delta_time);

        BeginDrawing();
        render_background(screen_width, screen_height);
        render_particles(particles.items, particles.count);
        EndDrawing();
    }

quit:
    dzlog_info("exiting game loop (frame=%d t=%.1fs)", frame, elapsed);
    particles_free(&particles);
    audio_shutdown();
    CloseWindow();
    zlog_fini();
    return 0;
}
