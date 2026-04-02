#ifndef AUDIO_H
#define AUDIO_H

#include "raylib.h"

typedef enum { SOUND_BUTTON, SOUND_COLLISION, SOUND_COUNT } SoundKind;

typedef struct {
    Sound sounds[SOUND_COUNT];
} AudioState;

void audio_init(AudioState *audio);
void audio_play(AudioState *audio, SoundKind kind);
void audio_shutdown(AudioState *audio);

#endif
