#ifndef PARTICLE_H
#define PARTICLE_H

#include "alloc.h"
#include "raylib.h"
#include "vec.h"

typedef struct {
    Vector2 position;
    Vector2 velocity;
    Color color;
    float lifetime;
    float size;
} Particle;

VEC_DECL(particle, Particle)

typedef struct {
    vec_particle items;
} ParticlePool;

void particles_init(ParticlePool *pool);
void particles_spawn(ParticlePool *pool, Allocator *alloc, Vector2 pos, Color color, int count);
void particles_update(ParticlePool *pool, float delta_time);
void particles_free(ParticlePool *pool, Allocator *alloc);

#endif
