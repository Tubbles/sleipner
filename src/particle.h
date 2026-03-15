#ifndef PARTICLE_H
#define PARTICLE_H

#include "raylib.h" // IWYU pragma: export

#define PARTICLE_INITIAL_CAPACITY 256

typedef struct {
    Vector2 position;
    Vector2 velocity;
    Color color;
    float lifetime;
    float size;
} Particle;

typedef struct {
    Particle *items;
    int count;
    int capacity;
} ParticlePool;

void particles_init(ParticlePool *pool);
void particles_spawn(ParticlePool *pool, Vector2 pos, Color color, int count);
void particles_update(ParticlePool *pool, float delta_time);
void particles_free(ParticlePool *pool);

#endif
