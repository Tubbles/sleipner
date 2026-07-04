#pragma once

#include <stdint.h>

/* xoshiro256** (Blackman & Vigna) seeded via splitmix64 (Vigna). Both
 * reference algorithms are public domain / CC0:
 *   https://prng.di.unimi.it/splitmix64.c
 *   https://prng.di.unimi.it/xoshiro256starstar.c
 *
 * RandomState is 256 bits of opaque generator state. It is runtime session
 * state (see GameState.rng in game.h) — NOT gamedata, so it lives outside
 * GamedataState and is never undo-snapshotted or reloaded. No statics: every
 * function here takes the state explicitly. */
typedef struct {
    uint64_t state[4];
} RandomState;

/* Seed the 256-bit state by running splitmix64 from `seed` four times,
 * writing each output into state[0..3] in order. This is the canonical
 * xoshiro256** seeding recipe: a single 64-bit seed carries too little
 * entropy to fill 256 bits directly, and splitmix64 spreads it out while
 * avoiding an all-zero state (xoshiro256** requires at least one nonzero
 * word). */
void random_seed(RandomState *rng, uint64_t seed);

/* Raw 64-bit output of the xoshiro256** generator. Every other function in
 * this header builds on this call; it is also the exact quantity pinned by
 * the known-answer test in random_test.c against the public reference
 * implementation. */
uint64_t random_next(RandomState *rng);

/* Uniform float in [min, max]. Builds a double in [0, 1) from the top 53
 * bits of a random_next() draw (the reference's recommended
 * "(x >> 11) * 0x1.0p-53" construction, matching the double's mantissa
 * width), then scales into [min, max] and narrows to float. min == max
 * returns min (the scale term vanishes). Requires min <= max. */
float random_float_range(RandomState *rng, float min, float max);

/* Uniform int in [min, max], inclusive on both ends. min == max returns
 * min without drawing from the generator. Requires min <= max. */
int random_int_range(RandomState *rng, int min, int max);
