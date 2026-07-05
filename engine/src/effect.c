#include "effect.h"

#include "alloc.h"
#include "strv.h"
#include "vec.h"

#include "raylib.h"

VEC_IMPL(sound_effect_request, SoundEffectRequest)
VEC_IMPL(camera_pan_request, CameraPanRequest)
VEC_IMPL(camera_shake_request, CameraShakeRequest)
VEC_IMPL(spawn_request, SpawnRequest)

void effect_queue_init(EffectQueue *queue, Allocator alloc)
{
    queue->sounds = vec_sound_effect_request_new(alloc);
    queue->camera_pans = vec_camera_pan_request_new(alloc);
    queue->camera_shakes = vec_camera_shake_request_new(alloc);
    queue->spawns = vec_spawn_request_new(alloc);
}

void effect_queue_clear(EffectQueue *queue)
{
    vec_sound_effect_request_clear(&queue->sounds);
    vec_camera_pan_request_clear(&queue->camera_pans);
    vec_camera_shake_request_clear(&queue->camera_shakes);
    vec_spawn_request_clear(&queue->spawns);
}

bool effect_queue_push_sound(EffectQueue *queue, Strv name)
{
    return vec_sound_effect_request_push(&queue->sounds, (SoundEffectRequest){.name = name});
}

bool effect_queue_push_camera_pan(EffectQueue *queue, Vector2 target, float duration)
{
    return vec_camera_pan_request_push(&queue->camera_pans, (CameraPanRequest){.target = target, .duration = duration});
}

bool effect_queue_push_camera_shake(EffectQueue *queue, CameraShakeRequest request)
{
    return vec_camera_shake_request_push(&queue->camera_shakes, request);
}

bool effect_queue_push_spawn(EffectQueue *queue, Strv blueprint, Vector2 position)
{
    SpawnRequest request = {.blueprint = blueprint, .x = position.x, .y = position.y};
    return vec_spawn_request_push(&queue->spawns, request);
}
