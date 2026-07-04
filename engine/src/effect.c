#include "effect.h"

#include "alloc.h"
#include "debug.h"
#include "diag.h"
#include "strv.h"
#include "vec.h"

#include "raylib.h"

VEC_IMPL(sound_effect_request, SoundEffectRequest)
VEC_IMPL(camera_pan_request, CameraPanRequest)
VEC_IMPL(camera_shake_request, CameraShakeRequest)
VEC_IMPL(spawn_request, SpawnRequest)
VEC_IMPL(dialogue_request, DialogueRequest)

void effect_queue_init(EffectQueue *queue, Allocator alloc)
{
    queue->sounds = vec_sound_effect_request_new(alloc);
    queue->camera_pans = vec_camera_pan_request_new(alloc);
    queue->camera_shakes = vec_camera_shake_request_new(alloc);
    queue->spawns = vec_spawn_request_new(alloc);
    queue->dialogues = vec_dialogue_request_new(alloc);
}

void effect_queue_clear(EffectQueue *queue)
{
    vec_sound_effect_request_clear(&queue->sounds);
    vec_camera_pan_request_clear(&queue->camera_pans);
    vec_camera_shake_request_clear(&queue->camera_shakes);
    vec_spawn_request_clear(&queue->spawns);
    vec_dialogue_request_clear(&queue->dialogues);
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

bool effect_queue_push_dialogue(EffectQueue *queue, Strv text)
{
    return vec_dialogue_request_push(&queue->dialogues, (DialogueRequest){.text = text});
}

static void log_sound_effects(Diag *diag, const vec_sound_effect_request *sounds)
{
    for (int index = 0; index < sounds->count; index++) {
        const SoundEffectRequest *request = &sounds->data[index];
        debug_log(diag->debug, "effect: sound %.*s", (int)request->name.len, request->name.ptr);
    }
}

static void log_camera_pan_effects(Diag *diag, const vec_camera_pan_request *camera_pans)
{
    for (int index = 0; index < camera_pans->count; index++) {
        const CameraPanRequest *request = &camera_pans->data[index];
        debug_log(diag->debug, "effect: camera_pan target=(%.1f, %.1f) duration=%.2f", (double)request->target.x,
                  (double)request->target.y, (double)request->duration);
    }
}

static void log_camera_shake_effects(Diag *diag, const vec_camera_shake_request *camera_shakes)
{
    for (int index = 0; index < camera_shakes->count; index++) {
        const CameraShakeRequest *request = &camera_shakes->data[index];
        debug_log(diag->debug, "effect: camera_shake magnitude=%.2f duration=%.2f", (double)request->magnitude,
                  (double)request->duration);
    }
}

static void log_spawn_effects(Diag *diag, const vec_spawn_request *spawns)
{
    for (int index = 0; index < spawns->count; index++) {
        const SpawnRequest *request = &spawns->data[index];
        debug_log(diag->debug, "effect: spawn %.*s at (%.1f, %.1f)", (int)request->blueprint.len,
                  request->blueprint.ptr, (double)request->x, (double)request->y);
    }
}

static void log_dialogue_effects(Diag *diag, const vec_dialogue_request *dialogues)
{
    for (int index = 0; index < dialogues->count; index++) {
        const DialogueRequest *request = &dialogues->data[index];
        debug_log(diag->debug, "effect: dialogue %.*s", (int)request->text.len, request->text.ptr);
    }
}

void effect_queue_drain(Diag *diag, EffectQueue *queue)
{
    log_sound_effects(diag, &queue->sounds);
    log_camera_pan_effects(diag, &queue->camera_pans);
    log_camera_shake_effects(diag, &queue->camera_shakes);
    log_spawn_effects(diag, &queue->spawns);
    log_dialogue_effects(diag, &queue->dialogues);
    effect_queue_clear(queue);
}
