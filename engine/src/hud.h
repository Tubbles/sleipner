#pragma once

#include "raylib.h"

/* HUD v1 (S6.12a, D34): player health shown as a row of hearts, top-left,
 * in HALVES -- one heart per HUD_HEALTH_PER_HEART health points. Drawn in
 * SCREEN space (not world/camera space), after the world render and before
 * the menu/settings overlays (see main.c's render_frame). This module is a
 * pure layout/state half (hud_compute_hearts, hud_heart_screen_position --
 * both headless-testable) plus a thin raylib render half (hud_draw_hearts,
 * production only, never called from tests). The inventory pause-menu
 * screen is a separate, deferred slice (S6.12b); the minimap is deferred
 * further still. Hearts are drawn as placeholder rectangles -- no heart
 * sprite asset exists yet (TODO.md). */

typedef enum {
    HUD_HEART_EMPTY,
    HUD_HEART_HALF,
    HUD_HEART_FULL,
} HudHeartState;

/* Health points represented by one full heart. A half heart is exactly
 * half this value. */
#define HUD_HEALTH_PER_HEART 2

/* Cap on displayable hearts -- screen-space HUD real estate, not a
 * gameplay balance limit: a placeholder row of icons anchored top-left has
 * to stop somewhere before it runs off the default 800px-wide window.
 * Health above the cap (HUD_MAX_HEARTS_DISPLAYED * HUD_HEALTH_PER_HEART)
 * still works correctly everywhere else (damage, defense, defeat) -- only
 * the heart ROW clamps, matching classic action-RPG heart-container counts
 * (rarely more than ~20). The "player" blueprint currently authors
 * health = [100, 100] (50 half-hearts), which will clamp hard against this
 * cap until gamedata is retuned for a hearts-sized health range
 * (TODO.md) -- that retuning is a content change, not a code change. */
#define HUD_MAX_HEARTS_DISPLAYED 20

typedef struct {
    HudHeartState hearts[HUD_MAX_HEARTS_DISPLAYED];
    int count;
} HudHearts;

/* current/max player health, raw health points (not hearts). A struct
 * rather than two adjacent float parameters on hud_compute_hearts below --
 * clang-tidy's bugprone-easily-swappable-parameters flags two same-typed
 * floats in a row, the same reason effect.h's CameraShakeRequest wraps
 * magnitude/duration instead of taking them as separate parameters. */
typedef struct {
    float current;
    float max;
} HudPlayerHealth;

/* Pure: maps current/max player health to a row of heart states.
 * `health.current` is clamped to [0, health.max] and rounded to the
 * nearest half-heart (the finest displayable granularity, one health
 * point) before classification, so float noise never produces a
 * flickering half-heart. `health.max` is clamped to >= 0. Heart count is
 * ceil(health.max / HUD_HEALTH_PER_HEART), clamped to
 * HUD_MAX_HEARTS_DISPLAYED -- an odd max (e.g. 5) makes the last heart a
 * half-slot: at full health it displays HUD_HEART_HALF, since only one
 * health point fills that heart's capacity. */
[[nodiscard]] HudHearts hud_compute_hearts(HudPlayerHealth health);

/* Pure: top-left screen position of heart `heart_index` (0-based), spaced
 * HUD_HEART_SIZE + HUD_HEART_SPACING apart. Anchored at a fixed margin from
 * the screen's top-left corner -- does not depend on screen size. */
[[nodiscard]] Vector2 hud_heart_screen_position(int heart_index);

/* Render (production only): draws `hearts` in screen space as placeholder
 * shapes -- a filled square for a full heart, a half-filled square for a
 * half heart, an outlined square for an empty heart. Call after the
 * world's EndMode2D/EndTextureMode and before menu_render/settings_render
 * (main.c's render_frame). Never call from headless tests. */
void hud_draw_hearts(HudHearts hearts);
