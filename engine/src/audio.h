#pragma once

#include "map.h" // IWYU pragma: export
#include "strv.h"

#include "raylib.h"

#include <stdbool.h>

typedef enum { SOUND_BUTTON, SOUND_COLLISION, SOUND_COUNT } SoundKind;

/* Embedded SFX registry: name -> Sound (S6.4, D32). Populated once at
 * startup by main.c's load_persistent_assets, into gamedata_arena below
 * gamedata_base -- same persistent, survives-every-reload lifetime as the
 * texture registry (game.h AssetRegistry.textures). Keys are full
 * filenames with extension ("pickup.wav"), mirroring how the texture
 * registry keys on "player.png" etc. Never populated in headless tests
 * (main.c's asset-loading path is the only caller), so a headless lookup
 * always misses and the caller's not-found branch runs instead of any
 * raylib audio call -- the headless guard is architectural, not a runtime
 * check. */
MAP_DECL(strv_sound, Strv, Sound)

/* Embedded music registry: name -> Music (S6.13b, D32). Populated once at
 * startup by main.c's load_persistent_assets, into gamedata_arena below
 * gamedata_base -- same persistent, survives-every-reload lifetime as the
 * SFX/texture registries above. Keys are full filenames with extension
 * ("bgm.mp3"), mirroring map_strv_sound's convention. Never populated in
 * headless tests -- see map_strv_sound's doc comment above for why that
 * guard is architectural, not a runtime check. */
MAP_DECL(strv_music, Strv, Music)

/* D32: exactly this many concurrent plays are allowed system-wide,
 * drop-oldest when full. */
#define SFX_MAX_CONCURRENT_ALIASES 8

/* One slot in the alias concurrency cap. `in_use` distinguishes a slot
 * that has never been claimed (nothing to stop/unload) from one that is
 * up for eviction (stop + unload before reuse). */
typedef struct {
    Sound alias;
    bool in_use;
} SfxAliasSlot;

/* Fixed SFX_MAX_CONCURRENT_ALIASES-slot ring. `next_slot` is a bump
 * cursor, not a free-list index: once every slot has been claimed once,
 * the next claim always evicts the least-recently-claimed alias, which is
 * drop-oldest by construction -- no separate LRU bookkeeping needed. */
typedef struct {
    SfxAliasSlot slots[SFX_MAX_CONCURRENT_ALIASES];
    int next_slot;
} SfxAliasPool;

typedef struct {
    Sound sounds[SOUND_COUNT];
    SfxAliasPool sfx_aliases;
} AudioState;

void audio_init(AudioState *audio);
void audio_play(AudioState *audio, SoundKind kind);
void audio_shutdown(AudioState *audio);

/* Pure: returns the slot to (re)use for the next alias play and advances
 * the cursor with wraparound. No raylib calls -- safe to unit test
 * without an audio device (see audio_test.c). */
[[nodiscard]] int sfx_alias_pool_next_slot(SfxAliasPool *pool);

/* Plays `sound` through the alias cap in `pool` at `volume` (typically
 * preferences_effective_sfx_volume(&state->preferences), D32/F31,
 * S6.13a): evicts (stops + unloads) whatever alias currently occupies
 * the claimed slot, loads a fresh alias of `sound`, sets its volume,
 * and plays it. Requires an initialized audio device -- callers only
 * reach this after a registry hit, and the registry is only populated
 * by main.c's production asset-loading path, so this never fires from
 * a headless test. */
void sfx_alias_pool_play(SfxAliasPool *pool, Sound sound, float volume);

/* Per-level music (S6.13b, D32). Crossfade duration and the fixed track-
 * name buffer size are both named constants -- see MusicState below. */
#define MUSIC_CROSSFADE_SECONDS 1.0F
#define MUSIC_TRACK_NAME_LEN 64

/* Runtime crossfade state, keyed on track NAME rather than a loaded Music
 * handle -- this is what makes the transition logic headless-testable
 * without an audio device. `current_track_name` is the track the level
 * that is current right now selects (Level.music_name, level.h);
 * `outgoing_track_name` is non-empty only while a crossfade is in flight
 * (the track that was current before the last change). crossfade_timer
 * counts seconds remaining, 0 meaning "no crossfade". Lives on GameState,
 * not GamedataState: it is session-runtime state, not undo-snapshotted,
 * and (like CameraEffect, game.h) must not be disturbed by anything other
 * than an actual level change. main.c's stream layer reads this each
 * frame to decide which registry Music streams (map_strv_music above) to
 * play/update/fade and which to stop. */
typedef struct {
    char current_track_name[MUSIC_TRACK_NAME_LEN];
    char outgoing_track_name[MUSIC_TRACK_NAME_LEN];
    float crossfade_timer;
} MusicState;

/* Linear crossfade gain pair for a moment `timer` seconds into a
 * `total`-second crossfade (both in seconds). At timer == total the
 * outgoing track is still full volume and the incoming track is silent;
 * at timer <= 0 (or total <= 0) the crossfade is over: outgoing 0,
 * incoming 1. Clamps timer to [0, total] first, so a stale/overlong timer
 * never yields a gain outside [0, 1]. Pure -- no raylib calls, safe to
 * unit test without an audio device (see audio_test.c). */
typedef struct {
    float outgoing_gain;
    float incoming_gain;
} MusicCrossfadeGain;

[[nodiscard]] MusicCrossfadeGain music_crossfade_gain(float timer, float total);

/* Call wherever state->gamedata.current_level becomes current: after a
 * fresh load/hot-reload (game_load_gamedata) and after the editor's direct
 * current_level swap (level_activate, both game.c). `new_track_name` is
 * the new current level's music_name.ptr (nullptr if the level authors no
 * `music` field -- treated the same as ""). If it names the same track
 * music already holds as current (including both empty), this is a no-op:
 * no crossfade restart. Otherwise: if a track was already current, it
 * becomes the outgoing (fading) track and crossfade_timer resets to
 * MUSIC_CROSSFADE_SECONDS; if nothing was current yet (fresh boot), the
 * new track just becomes current with no crossfade -- there is nothing to
 * fade from. Pure string/float state -- no raylib calls. */
void music_on_level_changed(MusicState *music, const char *new_track_name);

/* Ticks crossfade_timer down by delta_time, floored at 0. Once the timer
 * reaches 0, clears outgoing_track_name -- that is the signal main.c's
 * stream layer uses to stop the track that just finished fading out. Call
 * once per frame from game_update (game.c), so the crossfade advances
 * headlessly along with everything else game_update ticks. */
void music_state_tick(MusicState *music, float delta_time);
