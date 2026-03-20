#ifndef AUDIO_H
#define AUDIO_H

#include "raylib.h"

typedef enum { SOUND_BUTTON, SOUND_COLLISION, SOUND_COUNT } SoundKind;

/* Audio state - instantiated by owner, used by audio module */
typedef struct {
    Sound audio_sounds[SOUND_COUNT];
} AudioState;

/* Initialize the audio system. Call once at startup.
 * state: pointer to AudioState struct (owned by caller) */
void audio_init(AudioState *state);

/* Play a sound effect.
 * state: pointer to AudioState struct
 * kind: which sound to play */
void audio_play(AudioState *state, SoundKind kind);

/* Shut down the audio system. Call once at shutdown. */
void audio_shutdown(AudioState *state);

#endif
