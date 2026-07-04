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

/* Plays `sound` through the alias cap in `pool`: evicts (stops + unloads)
 * whatever alias currently occupies the claimed slot, loads a fresh alias
 * of `sound`, and plays it. Requires an initialized audio device --
 * callers only reach this after a registry hit, and the registry is only
 * populated by main.c's production asset-loading path, so this never
 * fires from a headless test. */
void sfx_alias_pool_play(SfxAliasPool *pool, Sound sound);
