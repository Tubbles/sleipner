#include "particle.h"
#include "alloc.h"
#include "random.h"
#include "raylib.h"
#include "vec.h"

#include <math.h>

#define PARTICLE_SPEED_MIN 50.0F
#define PARTICLE_SPEED_MAX 200.0F
#define PARTICLE_LIFE_MIN 0.5F
#define PARTICLE_LIFE_MAX 1.5F
#define PARTICLE_SIZE_MIN 3.0F
#define PARTICLE_SIZE_MAX 8.0F
#define PARTICLE_DRAG 0.98F

VEC_IMPL(particle, Particle)

void particles_init(ParticlePool *pool)
{
    *pool = (ParticlePool){0};
}

void particles_spawn(ParticlePool *pool, Allocator *alloc, RandomState *rng, Vector2 pos, Color color, int count)
{
    pool->items.alloc = *alloc;
    for (int index = 0; index < count; index++) {
        float angle = random_float_range(rng, 0.0F, 2.0F * PI);
        float speed = random_float_range(rng, PARTICLE_SPEED_MIN, PARTICLE_SPEED_MAX);
        Particle particle = {
            .position = pos,
            .velocity = {cosf(angle) * speed, sinf(angle) * speed},
            .color = color,
            .lifetime = random_float_range(rng, PARTICLE_LIFE_MIN, PARTICLE_LIFE_MAX),
            .size = random_float_range(rng, PARTICLE_SIZE_MIN, PARTICLE_SIZE_MAX),
        };
        (void)vec_particle_push(&pool->items, particle);
    }
}

void particles_update(ParticlePool *pool, float delta_time)
{
    int index = 0;
    while (index < pool->items.count) {
        Particle *particle = &pool->items.data[index];
        particle->lifetime -= delta_time;

        if (particle->lifetime <= 0.0F) {
            pool->items.data[index] = pool->items.data[pool->items.count - 1];
            pool->items.count--;
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
    vec_particle_free(&pool->items);
}
