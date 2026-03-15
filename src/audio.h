#ifndef AUDIO_H
#define AUDIO_H

typedef enum { SOUND_BUTTON, SOUND_COLLISION, SOUND_COUNT } SoundKind;

void audio_init(void);
void audio_play(SoundKind kind);
void audio_shutdown(void);

#endif
