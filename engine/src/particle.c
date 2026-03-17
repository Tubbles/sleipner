#include "particle.h"
#include "raylib.h"

#include <math.h>
#include <stdlib.h>

#define PARTICLE_SPEED_MIN 50.0F
#define PARTICLE_SPEED_MAX 200.0F
#define PARTICLE_LIFE_MIN 0.5F
#define PARTICLE_LIFE_MAX 1.5F
#define PARTICLE_SIZE_MIN 3.0F
#define PARTICLE_SIZE_MAX 8.0F
#define PARTICLE_DRAG 0.98F

void particles_init(ParticlePool *pool)
{
    pool->capacity = PARTICLE_INITIAL_CAPACITY;
    pool->count = 0;
    pool->items = malloc(sizeof(Particle) * pool->capacity);
}

static void grow_if_needed(ParticlePool *pool, int needed)
{
    while (pool->count + needed > pool->capacity) {
        pool->capacity *= 2;
        Particle *tmp = realloc(pool->items, sizeof(Particle) * pool->capacity);
        if (tmp != NULL) {
            pool->items = tmp;
        }
    }
}

static float rand_float(float min, float max)
{
    return min + (((float)rand() / (float)RAND_MAX) * (max - min));
}

void particles_spawn(ParticlePool *pool, Vector2 pos, Color color, int count)
{
    grow_if_needed(pool, count);

    for (int index = 0; index < count; index++) {
        float angle = rand_float(0.0F, 2.0F * PI);
        float speed = rand_float(PARTICLE_SPEED_MIN, PARTICLE_SPEED_MAX);
        Particle particle = {
            .position = pos,
            .velocity = {cosf(angle) * speed, sinf(angle) * speed},
            .color = color,
            .lifetime = rand_float(PARTICLE_LIFE_MIN, PARTICLE_LIFE_MAX),
            .size = rand_float(PARTICLE_SIZE_MIN, PARTICLE_SIZE_MAX),
        };
        pool->items[pool->count++] = particle;
    }
}

void particles_update(ParticlePool *pool, float delta_time)
{
    int index = 0;
    while (index < pool->count) {
        Particle *particle = &pool->items[index];
        particle->lifetime -= delta_time;

        if (particle->lifetime <= 0.0F) {
            pool->items[index] = pool->items[pool->count - 1];
            pool->count--;
            continue;
        }

        particle->position.x += particle->velocity.x * delta_time;
        particle->position.y += particle->velocity.y * delta_time;
        particle->velocity.x *= PARTICLE_DRAG;
        particle->velocity.y *= PARTICLE_DRAG;
        index++;
    }
}

void particles_free(ParticlePool *pool)
{
    free(pool->items);
    pool->items = NULL;
    pool->count = 0;
    pool->capacity = 0;
}
