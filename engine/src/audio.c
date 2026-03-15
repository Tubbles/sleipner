#include "audio.h"
#include "raylib.h"
#include <math.h>
#include <stdlib.h>

#define SAMPLE_RATE 44100
#define SAMPLE_MAX 32767.0F
#define BITS_PER_SAMPLE 16

#define TONE_FREQ_HZ 440.0F
#define TONE_DURATION_SEC 0.15F
#define TONE_VOLUME 0.3F

#define POP_BASE_FREQ 300.0F
#define POP_FREQ_RANGE 500.0F
#define POP_ATTACK_TIME 0.05F
#define POP_DURATION_SEC 0.12F
#define POP_VOLUME 0.3F

static Sound sounds[SOUND_COUNT];

static Wave generate_tone(float freq, float duration, float volume)
{
    int sample_count = (int)(SAMPLE_RATE * duration);
    short *data = malloc(sizeof(short) * sample_count);

    for (int index = 0; index < sample_count; index++) {
        float sample_time = (float)index / SAMPLE_RATE;
        float envelope = 1.0F - (sample_time / duration);
        float sample = sinf((2.0F * PI * freq) * sample_time) * envelope * volume;
        data[index] = (short)(sample * SAMPLE_MAX);
    }

    Wave wave = {
        .frameCount = sample_count,
        .sampleRate = SAMPLE_RATE,
        .sampleSize = BITS_PER_SAMPLE,
        .channels = 1,
        .data = data,
    };
    return wave;
}

static Wave generate_bubble_pop(float duration, float volume)
{
    int sample_count = (int)(SAMPLE_RATE * duration);
    short *data = malloc(sizeof(short) * sample_count);

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
        .frameCount = sample_count,
        .sampleRate = SAMPLE_RATE,
        .sampleSize = BITS_PER_SAMPLE,
        .channels = 1,
        .data = data,
    };
    return wave;
}

void audio_init(void)
{
    InitAudioDevice();

    Wave button_wave = generate_tone(TONE_FREQ_HZ, TONE_DURATION_SEC, TONE_VOLUME);
    sounds[SOUND_BUTTON] = LoadSoundFromWave(button_wave);
    free(button_wave.data);

    Wave collision_wave = generate_bubble_pop(POP_DURATION_SEC, POP_VOLUME);
    sounds[SOUND_COLLISION] = LoadSoundFromWave(collision_wave);
    free(collision_wave.data);
}

void audio_play(SoundKind kind)
{
    if (kind >= 0 && kind < SOUND_COUNT) {
        PlaySound(sounds[kind]);
    }
}

void audio_shutdown(void)
{
    for (int index = 0; index < SOUND_COUNT; index++) {
        UnloadSound(sounds[index]);
    }
    CloseAudioDevice();
}
