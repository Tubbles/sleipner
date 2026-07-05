#include "audio.h"

#include "strv.h"

#include "raylib.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* strv_hash (strv.h) is the shared FNV-1a hash for every Strv-keyed
 * MAP_DECL -- see rule.h's map_strv_int (S6.8a) for the other one; every
 * other MAP_DECL keys on int and reuses map_hash_int/map_eq_int from
 * map.h. */
MAP_IMPL(strv_sound, Strv, Sound, strv_hash, strv_eq)
MAP_IMPL(strv_music, Strv, Music, strv_hash, strv_eq)

#define SAMPLE_RATE 44100
#define SAMPLE_MAX 32767.0F
#define BITS_PER_SAMPLE 16
#define MAX_AUDIO_SAMPLE_COUNT 7000

#define TONE_FREQ_HZ 440.0F
#define TONE_DURATION_SEC 0.15F
#define TONE_VOLUME 0.3F

#define POP_BASE_FREQ 300.0F
#define POP_FREQ_RANGE 500.0F
#define POP_ATTACK_TIME 0.05F
#define POP_DURATION_SEC 0.12F
#define POP_VOLUME 0.3F

// freq and duration are two of the three floats that together fully
// describe one synthesized tone; a wrapper struct would just re-box the
// same tone spec for this single-use helper.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static Sound generate_tone(float freq, float duration, float volume)
{
    int sample_count = (int)(SAMPLE_RATE * duration);
    short data[MAX_AUDIO_SAMPLE_COUNT];

    for (int index = 0; index < sample_count; index++) {
        float sample_time = (float)index / SAMPLE_RATE;
        float envelope = 1.0F - (sample_time / duration);
        float sample = sinf((2.0F * PI * freq) * sample_time) * envelope * volume;
        data[index] = (short)(sample * SAMPLE_MAX);
    }

    Wave wave = {
        .frameCount = (unsigned int)sample_count,
        .sampleRate = SAMPLE_RATE,
        .sampleSize = BITS_PER_SAMPLE,
        .channels = 1,
        .data = data,
    };
    return LoadSoundFromWave(wave);
}

// duration and volume are the pop envelope's length and loudness, the
// same two-float DSP spec pattern as generate_tone above.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static Sound generate_bubble_pop(float duration, float volume)
{
    int sample_count = (int)(SAMPLE_RATE * duration);
    short data[MAX_AUDIO_SAMPLE_COUNT];

    for (int index = 0; index < sample_count; index++) {
        float sample_time = (float)index / SAMPLE_RATE;
        float progress = sample_time / duration;
        /* Quick rise from 300Hz to 800Hz, then drop off */
        float freq = POP_BASE_FREQ + (POP_FREQ_RANGE * sinf(progress * PI * 0.5F));
        /* Fast attack, quick decay */
        float envelope = (1.0F - progress) * (1.0F - progress);
        if (progress < POP_ATTACK_TIME) {
            envelope *= progress / POP_ATTACK_TIME;
        }
        float sample = sinf((2.0F * PI * freq) * sample_time) * envelope * volume;
        data[index] = (short)(sample * SAMPLE_MAX);
    }

    Wave wave = {
        .frameCount = (unsigned int)sample_count,
        .sampleRate = SAMPLE_RATE,
        .sampleSize = BITS_PER_SAMPLE,
        .channels = 1,
        .data = data,
    };
    return LoadSoundFromWave(wave);
}

void audio_init(AudioState *audio)
{
    InitAudioDevice();

    audio->sounds[SOUND_BUTTON] = generate_tone(TONE_FREQ_HZ, TONE_DURATION_SEC, TONE_VOLUME);
    audio->sounds[SOUND_COLLISION] = generate_bubble_pop(POP_DURATION_SEC, POP_VOLUME);
}

void audio_play(AudioState *audio, SoundKind kind)
{
    if ((int)kind >= 0 && (int)kind < SOUND_COUNT) {
        PlaySound(audio->sounds[kind]);
    }
}

void audio_shutdown(AudioState *audio)
{
    for (int index = 0; index < SOUND_COUNT; index++) {
        UnloadSound(audio->sounds[index]);
    }
    CloseAudioDevice();
}

int sfx_alias_pool_next_slot(SfxAliasPool *pool)
{
    int slot = pool->next_slot;
    pool->next_slot = (pool->next_slot + 1) % SFX_MAX_CONCURRENT_ALIASES;
    return slot;
}

void sfx_alias_pool_play(SfxAliasPool *pool, Sound sound, float volume)
{
    SfxAliasSlot *entry = &pool->slots[sfx_alias_pool_next_slot(pool)];
    if (entry->in_use) {
        StopSound(entry->alias);
        UnloadSoundAlias(entry->alias);
    }
    entry->alias = LoadSoundAlias(sound);
    entry->in_use = true;
    SetSoundVolume(entry->alias, volume);
    PlaySound(entry->alias);
}

MusicCrossfadeGain music_crossfade_gain(float timer, float total)
{
    if (timer <= 0.0F || total <= 0.0F) {
        return (MusicCrossfadeGain){.outgoing_gain = 0.0F, .incoming_gain = 1.0F};
    }
    float clamped_timer = timer > total ? total : timer;
    float outgoing_gain = clamped_timer / total;
    return (MusicCrossfadeGain){.outgoing_gain = outgoing_gain, .incoming_gain = 1.0F - outgoing_gain};
}

/* Fixed-size name copy, always null-terminated -- same idiom main.c uses
 * for TextureEntry.filename/FontPreviewEntry.name. */
static void music_track_name_copy(char destination[MUSIC_TRACK_NAME_LEN], const char *source)
{
    strncpy(destination, source, MUSIC_TRACK_NAME_LEN - 1);
    destination[MUSIC_TRACK_NAME_LEN - 1] = '\0';
}

void music_on_level_changed(MusicState *music, const char *new_track_name)
{
    const char *safe_new_name = new_track_name ? new_track_name : "";
    if (strncmp(music->current_track_name, safe_new_name, MUSIC_TRACK_NAME_LEN) == 0) {
        return;
    }
    if (music->current_track_name[0] != '\0') {
        music_track_name_copy(music->outgoing_track_name, music->current_track_name);
        music->crossfade_timer = MUSIC_CROSSFADE_SECONDS;
    }
    music_track_name_copy(music->current_track_name, safe_new_name);
}

void music_state_tick(MusicState *music, float delta_time)
{
    if (music->crossfade_timer <= 0.0F) {
        return;
    }
    music->crossfade_timer -= delta_time;
    if (music->crossfade_timer <= 0.0F) {
        music->crossfade_timer = 0.0F;
        music->outgoing_track_name[0] = '\0';
    }
}
