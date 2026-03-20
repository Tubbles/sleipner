#ifndef AUDIO_H
#define AUDIO_H

typedef enum { SOUND_BUTTON, SOUND_COLLISION, SOUND_COUNT } SoundKind;

/* GameState is defined in game.h - audio.c includes game.h for the full definition */
void audio_init(void *state);
void audio_play(void *state, SoundKind kind);
void audio_shutdown(void *state);

#endif
