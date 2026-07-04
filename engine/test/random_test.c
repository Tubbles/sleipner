#include "fff.h"
#include "unity.h"

#include "../src/random.c" // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

/* Known-answer tests: expected values are the first four raw outputs of
 * the public-domain reference implementation (splitmix64 seeding four
 * xoshiro256** state words, then xoshiro256** itself), hand-derived by
 * running the reference C source at
 * https://prng.di.unimi.it/splitmix64.c and
 * https://prng.di.unimi.it/xoshiro256starstar.c against these seeds. Any
 * future refactor that silently changes the generator must fail these. */
void test_random_next_known_answer_seed_zero(void)
{
    RandomState rng;
    random_seed(&rng, 0);
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x99EC5F36CB75F2B4), random_next(&rng));
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0xBF6E1F784956452A), random_next(&rng));
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x1A5F849D4933E6E0), random_next(&rng));
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x6AA594F1262D2D2C), random_next(&rng));
}

void test_random_next_known_answer_seed_42(void)
{
    RandomState rng;
    random_seed(&rng, 42);
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x15780B2E0C2EC716), random_next(&rng));
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x6104D9866D113A7E), random_next(&rng));
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0xAE17533239E499A1), random_next(&rng));
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0xECB8AD4703B360A1), random_next(&rng));
}

void test_random_determinism_same_seed_reproduces_sequence(void)
{
    RandomState rng_a;
    RandomState rng_b;
    random_seed(&rng_a, 2024);
    random_seed(&rng_b, 2024);
    for (int index = 0; index < 8; index++) {
        TEST_ASSERT_EQUAL_UINT64(random_next(&rng_a), random_next(&rng_b));
    }
}

void test_random_determinism_different_seeds_diverge(void)
{
    RandomState rng_a;
    RandomState rng_b;
    random_seed(&rng_a, 1);
    random_seed(&rng_b, 2);
    TEST_ASSERT_NOT_EQUAL_UINT64(random_next(&rng_a), random_next(&rng_b));
}

void test_random_float_range_stays_within_bounds(void)
{
    RandomState rng;
    random_seed(&rng, 99);
    float min = -5.0f;
    float max = 10.0f;
    for (int index = 0; index < 2000; index++) {
        float value = random_float_range(&rng, min, max);
        TEST_ASSERT_TRUE(value >= min);
        TEST_ASSERT_TRUE(value <= max);
    }
}

void test_random_float_range_degenerate_returns_min(void)
{
    RandomState rng;
    random_seed(&rng, 55);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, random_float_range(&rng, 3.0f, 3.0f));
}

void test_random_int_range_covers_and_stays_within_bounds(void)
{
    RandomState rng;
    random_seed(&rng, 7);
    int min = 0;
    int max = 4;
    int min_seen = max;
    int max_seen = min;
    for (int index = 0; index < 2000; index++) {
        int value = random_int_range(&rng, min, max);
        TEST_ASSERT_TRUE(value >= min);
        TEST_ASSERT_TRUE(value <= max);
        if (value < min_seen) {
            min_seen = value;
        }
        if (value > max_seen) {
            max_seen = value;
        }
    }
    TEST_ASSERT_EQUAL_INT(min, min_seen);
    TEST_ASSERT_EQUAL_INT(max, max_seen);
}

void test_random_int_range_degenerate_returns_min(void)
{
    RandomState rng;
    random_seed(&rng, 55);
    TEST_ASSERT_EQUAL_INT(7, random_int_range(&rng, 7, 7));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_random_next_known_answer_seed_zero);
    RUN_TEST(test_random_next_known_answer_seed_42);
    RUN_TEST(test_random_determinism_same_seed_reproduces_sequence);
    RUN_TEST(test_random_determinism_different_seeds_diverge);
    RUN_TEST(test_random_float_range_stays_within_bounds);
    RUN_TEST(test_random_float_range_degenerate_returns_min);
    RUN_TEST(test_random_int_range_covers_and_stays_within_bounds);
    RUN_TEST(test_random_int_range_degenerate_returns_min);
    return UNITY_END();
}
