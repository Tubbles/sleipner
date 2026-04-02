#include "fff.h"
#include "unity.h"

#include "../src/particle.c" // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

#include "test_heap_alloc.h"

#include <stdlib.h>

static ParticlePool pool;

void setUp(void) {}
void tearDown(void) {}

void test_particle_init(void)
{
    particles_init(&pool);
    TEST_ASSERT_EQUAL_INT(0, pool.items.count);
    TEST_ASSERT_NULL(pool.items.data);
}

void test_particle_spawn_increases_count(void)
{
    particles_init(&pool);
    particles_spawn(&pool, &test_heap_alloc, (Vector2){100, 100}, RED, 10);
    TEST_ASSERT_EQUAL_INT(10, pool.items.count);
    particles_free(&pool);
}

void test_particle_lifetime_expiry(void)
{
    particles_init(&pool);
    particles_spawn(&pool, &test_heap_alloc, (Vector2){100, 100}, RED, 5);

    /* Update with a very large dt to expire all particles */
    particles_update(&pool, 100.0f);
    TEST_ASSERT_EQUAL_INT(0, pool.items.count);
    particles_free(&pool);
}

void test_particle_position_updates(void)
{
    particles_init(&pool);
    srand(42);
    particles_spawn(&pool, &test_heap_alloc, (Vector2){0, 0}, RED, 1);

    Vector2 initial_pos = pool.items.data[0].position;
    particles_update(&pool, 0.1f);

    /* Position should have changed (particle has velocity) */
    bool moved = (pool.items.data[0].position.x != initial_pos.x) || (pool.items.data[0].position.y != initial_pos.y);
    TEST_ASSERT_TRUE(moved);
    particles_free(&pool);
}

void test_particle_capacity_grows(void)
{
    particles_init(&pool);
    int big_count = 356;
    particles_spawn(&pool, &test_heap_alloc, (Vector2){0, 0}, RED, big_count);
    TEST_ASSERT_EQUAL_INT(big_count, pool.items.count);
    TEST_ASSERT_TRUE(pool.items.capacity >= big_count);
    particles_free(&pool);
}

void test_particle_free_cleans_up(void)
{
    particles_init(&pool);
    particles_spawn(&pool, &test_heap_alloc, (Vector2){0, 0}, RED, 10);
    particles_free(&pool);
    TEST_ASSERT_NULL(pool.items.data);
    TEST_ASSERT_EQUAL_INT(0, pool.items.count);
    TEST_ASSERT_EQUAL_INT(0, pool.items.capacity);
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();
    RUN_TEST(test_particle_init);
    RUN_TEST(test_particle_spawn_increases_count);
    RUN_TEST(test_particle_lifetime_expiry);
    RUN_TEST(test_particle_position_updates);
    RUN_TEST(test_particle_capacity_grows);
    RUN_TEST(test_particle_free_cleans_up);
    return UNITY_END();
}
