#ifndef AUDIO_H
#define AUDIO_H

typedef enum { SOUND_BUTTON, SOUND_COLLISION, SOUND_COUNT } SoundKind;

struct EngineContext;

void audio_init(struct EngineContext *ctx);
void audio_play(struct EngineContext *ctx, SoundKind kind);
void audio_shutdown(struct EngineContext *ctx);

#endif
