#include "random.h"

#include <stdint.h>

/* splitmix64 (Vigna, public domain): https://prng.di.unimi.it/splitmix64.c */
#define SPLITMIX64_GOLDEN_GAMMA UINT64_C(0x9E3779B97F4A7C15)
#define SPLITMIX64_MULTIPLIER_1 UINT64_C(0xBF58476D1CE4E5B9)
#define SPLITMIX64_MULTIPLIER_2 UINT64_C(0x94D049BB133111EB)
#define SPLITMIX64_SHIFT_1 30
#define SPLITMIX64_SHIFT_2 27
#define SPLITMIX64_SHIFT_3 31

/* xoshiro256** (Blackman & Vigna, public domain):
 * https://prng.di.unimi.it/xoshiro256starstar.c */
#define XOSHIRO256SS_SCRAMBLE_MULTIPLIER_1 5
#define XOSHIRO256SS_SCRAMBLE_ROTATE 7
#define XOSHIRO256SS_SCRAMBLE_MULTIPLIER_2 9
#define XOSHIRO256SS_UPDATE_SHIFT 17
#define XOSHIRO256SS_UPDATE_ROTATE 45
#define UINT64_BIT_WIDTH 64

/* uint64 -> [0, 1) double conversion, top-53-bits reference recipe. */
#define RANDOM_DOUBLE_MANTISSA_SHIFT 11
#define RANDOM_UNIT_INTERVAL_SCALE 0x1.0p-53

static uint64_t rotate_left_uint64(uint64_t value, int bit_count)
{
    return (value << bit_count) | (value >> (UINT64_BIT_WIDTH - bit_count));
}

static uint64_t splitmix64_next(uint64_t *splitmix_state)
{
    uint64_t mixed = (*splitmix_state += SPLITMIX64_GOLDEN_GAMMA);
    mixed = (mixed ^ (mixed >> SPLITMIX64_SHIFT_1)) * SPLITMIX64_MULTIPLIER_1;
    mixed = (mixed ^ (mixed >> SPLITMIX64_SHIFT_2)) * SPLITMIX64_MULTIPLIER_2;
    return mixed ^ (mixed >> SPLITMIX64_SHIFT_3);
}

void random_seed(RandomState *rng, uint64_t seed)
{
    uint64_t splitmix_state = seed;
    for (int word_index = 0; word_index < 4; word_index++) {
        rng->state[word_index] = splitmix64_next(&splitmix_state);
    }
}

uint64_t random_next(RandomState *rng)
{
    uint64_t *state = rng->state;
    const uint64_t result =
        rotate_left_uint64(state[1] * XOSHIRO256SS_SCRAMBLE_MULTIPLIER_1, XOSHIRO256SS_SCRAMBLE_ROTATE) *
        XOSHIRO256SS_SCRAMBLE_MULTIPLIER_2;
    const uint64_t update_term = state[1] << XOSHIRO256SS_UPDATE_SHIFT;

    state[2] ^= state[0];
    state[3] ^= state[1];
    state[1] ^= state[2];
    state[0] ^= state[3];

    state[2] ^= update_term;
    state[3] = rotate_left_uint64(state[3], XOSHIRO256SS_UPDATE_ROTATE);

    return result;
}

float random_float_range(RandomState *rng, float min, float max)
{
    uint64_t raw = random_next(rng);
    double unit_interval = (double)(raw >> RANDOM_DOUBLE_MANTISSA_SHIFT) * RANDOM_UNIT_INTERVAL_SCALE;
    return (float)((double)min + (unit_interval * ((double)max - (double)min)));
}

int random_int_range(RandomState *rng, int min, int max)
{
    if (min == max) {
        return min;
    }
    uint64_t range = (uint64_t)(max - min) + 1;
    return min + (int)(random_next(rng) % range);
}
