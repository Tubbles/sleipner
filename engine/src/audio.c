#include "audio.h"

#include "raylib.h"
#include <math.h>

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
